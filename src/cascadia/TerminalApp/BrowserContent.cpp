// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "BrowserContent.h"

#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.Web.WebView2.Core.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <wil/cppwinrt_helpers.h>

#include <string>
#include <algorithm>
#include <cwctype>

using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::UI::Xaml;
using namespace winrt::Windows::UI::Xaml::Controls;
using namespace winrt::Microsoft::Terminal::Settings::Model;

namespace winrt::TerminalApp::implementation
{
    static constexpr std::wstring_view DefaultHomePage{ L"https://www.google.com" };

    BrowserContent::BrowserContent(const winrt::hstring& initialUrl, const winrt::hstring& searchUrl)
    {
        _searchUrl = searchUrl;
        _root = Grid{};
        auto res = Application::Current().Resources();
        if (auto bg = res.TryLookup(winrt::box_value(L"UnfocusedBorderBrush")))
        {
            _root.Background(bg.try_as<Media::Brush>());
        }

        auto toolbarRow = RowDefinition{};
        toolbarRow.Height(GridLengthHelper::FromValueAndType(0, GridUnitType::Auto));
        auto webRow = RowDefinition{};
        webRow.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        _root.RowDefinitions().Append(toolbarRow);
        _root.RowDefinitions().Append(webRow);

        // Toolbar: [back] [forward] [reload] [url ............] [paste]
        auto toolbar = Grid{};
        Grid::SetRow(toolbar, 0);
        for (auto type : { GridUnitType::Auto, GridUnitType::Auto, GridUnitType::Auto, GridUnitType::Star, GridUnitType::Auto })
        {
            auto col = ColumnDefinition{};
            col.Width(GridLengthHelper::FromValueAndType(type == GridUnitType::Star ? 1 : 0, type));
            toolbar.ColumnDefinitions().Append(col);
        }

        auto backBtn = Button{};
        backBtn.Content(winrt::box_value(L"◀"));
        backBtn.Margin({ 4, 4, 2, 4 });
        backBtn.Click([weak = get_weak()](auto&&, auto&&) {
            if (auto self = weak.get())
            {
                try
                {
                    if (self->_web && self->_web.CanGoBack())
                    {
                        self->_web.GoBack();
                    }
                }
                catch (...)
                {
                }
            }
        });
        Grid::SetColumn(backBtn, 0);
        toolbar.Children().Append(backBtn);

        auto fwdBtn = Button{};
        fwdBtn.Content(winrt::box_value(L"▶"));
        fwdBtn.Margin({ 2, 4, 2, 4 });
        fwdBtn.Click([weak = get_weak()](auto&&, auto&&) {
            if (auto self = weak.get())
            {
                try
                {
                    if (self->_web && self->_web.CanGoForward())
                    {
                        self->_web.GoForward();
                    }
                }
                catch (...)
                {
                }
            }
        });
        Grid::SetColumn(fwdBtn, 1);
        toolbar.Children().Append(fwdBtn);

        auto reloadBtn = Button{};
        reloadBtn.Content(winrt::box_value(L"⟳"));
        reloadBtn.Margin({ 2, 4, 2, 4 });
        reloadBtn.Click([weak = get_weak()](auto&&, auto&&) {
            if (auto self = weak.get())
            {
                try
                {
                    if (self->_web)
                    {
                        self->_web.Reload();
                    }
                }
                catch (...)
                {
                }
            }
        });
        Grid::SetColumn(reloadBtn, 2);
        toolbar.Children().Append(reloadBtn);

        _urlBox = TextBox{};
        _urlBox.Margin({ 2, 4, 2, 4 });
        _urlBox.PlaceholderText(L"Search or enter address");
        _urlBox.KeyDown([weak = get_weak()](auto&&, winrt::Windows::UI::Xaml::Input::KeyRoutedEventArgs const& e) {
            if (e.Key() == winrt::Windows::System::VirtualKey::Enter)
            {
                if (auto self = weak.get())
                {
                    self->_Navigate(self->_urlBox.Text());
                    e.Handled(true);
                }
            }
        });
        Grid::SetColumn(_urlBox, 3);
        toolbar.Children().Append(_urlBox);

        // Paste the clipboard text into the address bar.
        auto pasteBtn = Button{};
        pasteBtn.Content(winrt::box_value(L"📋"));
        pasteBtn.Margin({ 2, 4, 4, 4 });
        pasteBtn.Click([weak = get_weak()](auto&&, auto&&) -> winrt::fire_and_forget {
            auto self = weak.get();
            if (!self)
            {
                co_return;
            }
            auto dispatcher = winrt::Windows::System::DispatcherQueue::GetForCurrentThread();
            winrt::hstring text;
            try
            {
                auto content = winrt::Windows::ApplicationModel::DataTransfer::Clipboard::GetContent();
                if (content && content.Contains(winrt::Windows::ApplicationModel::DataTransfer::StandardDataFormats::Text()))
                {
                    text = co_await content.GetTextAsync();
                }
            }
            catch (...)
            {
            }
            co_await wil::resume_foreground(dispatcher);
            if (!text.empty())
            {
                self->_urlBox.Text(text);
                self->_urlBox.Focus(winrt::Windows::UI::Xaml::FocusState::Programmatic);
                self->_urlBox.SelectionStart(static_cast<int32_t>(text.size()));
            }
        });
        Grid::SetColumn(pasteBtn, 4);
        toolbar.Children().Append(pasteBtn);

        _root.Children().Append(toolbar);

        _web = winrt::Microsoft::UI::Xaml::Controls::WebView2{};
        Grid::SetRow(_web, 1);
        _root.Children().Append(_web);

        _pendingUrl = initialUrl.empty() ? winrt::hstring{ DefaultHomePage } : initialUrl;

        // The CoreWebView2 must be created once the control is in the tree.
        // Initialize it explicitly on Loaded, then navigate. Guard so reparenting
        // the pane doesn't reload the initial page.
        _web.Loaded([weak = get_weak()](auto&&, auto&&) -> winrt::fire_and_forget {
            auto self = weak.get();
            if (!self || self->_initialized)
            {
                co_return;
            }
            self->_initialized = true;
            try
            {
                // Point WebView2 at an explicit, writable user-data folder. The
                // default folder resolution can fail for an unpackaged/portable
                // app (which has a manifest identity but no registered package),
                // leaving the view blank. WebView2 honors this env var for the
                // user-data folder of its default environment.
                wchar_t lad[MAX_PATH]{};
                const auto n = GetEnvironmentVariableW(L"LOCALAPPDATA", lad, MAX_PATH);
                std::wstring udf = (n > 0 && n < MAX_PATH ? std::wstring{ lad } : std::wstring{ L"." });
                udf += L"\\WindowsTerminalFork.WebView2";
                SetEnvironmentVariableW(L"WEBVIEW2_USER_DATA_FOLDER", udf.c_str());

                co_await self->_web.EnsureCoreWebView2Async();

                // Remove the variable from the process environment so it doesn't
                // leak into shells spawned by terminal panes afterwards. WebView2
                // has already read it during initialization above.
                SetEnvironmentVariableW(L"WEBVIEW2_USER_DATA_FOLDER", nullptr);

                self->_Navigate(self->_pendingUrl);
            }
            catch (winrt::hresult_error const& ex)
            {
                SetEnvironmentVariableW(L"WEBVIEW2_USER_DATA_FOLDER", nullptr);
                self->_urlBox.Text(winrt::hstring{ L"WebView2 init failed: " } + ex.message());
            }
            catch (...)
            {
                SetEnvironmentVariableW(L"WEBVIEW2_USER_DATA_FOLDER", nullptr);
                self->_urlBox.Text(L"WebView2 init failed (unknown error)");
            }
        });

        _web.NavigationCompleted([weak = get_weak()](auto&&, auto&&) {
            auto self = weak.get();
            if (!self)
            {
                return;
            }
            try
            {
                if (auto core = self->_web.CoreWebView2())
                {
                    auto t = core.DocumentTitle();
                    self->_title = t.empty() ? winrt::hstring{ L"Browser" } : t;
                    self->TitleChanged.raise(*self, nullptr);
                    self->_urlBox.Text(core.Source());
                }
            }
            catch (...)
            {
            }
        });
    }

    void BrowserContent::_Navigate(const winrt::hstring& text)
    {
        std::wstring s{ text.c_str() };
        while (!s.empty() && iswspace(s.front()))
        {
            s.erase(s.begin());
        }
        while (!s.empty() && iswspace(s.back()))
        {
            s.pop_back();
        }
        if (s.empty())
        {
            return;
        }

        // Turn a non-URL token into a web search using the configured
        // searchWebDefaultQueryUrl template (which uses %s for the query).
        const auto searchFor = [&](const std::wstring& query) -> winrt::hstring {
            const auto q = Uri::EscapeComponent(winrt::hstring{ query });
            std::wstring tmpl{ _searchUrl.c_str() };
            if (tmpl.empty())
            {
                tmpl = L"https://www.google.com/search?q=%s";
            }
            const auto pos = tmpl.find(L"%s");
            if (pos != std::wstring::npos)
            {
                tmpl.replace(pos, 2, std::wstring{ q.c_str() });
            }
            else
            {
                tmpl += std::wstring{ q.c_str() };
            }
            return winrt::hstring{ tmpl };
        };

        winrt::hstring url;
        const auto schemePos = s.find(L"://");
        if (schemePos != std::wstring::npos)
        {
            // Only navigate a small allow-list of schemes; route anything else
            // (javascript:, data:, vbscript:, ...) to search instead.
            std::wstring scheme = s.substr(0, schemePos);
            std::transform(scheme.begin(), scheme.end(), scheme.begin(), ::towlower);
            if (scheme == L"http" || scheme == L"https" || scheme == L"file" || scheme == L"ftp" || scheme == L"about")
            {
                url = winrt::hstring{ s };
            }
            else
            {
                url = searchFor(s);
            }
        }
        else
        {
            const bool hasSpace = s.find(L' ') != std::wstring::npos;
            const bool hasDot = s.find(L'.') != std::wstring::npos;
            const bool isLocalhost = (s == L"localhost") || s.rfind(L"localhost:", 0) == 0 || s.rfind(L"localhost/", 0) == 0;
            bool isHostPort = false;
            if (!hasSpace)
            {
                const auto colon = s.find(L':');
                if (colon != std::wstring::npos && colon + 1 < s.size() && iswdigit(s[colon + 1]))
                {
                    isHostPort = true;
                }
            }
            if (!hasSpace && (hasDot || isLocalhost || isHostPort))
            {
                // A dotted public host defaults to https; a bare host / localhost
                // / host:port (typical for dev servers) defaults to http.
                const wchar_t* prefix = (hasDot && !isLocalhost) ? L"https://" : L"http://";
                url = winrt::hstring{ prefix + s };
            }
            else
            {
                url = searchFor(s);
            }
        }

        try
        {
            _web.Source(Uri{ url });
            _urlBox.Text(url);
        }
        catch (...)
        {
        }
    }

    void BrowserContent::UpdateSettings(const CascadiaSettings& /*settings*/)
    {
    }

    winrt::Windows::UI::Xaml::FrameworkElement BrowserContent::GetRoot()
    {
        return _root;
    }

    winrt::Windows::Foundation::Size BrowserContent::MinimumSize()
    {
        return { 1, 1 };
    }

    void BrowserContent::Focus(winrt::Windows::UI::Xaml::FocusState reason)
    {
        if (_urlBox)
        {
            _urlBox.Focus(reason);
        }
    }

    void BrowserContent::Close()
    {
        try
        {
            if (_web)
            {
                _web.Close();
            }
        }
        catch (...)
        {
        }
    }

    INewContentArgs BrowserContent::GetNewTerminalArgs(const BuildStartupKind /* kind */) const
    {
        // NOTE: only the content type is round-tripped. Moving this pane to a new
        // window or restoring it does not preserve the current URL; it reopens at
        // the configured home page. Persisting the URL would require a dedicated
        // args type.
        return BaseContentArgs(L"browser");
    }

    winrt::hstring BrowserContent::Icon() const
    {
        static constexpr std::wstring_view glyph{ L"\xE774" }; // Globe
        return winrt::hstring{ glyph };
    }

    winrt::Windows::UI::Xaml::Media::Brush BrowserContent::BackgroundBrush()
    {
        return _root.Background();
    }
}

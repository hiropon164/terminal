// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "WingetContent.h"

#include <winrt/Windows.UI.Xaml.Controls.h>
#include <wil/cppwinrt_helpers.h>

#include <string>
#include <vector>

using namespace winrt::Windows::UI::Xaml;
using namespace winrt::Windows::UI::Xaml::Controls;
using namespace winrt::Microsoft::Terminal::Settings::Model;

namespace winrt::TerminalApp::implementation
{
    WingetContent::WingetContent()
    {
        _root = Grid{};
        auto res = Application::Current().Resources();
        if (auto bg = res.TryLookup(winrt::box_value(L"UnfocusedBorderBrush")))
        {
            _root.Background(bg.try_as<Media::Brush>());
        }

        // Rows: [toolbar][results list*][action bar][output log*]
        for (auto h : { GridUnitType::Auto, GridUnitType::Star, GridUnitType::Auto, GridUnitType::Star })
        {
            auto row = RowDefinition{};
            row.Height(GridLengthHelper::FromValueAndType(h == GridUnitType::Star ? 1 : 0, h));
            _root.RowDefinitions().Append(row);
        }
        // Give the list more room than the log.
        _root.RowDefinitions().GetAt(1).Height(GridLengthHelper::FromValueAndType(3, GridUnitType::Star));
        _root.RowDefinitions().GetAt(3).Height(GridLengthHelper::FromValueAndType(2, GridUnitType::Star));

        // --- Toolbar: [search box ........] [Search] [Installed] [Upgradable] ---
        auto toolbar = Grid{};
        Grid::SetRow(toolbar, 0);
        for (auto t : { GridUnitType::Star, GridUnitType::Auto, GridUnitType::Auto, GridUnitType::Auto })
        {
            auto c = ColumnDefinition{};
            c.Width(GridLengthHelper::FromValueAndType(t == GridUnitType::Star ? 1 : 0, t));
            toolbar.ColumnDefinitions().Append(c);
        }

        _search = TextBox{};
        _search.Margin({ 4, 4, 2, 4 });
        _search.PlaceholderText(L"Search packages (name or id), Enter to search");
        _search.KeyDown([weak = get_weak()](auto&&, winrt::Windows::UI::Xaml::Input::KeyRoutedEventArgs const& e) {
            if (e.Key() == winrt::Windows::System::VirtualKey::Enter)
            {
                if (auto self = weak.get())
                {
                    self->_Search();
                    e.Handled(true);
                }
            }
        });
        Grid::SetColumn(_search, 0);
        toolbar.Children().Append(_search);

        const auto makeButton = [](const wchar_t* text, int col) {
            auto b = Button{};
            b.Content(winrt::box_value(winrt::hstring{ text }));
            b.Margin({ 2, 4, 2, 4 });
            Grid::SetColumn(b, col);
            return b;
        };

        auto searchBtn = makeButton(L"Search", 1);
        searchBtn.Click([weak = get_weak()](auto&&, auto&&) { if (auto self = weak.get()) self->_Search(); });
        toolbar.Children().Append(searchBtn);

        auto installedBtn = makeButton(L"Installed", 2);
        installedBtn.Click([weak = get_weak()](auto&&, auto&&) { if (auto self = weak.get()) self->_ShowInstalled(); });
        toolbar.Children().Append(installedBtn);

        auto upgradableBtn = makeButton(L"Upgradable", 3);
        upgradableBtn.Click([weak = get_weak()](auto&&, auto&&) { if (auto self = weak.get()) self->_ShowUpgradable(); });
        toolbar.Children().Append(upgradableBtn);

        _root.Children().Append(toolbar);

        // --- Results list ---
        _list = ListView{};
        _list.Margin({ 4, 0, 4, 4 });
        _list.SelectionMode(ListViewSelectionMode::Single);
        _list.FontFamily(Media::FontFamily{ L"Cascadia Mono, Consolas" });
        Grid::SetRow(_list, 1);
        _root.Children().Append(_list);

        // --- Action bar: [Install] [Upgrade] [Uninstall]   <status> ---
        auto actions = Grid{};
        Grid::SetRow(actions, 2);
        for (auto t : { GridUnitType::Auto, GridUnitType::Auto, GridUnitType::Auto, GridUnitType::Star })
        {
            auto c = ColumnDefinition{};
            c.Width(GridLengthHelper::FromValueAndType(t == GridUnitType::Star ? 1 : 0, t));
            actions.ColumnDefinitions().Append(c);
        }

        auto installBtn = makeButton(L"Install", 0);
        installBtn.Click([weak = get_weak()](auto&&, auto&&) { if (auto self = weak.get()) self->_ActionOnSelected(L"install"); });
        actions.Children().Append(installBtn);

        auto upgradeBtn = makeButton(L"Upgrade", 1);
        upgradeBtn.Click([weak = get_weak()](auto&&, auto&&) { if (auto self = weak.get()) self->_ActionOnSelected(L"upgrade"); });
        actions.Children().Append(upgradeBtn);

        auto uninstallBtn = makeButton(L"Uninstall", 2);
        uninstallBtn.Click([weak = get_weak()](auto&&, auto&&) { if (auto self = weak.get()) self->_ActionOnSelected(L"uninstall"); });
        actions.Children().Append(uninstallBtn);

        _status = TextBlock{};
        _status.Margin({ 8, 4, 4, 4 });
        _status.VerticalAlignment(VerticalAlignment::Center);
        _status.Text(L"Ready");
        Grid::SetColumn(_status, 3);
        actions.Children().Append(_status);

        _root.Children().Append(actions);

        // --- Output log ---
        _log = TextBox{};
        _log.Margin({ 4, 0, 4, 4 });
        _log.IsReadOnly(true);
        _log.AcceptsReturn(true);
        _log.TextWrapping(TextWrapping::NoWrap);
        _log.FontFamily(Media::FontFamily{ L"Cascadia Mono, Consolas" });
        _log.IsSpellCheckEnabled(false);
        ScrollViewer::SetVerticalScrollBarVisibility(_log, ScrollBarVisibility::Auto);
        ScrollViewer::SetHorizontalScrollBarVisibility(_log, ScrollBarVisibility::Auto);
        Grid::SetRow(_log, 3);
        _root.Children().Append(_log);

        _search.Loaded([weak = get_weak()](auto&&, auto&&) {
            if (auto self = weak.get())
            {
                if (!self->_didInitialLoad)
                {
                    self->_didInitialLoad = true;
                    self->_search.Focus(winrt::Windows::UI::Xaml::FocusState::Programmatic);
                    // Show installed packages right away so the pane is useful on open.
                    self->_ShowInstalled();
                }
            }
        });
    }

    void WingetContent::_Search()
    {
        std::wstring q{ _search.Text().c_str() };
        // trim
        while (!q.empty() && iswspace(q.front()))
        {
            q.erase(q.begin());
        }
        while (!q.empty() && iswspace(q.back()))
        {
            q.pop_back();
        }
        if (q.empty())
        {
            return;
        }
        _RunWinget({ L"search", q, L"--disable-interactivity" }, true);
    }

    void WingetContent::_ShowInstalled()
    {
        _RunWinget({ L"list", L"--disable-interactivity" }, true);
    }

    void WingetContent::_ShowUpgradable()
    {
        _RunWinget({ L"upgrade", L"--disable-interactivity" }, true);
    }

    void WingetContent::_ActionOnSelected(std::wstring verb)
    {
        const auto idx = _list.SelectedIndex();
        if (idx < 0 || static_cast<size_t>(idx) >= _entries.size())
        {
            _SetBusy(false, L"Select a package first");
            return;
        }
        const auto id = _entries[idx].id;
        std::vector<std::wstring> args{ verb, L"--id", id, L"-e", L"--disable-interactivity" };
        if (verb == L"install" || verb == L"upgrade")
        {
            args.push_back(L"--accept-package-agreements");
            args.push_back(L"--accept-source-agreements");
        }
        _RunWinget(std::move(args), false);
    }

    void WingetContent::_SetBusy(bool busy, const winrt::hstring& status)
    {
        _busy = busy;
        if (_status)
        {
            _status.Text(status);
        }
    }

    winrt::fire_and_forget WingetContent::_RunWinget(std::vector<std::wstring> args, bool parseList)
    {
        if (_busy)
        {
            co_return;
        }
        auto self = get_strong();

        auto dispatcher = winrt::Windows::System::DispatcherQueue::GetForCurrentThread();
        if (!dispatcher)
        {
            co_return;
        }

        // Everything below is guarded: this is a fire_and_forget coroutine, and an
        // exception escaping it would call winrt::terminate() and crash the process.
        std::string rawBytes;
        DWORD exitCode = 0;
        bool launched = false;
        bool failed = false;
        try
        {
            _SetBusy(true, L"Running...");
            _log.Text(L"");
            if (parseList)
            {
                _list.Items().Clear();
                _entries.clear();
            }

            // Build the command line (winget.exe + quoted args).
            std::wstring cmd = L"winget.exe";
            for (const auto& a : args)
            {
                cmd += L' ';
                if (a.find(L' ') != std::wstring::npos)
                {
                    cmd += L'"';
                    cmd += a;
                    cmd += L'"';
                }
                else
                {
                    cmd += a;
                }
            }

            // Run off the UI thread; winget can take a while.
            co_await winrt::resume_background();

            SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
            HANDLE hRead{ nullptr };
            HANDLE hWrite{ nullptr };
            if (CreatePipe(&hRead, &hWrite, &sa, 0))
            {
                // The child must not inherit the read end.
                SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

                STARTUPINFOW si{};
                si.cb = sizeof(si);
                si.dwFlags = STARTF_USESTDHANDLES;
                si.hStdOutput = hWrite;
                si.hStdError = hWrite;
                si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

                PROCESS_INFORMATION pi{};
                std::wstring mutableCmd = cmd; // CreateProcessW may modify the buffer
                if (CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
                {
                    launched = true;
                    // Parent closes its copy of the write end so ReadFile sees EOF
                    // when the child exits.
                    CloseHandle(hWrite);
                    hWrite = nullptr;

                    char buf[4096];
                    DWORD read = 0;
                    while (ReadFile(hRead, buf, sizeof(buf), &read, nullptr) && read > 0)
                    {
                        rawBytes.append(buf, read);
                    }

                    WaitForSingleObject(pi.hProcess, INFINITE);
                    GetExitCodeProcess(pi.hProcess, &exitCode);
                    CloseHandle(pi.hProcess);
                    CloseHandle(pi.hThread);
                }
                if (hWrite)
                {
                    CloseHandle(hWrite);
                }
                CloseHandle(hRead);
            }

            // Decode winget's UTF-8 output and strip ANSI/control noise.
            std::wstring text;
            if (!rawBytes.empty())
            {
                const int n = MultiByteToWideChar(CP_UTF8, 0, rawBytes.data(), static_cast<int>(rawBytes.size()), nullptr, 0);
                if (n > 0)
                {
                    text.resize(static_cast<size_t>(n));
                    MultiByteToWideChar(CP_UTF8, 0, rawBytes.data(), static_cast<int>(rawBytes.size()), text.data(), n);
                }
            }
            _StripAnsi(text);

            co_await wil::resume_foreground(dispatcher);

            // The pane may have been closed while winget was running; its XAML
            // elements are no longer safe to touch.
            if (_closed)
            {
                _busy = false;
                co_return;
            }

            _log.Text(winrt::hstring{ text });
            if (parseList)
            {
                _ParseTable(text);
            }
        }
        catch (...)
        {
            failed = true;
        }

        // Restore state on the UI thread no matter what happened above.
        try
        {
            co_await wil::resume_foreground(dispatcher);
        }
        catch (...)
        {
        }

        if (_closed)
        {
            _busy = false;
            co_return;
        }

        winrt::hstring status;
        if (failed)
        {
            status = L"Error while running winget";
        }
        else if (!launched)
        {
            status = L"Could not launch winget (is it installed?)";
        }
        else if (parseList)
        {
            status = winrt::hstring{ L"Done - " + std::to_wstring(_entries.size()) + L" result(s)" };
        }
        else
        {
            status = winrt::hstring{ L"Done (exit " + std::to_wstring(exitCode) + L")" };
        }
        _busy = false;
        try
        {
            if (_status)
            {
                _status.Text(status);
            }
        }
        catch (...)
        {
        }
    }

    void WingetContent::_StripAnsi(std::wstring& s)
    {
        std::wstring out;
        out.reserve(s.size());
        size_t lineStart = 0; // index in `out` where the current line begins
        for (size_t i = 0; i < s.size(); ++i)
        {
            const wchar_t c = s[i];
            if (c == 0x1b) // ESC - skip a CSI sequence (ESC [ ... final-byte)
            {
                ++i;
                if (i < s.size() && s[i] == L'[')
                {
                    ++i;
                    while (i < s.size() && !(s[i] >= L'@' && s[i] <= L'~'))
                    {
                        ++i;
                    }
                }
                continue;
            }
            if (c == L'\r')
            {
                if (i + 1 < s.size() && s[i + 1] == L'\n')
                {
                    // CRLF line ending - drop just the CR, keep the line.
                    continue;
                }
                // A bare carriage return: winget overwrites the current line for
                // progress animation. Reset the line so only its final content
                // survives (otherwise spinner frames merge into the table header).
                out.erase(lineStart);
                continue;
            }
            out.push_back(c);
            if (c == L'\n')
            {
                lineStart = out.size();
            }
        }
        s.swap(out);
    }

    int WingetContent::_DisplayWidth(wchar_t c)
    {
        // winget aligns columns by display width, so East-Asian wide/fullwidth
        // characters (common in localized output) count as 2 cells.
        if ((c >= 0x1100 && c <= 0x115F) ||
            (c >= 0x2E80 && c <= 0x303E) ||
            (c >= 0x3041 && c <= 0x33FF) ||
            (c >= 0x3400 && c <= 0x4DBF) ||
            (c >= 0x4E00 && c <= 0x9FFF) ||
            (c >= 0xA000 && c <= 0xA4CF) ||
            (c >= 0xAC00 && c <= 0xD7A3) ||
            (c >= 0xF900 && c <= 0xFAFF) ||
            (c >= 0xFE30 && c <= 0xFE4F) ||
            (c >= 0xFF00 && c <= 0xFF60) ||
            (c >= 0xFFE0 && c <= 0xFFE6))
        {
            return 2;
        }
        return 1;
    }

    void WingetContent::_ParseTable(const std::wstring& text)
    {
        // Split into lines.
        std::vector<std::wstring> lines;
        {
            std::wstring cur;
            for (const wchar_t c : text)
            {
                if (c == L'\n')
                {
                    lines.push_back(cur);
                    cur.clear();
                }
                else
                {
                    cur.push_back(c);
                }
            }
            if (!cur.empty())
            {
                lines.push_back(cur);
            }
        }

        // Find the run-of-dashes line that separates the header from the rows.
        int dashIdx = -1;
        for (size_t i = 0; i < lines.size(); ++i)
        {
            const auto& l = lines[i];
            const auto a = l.find_first_not_of(L' ');
            const auto b = l.find_last_not_of(L' ');
            if (a == std::wstring::npos)
            {
                continue;
            }
            bool allDash = true;
            for (size_t k = a; k <= b; ++k)
            {
                if (l[k] != L'-')
                {
                    allDash = false;
                    break;
                }
            }
            if (allDash && (b - a) >= 3)
            {
                dashIdx = static_cast<int>(i);
                break;
            }
        }
        if (dashIdx <= 0)
        {
            return; // no header row before the separator -> nothing to parse
        }

        // Column start offsets, measured in display columns (each header is a
        // single word). Using display width keeps the offsets correct even when
        // the header is localized (e.g. Japanese, where characters are 2 cells).
        const std::wstring& header = lines[static_cast<size_t>(dashIdx) - 1];
        std::vector<size_t> offs;
        {
            size_t disp = 0;
            bool prevSpace = true;
            for (const wchar_t c : header)
            {
                if (c != L' ')
                {
                    if (prevSpace)
                    {
                        offs.push_back(disp);
                    }
                    prevSpace = false;
                }
                else
                {
                    prevSpace = true;
                }
                disp += _DisplayWidth(c);
            }
        }
        if (offs.size() < 2)
        {
            return;
        }

        const auto slice = [&](const std::wstring& d, size_t k) -> std::wstring {
            if (k >= offs.size())
            {
                return L"";
            }
            const size_t startDisp = offs[k];
            const size_t endDisp = (k + 1 < offs.size()) ? offs[k + 1] : SIZE_MAX;
            std::wstring v;
            size_t disp = 0;
            for (const wchar_t c : d)
            {
                if (disp >= endDisp)
                {
                    break;
                }
                if (disp >= startDisp)
                {
                    v.push_back(c);
                }
                disp += _DisplayWidth(c);
            }
            const auto a = v.find_first_not_of(L' ');
            const auto b = v.find_last_not_of(L' ');
            if (a == std::wstring::npos)
            {
                return L"";
            }
            return v.substr(a, b - a + 1);
        };

        for (size_t i = static_cast<size_t>(dashIdx) + 1; i < lines.size() && _entries.size() < 1000; ++i)
        {
            const auto& d = lines[i];
            if (d.find_first_not_of(L' ') == std::wstring::npos)
            {
                continue;
            }
            Entry e;
            e.name = slice(d, 0);
            e.id = slice(d, 1);
            e.version = slice(d, 2);
            // Skip trailing summary lines that aren't real package rows.
            if (e.id.empty() || e.id.find(L' ') != std::wstring::npos)
            {
                continue;
            }
            _entries.push_back(e);
            std::wstring disp = e.name + L"   [" + e.id + L"]";
            if (!e.version.empty())
            {
                disp += L"   " + e.version;
            }
            _list.Items().Append(winrt::box_value(winrt::hstring{ disp }));
        }
        if (!_entries.empty())
        {
            _list.SelectedIndex(0);
        }
    }

    void WingetContent::UpdateSettings(const CascadiaSettings& /*settings*/)
    {
    }

    winrt::Windows::UI::Xaml::FrameworkElement WingetContent::GetRoot()
    {
        return _root;
    }

    winrt::Windows::Foundation::Size WingetContent::MinimumSize()
    {
        return { 1, 1 };
    }

    void WingetContent::Focus(winrt::Windows::UI::Xaml::FocusState reason)
    {
        if (_search)
        {
            _search.Focus(reason);
        }
    }

    void WingetContent::Close()
    {
        // Once the pane is torn down its XAML elements may be disconnected. Any
        // in-flight winget coroutine must stop before touching them again.
        _closed = true;
    }

    INewContentArgs WingetContent::GetNewTerminalArgs(const BuildStartupKind /* kind */) const
    {
        return BaseContentArgs(L"winget");
    }

    winrt::hstring WingetContent::Icon() const
    {
        static constexpr std::wstring_view glyph{ L"\xE7B8" }; // Package
        return winrt::hstring{ glyph };
    }

    winrt::Windows::UI::Xaml::Media::Brush WingetContent::BackgroundBrush()
    {
        return _root.Background();
    }
}

// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once
#include "winrt/TerminalApp.h"
#include "BasicPaneEvents.h"
#include <winrt/Microsoft.UI.Xaml.Controls.h>

namespace winrt::TerminalApp::implementation
{
    // A WebView2 (Chromium/Edge) browser hosted as a pane. Code-only
    // IPaneContent, modelled on ScratchpadContent/FileBrowserContent.
    class BrowserContent : public winrt::implements<BrowserContent, IPaneContent>, public BasicPaneEvents
    {
    public:
        BrowserContent(const winrt::hstring& initialUrl, const winrt::hstring& searchUrl);

        winrt::Windows::UI::Xaml::FrameworkElement GetRoot();

        void UpdateSettings(const winrt::Microsoft::Terminal::Settings::Model::CascadiaSettings& settings);

        winrt::Windows::Foundation::Size MinimumSize();

        void Focus(winrt::Windows::UI::Xaml::FocusState reason = winrt::Windows::UI::Xaml::FocusState::Programmatic);
        void Close();
        winrt::Microsoft::Terminal::Settings::Model::INewContentArgs GetNewTerminalArgs(BuildStartupKind kind) const;

        winrt::hstring Title() { return _title; }
        uint64_t TaskbarState() { return 0; }
        uint64_t TaskbarProgress() { return 0; }
        bool ReadOnly() { return false; }
        winrt::hstring Icon() const;
        winrt::Windows::Foundation::IReference<winrt::Windows::UI::Color> TabColor() const noexcept { return nullptr; }
        winrt::Windows::UI::Xaml::Media::Brush BackgroundBrush();

        // See BasicPaneEvents for most generic event definitions

    private:
        winrt::Windows::UI::Xaml::Controls::Grid _root{ nullptr };
        winrt::Windows::UI::Xaml::Controls::TextBox _urlBox{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::WebView2 _web{ nullptr };
        winrt::hstring _title{ L"Browser" };
        winrt::hstring _pendingUrl;
        winrt::hstring _searchUrl;
        bool _initialized{ false };

        void _Navigate(const winrt::hstring& text);
    };
}

// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once
#include "winrt/TerminalApp.h"
#include "BasicPaneEvents.h"
#include <filesystem>

namespace winrt::TerminalApp::implementation
{
    // A simple keyboard-driven file browser + text viewer hosted as a pane. It is
    // a code-only IPaneContent (no XAML/IDL), modelled on ScratchpadContent.
    class FileBrowserContent : public winrt::implements<FileBrowserContent, IPaneContent>, public BasicPaneEvents
    {
    public:
        explicit FileBrowserContent(const winrt::hstring& startingDirectory);

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

        // Show the drive list from anywhere (bound to an action).
        void ShowDrives();

        // See BasicPaneEvents for most generic event definitions

    private:
        winrt::Windows::UI::Xaml::Controls::Grid _root{ nullptr };
        winrt::Windows::UI::Xaml::Controls::TextBlock _pathText{ nullptr };
        winrt::Windows::UI::Xaml::Controls::ListView _list{ nullptr };
        winrt::Windows::UI::Xaml::Controls::TextBox _preview{ nullptr };
        winrt::Windows::UI::Xaml::Controls::Image _image{ nullptr };

        std::filesystem::path _cwd;
        std::vector<std::filesystem::path> _entries;
        winrt::hstring _title{ L"Files" };
        bool _showingDrives{ false };
        bool _initialFocusDone{ false };
        // Bumped on every preview request so a slow async image load can detect it
        // is stale and not clobber a newer selection.
        uint64_t _previewGeneration{ 0 };

        void _Populate();
        void _Navigate(const std::filesystem::path& dir);
        void _OpenSelected();
        void _GoUp();
        void _LoadPreview(const std::filesystem::path& file);
        winrt::fire_and_forget _LoadImage(std::filesystem::path file, uint64_t generation);
        void _UpdateTitle();

        void _OnKeyDown(const winrt::Windows::Foundation::IInspectable& sender,
                        const winrt::Windows::UI::Xaml::Input::KeyRoutedEventArgs& e);
    };
}

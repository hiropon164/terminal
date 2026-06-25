// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once
#include "winrt/TerminalApp.h"
#include "BasicPaneEvents.h"
#include <string>
#include <vector>
#include <set>

namespace winrt::TerminalApp::implementation
{
    // A small GUI front-end for the Windows Package Manager (winget), hosted as a
    // code-only IPaneContent (no XAML/IDL), modelled on FileBrowserContent. It runs
    // winget.exe as a subprocess and shows search/installed/upgradable results in a
    // list with Install/Upgrade/Uninstall buttons. Only one of these panes is meant
    // to exist at a time (enforced by the open-action handler).
    class WingetContent : public winrt::implements<WingetContent, IPaneContent>, public BasicPaneEvents
    {
    public:
        WingetContent();

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
        struct Entry
        {
            std::wstring name;
            std::wstring id;
            std::wstring version;
        };

        enum class Mode
        {
            Search,
            Installed,
            Upgradable,
        };

        winrt::Windows::UI::Xaml::Controls::Grid _root{ nullptr };
        winrt::Windows::UI::Xaml::Controls::TextBox _search{ nullptr };
        winrt::Windows::UI::Xaml::Controls::ListView _list{ nullptr };
        winrt::Windows::UI::Xaml::Controls::TextBox _log{ nullptr };
        winrt::Windows::UI::Xaml::Controls::TextBlock _status{ nullptr };
        winrt::Windows::UI::Xaml::Controls::Button _installBtn{ nullptr };
        winrt::Windows::UI::Xaml::Controls::Button _upgradeBtn{ nullptr };
        winrt::Windows::UI::Xaml::Controls::Button _upgradeAllBtn{ nullptr };
        winrt::Windows::UI::Xaml::Controls::Button _uninstallBtn{ nullptr };
        Mode _mode{ Mode::Installed };

        std::vector<Entry> _entries;
        // Lower-cased ids of installed packages, refreshed whenever the installed
        // list loads. Used to flag/split already-installed search results.
        std::set<std::wstring> _installedIds;
        winrt::hstring _title{ L"winget" };
        bool _busy{ false };
        bool _didInitialLoad{ false };
        bool _closed{ false };

        void _Search();
        void _ShowInstalled();
        void _ShowUpgradable();
        void _UpgradeAll();
        void _ActionOnSelected(std::wstring verb);
        void _UpdateButtonStates();
        winrt::fire_and_forget _RunWinget(std::vector<std::wstring> args, bool parseList);
        std::vector<Entry> _ParseTable(const std::wstring& text);
        void _PopulateFlat(const std::vector<Entry>& items);
        void _PopulateSearch(const std::vector<Entry>& items);
        void _SetBusy(bool busy, const winrt::hstring& status);
        static void _StripAnsi(std::wstring& s);
        static int _DisplayWidth(wchar_t c);
    };
}

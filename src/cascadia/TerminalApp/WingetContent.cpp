// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "WingetContent.h"

#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Interop.h>
#include <wil/cppwinrt_helpers.h>

#include <string>
#include <vector>
#include <algorithm>
#include <cwctype>

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
        // Tighten the rows: the default ListViewItem reserves ~40px min height plus
        // generous padding, which leaves the single-line entries looking sparse.
        {
            Style itemStyle{ winrt::xaml_typename<ListViewItem>() };
            itemStyle.Setters().Append(Setter{ FrameworkElement::MinHeightProperty(), winrt::box_value(0.0) });
            itemStyle.Setters().Append(Setter{ Control::PaddingProperty(), winrt::box_value(Thickness{ 8, 1, 8, 1 }) });
            _list.ItemContainerStyle(itemStyle);
        }
        // Make the selected row clearly stand out by inverting it: the normal rows
        // are light text on a dark background, so the selected row is dark text on
        // a light background.
        {
            const auto selBg = Media::SolidColorBrush{ winrt::Windows::UI::ColorHelper::FromArgb(0xFF, 0xFF, 0xFF, 0xFF) };
            const auto selFg = Media::SolidColorBrush{ winrt::Windows::UI::ColorHelper::FromArgb(0xFF, 0x00, 0x00, 0x00) };
            auto res = _list.Resources();
            for (const auto key : { L"ListViewItemBackgroundSelected", L"ListViewItemBackgroundSelectedPointerOver", L"ListViewItemBackgroundSelectedPressed" })
            {
                res.Insert(winrt::box_value(winrt::hstring{ key }), selBg);
            }
            for (const auto key : { L"ListViewItemForegroundSelected", L"ListViewItemForegroundSelectedPointerOver", L"ListViewItemForegroundSelectedPressed" })
            {
                res.Insert(winrt::box_value(winrt::hstring{ key }), selFg);
            }
        }
        Grid::SetRow(_list, 1);
        _root.Children().Append(_list);

        // --- Action bar: [Install] [Upgrade] [Upgrade All] [Uninstall]  <status> ---
        auto actions = Grid{};
        Grid::SetRow(actions, 2);
        for (auto t : { GridUnitType::Auto, GridUnitType::Auto, GridUnitType::Auto, GridUnitType::Auto, GridUnitType::Star })
        {
            auto c = ColumnDefinition{};
            c.Width(GridLengthHelper::FromValueAndType(t == GridUnitType::Star ? 1 : 0, t));
            actions.ColumnDefinitions().Append(c);
        }

        _installBtn = makeButton(L"Install", 0);
        _installBtn.Click([weak = get_weak()](auto&&, auto&&) { if (auto self = weak.get()) self->_ActionOnSelected(L"install"); });
        actions.Children().Append(_installBtn);

        _upgradeBtn = makeButton(L"Upgrade", 1);
        _upgradeBtn.Click([weak = get_weak()](auto&&, auto&&) { if (auto self = weak.get()) self->_ActionOnSelected(L"upgrade"); });
        actions.Children().Append(_upgradeBtn);

        _upgradeAllBtn = makeButton(L"Upgrade All", 2);
        _upgradeAllBtn.Click([weak = get_weak()](auto&&, auto&&) { if (auto self = weak.get()) self->_UpgradeAll(); });
        actions.Children().Append(_upgradeAllBtn);

        _uninstallBtn = makeButton(L"Uninstall", 3);
        _uninstallBtn.Click([weak = get_weak()](auto&&, auto&&) { if (auto self = weak.get()) self->_ActionOnSelected(L"uninstall"); });
        actions.Children().Append(_uninstallBtn);

        _UpdateButtonStates();

        _status = TextBlock{};
        _status.Margin({ 8, 4, 4, 4 });
        _status.VerticalAlignment(VerticalAlignment::Center);
        _status.Text(L"Ready");
        Grid::SetColumn(_status, 4);
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
        // Ignore actions while a winget run is in flight: otherwise we'd flip the
        // mode/buttons but the new run would be dropped by _RunWinget's busy guard,
        // leaving the UI out of sync with the list still being shown.
        if (_busy)
        {
            return;
        }
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
        _mode = Mode::Search;
        _UpdateButtonStates();
        _RunWinget({ L"search", q, L"--disable-interactivity" }, true);
    }

    void WingetContent::_ShowInstalled()
    {
        if (_busy)
        {
            return;
        }
        _mode = Mode::Installed;
        _UpdateButtonStates();
        _RunWinget({ L"list", L"--disable-interactivity" }, true);
    }

    void WingetContent::_ShowUpgradable()
    {
        if (_busy)
        {
            return;
        }
        _mode = Mode::Upgradable;
        _UpdateButtonStates();
        _RunWinget({ L"upgrade", L"--disable-interactivity" }, true);
    }

    void WingetContent::_UpdateButtonStates()
    {
        // Enable only the actions that make sense for the current list:
        //   Search     -> Install
        //   Installed   -> Uninstall
        //   Upgradable  -> Upgrade, Upgrade All
        const bool search = _mode == Mode::Search;
        const bool installed = _mode == Mode::Installed;
        const bool upgradable = _mode == Mode::Upgradable;
        if (_installBtn)
        {
            _installBtn.IsEnabled(search);
        }
        if (_upgradeBtn)
        {
            _upgradeBtn.IsEnabled(upgradable);
        }
        if (_upgradeAllBtn)
        {
            _upgradeAllBtn.IsEnabled(upgradable);
        }
        if (_uninstallBtn)
        {
            _uninstallBtn.IsEnabled(installed);
        }
    }

    void WingetContent::_UpgradeAll()
    {
        if (_busy)
        {
            return;
        }
        // --include-unknown so packages whose installed version winget can't
        // determine are still upgraded.
        _RunWinget({ L"upgrade", L"--all", L"--include-unknown", L"--accept-package-agreements", L"--accept-source-agreements", L"--disable-interactivity" }, false);
    }

    void WingetContent::_ActionOnSelected(std::wstring verb)
    {
        if (_busy)
        {
            return;
        }
        const auto idx = _list.SelectedIndex();
        if (idx < 0 || static_cast<size_t>(idx) >= _entries.size())
        {
            _SetBusy(false, L"Select a package first");
            return;
        }
        const auto id = _entries[idx].id;
        // Header/installed reference rows are non-selectable, but guard anyway:
        // never act on a row without a real package id.
        if (id.empty())
        {
            _SetBusy(false, L"Select a package first");
            return;
        }
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

        // Bind the mode to THIS request: _mode can change (and a competing request
        // be dropped by the _busy guard) while we're awaiting the multi-second
        // winget run, so reading the member after the await would mis-route output.
        const Mode requestMode = _mode;

        // Everything below is guarded: this is a fire_and_forget coroutine, and an
        // exception escaping it would call winrt::terminate() and crash the process.
        std::string rawBytes;
        DWORD exitCode = 0;
        bool launched = false;
        bool failed = false;
        size_t resultCount = 0;
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
                const auto parsed = _ParseTable(text);
                resultCount = parsed.size();
                if (requestMode == Mode::Installed)
                {
                    // Refresh the installed-id set used to flag search results.
                    _installedIds.clear();
                    for (const auto& e : parsed)
                    {
                        std::wstring id = e.id;
                        std::transform(id.begin(), id.end(), id.begin(), ::towlower);
                        _installedIds.insert(id);
                    }
                }
                // Only split search results when we actually know what's installed;
                // otherwise everything would be (mis)labelled "not installed".
                if (requestMode == Mode::Search && !_installedIds.empty())
                {
                    _PopulateSearch(parsed);
                }
                else
                {
                    _PopulateFlat(parsed);
                }
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
            status = winrt::hstring{ L"Done - " + std::to_wstring(resultCount) + L" result(s)" };
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

    std::vector<WingetContent::Entry> WingetContent::_ParseTable(const std::wstring& text)
    {
        std::vector<Entry> result;

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
            return result; // no header row before the separator -> nothing to parse
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
            return result;
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

        for (size_t i = static_cast<size_t>(dashIdx) + 1; i < lines.size() && result.size() < 1000; ++i)
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
            // Skip rows whose id isn't a plausible package id. This drops winget's
            // trailing summary lines (e.g. "9 upgrades available." / "9 アップグレード
            // を利用できます。"), whose id column is empty or punctuation. Real
            // package ids are ASCII like "Publisher.Package".
            const auto isIdChar = [](wchar_t ch) {
                return (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') || (ch >= L'0' && ch <= L'9');
            };
            if (e.id.empty() || e.id.find(L' ') != std::wstring::npos ||
                std::none_of(e.id.begin(), e.id.end(), isIdChar))
            {
                continue;
            }
            result.push_back(e);
        }
        return result;
    }

    void WingetContent::_PopulateFlat(const std::vector<Entry>& items)
    {
        for (const auto& e : items)
        {
            std::wstring disp = e.name + L"   [" + e.id + L"]";
            if (!e.version.empty())
            {
                disp += L"   " + e.version;
            }
            _list.Items().Append(winrt::box_value(winrt::hstring{ disp }));
            _entries.push_back(e);
        }
        if (!_entries.empty())
        {
            _list.SelectedIndex(0);
        }
    }

    void WingetContent::_PopulateSearch(const std::vector<Entry>& items)
    {
        std::vector<Entry> notInstalled;
        std::vector<Entry> installed;
        for (const auto& e : items)
        {
            std::wstring id = e.id;
            std::transform(id.begin(), id.end(), id.begin(), ::towlower);
            if (_installedIds.count(id) != 0)
            {
                installed.push_back(e);
            }
            else
            {
                notInstalled.push_back(e);
            }
        }

        // Adds one row. Non-selectable rows (section headers and the
        // already-installed entries) are disabled so they read as reference only.
        const auto addRow = [&](const std::wstring& text, bool selectable, const Entry& entry) {
            ListViewItem item{};
            item.Content(winrt::box_value(winrt::hstring{ text }));
            item.MinHeight(0);
            item.Padding(Thickness{ 8, 1, 8, 1 });
            if (!selectable)
            {
                item.IsEnabled(false);
            }
            _list.Items().Append(item);
            _entries.push_back(entry);
        };
        const auto rowText = [](const Entry& e) {
            std::wstring disp = e.name + L"   [" + e.id + L"]";
            if (!e.version.empty())
            {
                disp += L"   " + e.version;
            }
            return disp;
        };

        // Only show the section headers when both sections are present - a lone
        // header above a single section is just noise.
        const bool showHeaders = !notInstalled.empty() && !installed.empty();

        const Entry blank;
        int firstSelectable = -1;
        if (!notInstalled.empty())
        {
            if (showHeaders)
            {
                addRow(L"── Not installed ──", false, blank);
            }
            for (const auto& e : notInstalled)
            {
                if (firstSelectable < 0)
                {
                    firstSelectable = static_cast<int>(_entries.size());
                }
                addRow(rowText(e), true, e);
            }
        }
        if (!installed.empty())
        {
            if (showHeaders)
            {
                addRow(L"── Installed (reference only) ──", false, blank);
            }
            for (const auto& e : installed)
            {
                addRow(rowText(e), false, e);
            }
        }
        if (firstSelectable >= 0)
        {
            _list.SelectedIndex(firstSelectable);
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

// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "FileBrowserContent.h"

#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <wil/cppwinrt_helpers.h>

#include <fstream>
#include <algorithm>
#include <set>
#include <vector>

using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::UI::Xaml;
using namespace winrt::Windows::UI::Xaml::Controls;
using namespace winrt::Microsoft::Terminal::Settings::Model;

namespace fs = std::filesystem;

namespace winrt::TerminalApp::implementation
{
    FileBrowserContent::FileBrowserContent(const winrt::hstring& startingDirectory)
    {
        // Resolve the starting directory: the argument, else %USERPROFILE%, else cwd.
        std::error_code ec;
        wchar_t home[MAX_PATH]{};
        const auto homeLen = GetEnvironmentVariableW(L"USERPROFILE", home, MAX_PATH);
        if (!startingDirectory.empty() && fs::is_directory(fs::path{ startingDirectory.c_str() }, ec))
        {
            _cwd = fs::path{ startingDirectory.c_str() };
        }
        else if (homeLen > 0 && homeLen < MAX_PATH && fs::is_directory(fs::path{ home }, ec))
        {
            _cwd = fs::path{ home };
        }
        else
        {
            _cwd = fs::current_path(ec);
        }

        _root = Grid{};
        auto res = Application::Current().Resources();
        if (auto bg = res.TryLookup(winrt::box_value(L"UnfocusedBorderBrush")))
        {
            _root.Background(bg.try_as<Media::Brush>());
        }

        auto headerRow = RowDefinition{};
        headerRow.Height(GridLengthHelper::FromValueAndType(0, GridUnitType::Auto));
        auto bodyRow = RowDefinition{};
        bodyRow.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        _root.RowDefinitions().Append(headerRow);
        _root.RowDefinitions().Append(bodyRow);

        // Current path header.
        _pathText = TextBlock{};
        _pathText.Margin({ 8, 6, 8, 6 });
        _pathText.TextTrimming(TextTrimming::CharacterEllipsis);
        _pathText.FontFamily(Media::FontFamily{ L"Cascadia Mono, Consolas" });
        Grid::SetRow(_pathText, 0);
        _root.Children().Append(_pathText);

        // Body: file list on the left, text preview on the right.
        auto body = Grid{};
        Grid::SetRow(body, 1);
        auto listCol = ColumnDefinition{};
        listCol.Width(GridLengthHelper::FromValueAndType(2, GridUnitType::Star));
        auto previewCol = ColumnDefinition{};
        previewCol.Width(GridLengthHelper::FromValueAndType(3, GridUnitType::Star));
        body.ColumnDefinitions().Append(listCol);
        body.ColumnDefinitions().Append(previewCol);

        _list = ListView{};
        _list.Margin({ 4, 0, 4, 4 });
        _list.SelectionMode(ListViewSelectionMode::Single);
        _list.SingleSelectionFollowsFocus(true);
        // When the pane first opens, Focus() may run before the list is in the
        // visual tree (so it silently fails and the pane has no keyboard focus).
        // Focus once the list is loaded so arrow keys / actions work immediately.
        _list.Loaded([weak = get_weak()](auto&&, auto&&) {
            if (auto self = weak.get())
            {
                // Only on first display - Loaded also fires when the pane is
                // re-parented (moved/unzoomed), and we don't want to steal focus
                // from whatever pane the user is actually using then.
                if (!self->_initialFocusDone)
                {
                    self->_initialFocusDone = true;
                    self->_list.Focus(winrt::Windows::UI::Xaml::FocusState::Programmatic);
                }
            }
        });
        // Register with handledEventsToo so we still see Enter/Backspace even if
        // the ListView marks them handled as part of its own key processing.
        _list.AddHandler(UIElement::KeyDownEvent(),
                         winrt::box_value(winrt::Windows::UI::Xaml::Input::KeyEventHandler{ get_weak(), &FileBrowserContent::_OnKeyDown }),
                         true);
        Grid::SetColumn(_list, 0);
        body.Children().Append(_list);

        _preview = TextBox{};
        _preview.Margin({ 4, 0, 4, 4 });
        _preview.IsReadOnly(true);
        _preview.AcceptsReturn(true);
        _preview.TextWrapping(TextWrapping::NoWrap);
        _preview.FontFamily(Media::FontFamily{ L"Cascadia Mono, Consolas" });
        _preview.IsSpellCheckEnabled(false);
        ScrollViewer::SetVerticalScrollBarVisibility(_preview, ScrollBarVisibility::Auto);
        ScrollViewer::SetHorizontalScrollBarVisibility(_preview, ScrollBarVisibility::Auto);
        Grid::SetColumn(_preview, 1);
        body.Children().Append(_preview);

        // Image preview shares the same cell as the text preview; we toggle which
        // one is visible based on the selected file's type.
        _image = Image{};
        _image.Margin({ 4, 0, 4, 4 });
        _image.Stretch(winrt::Windows::UI::Xaml::Media::Stretch::Uniform);
        _image.HorizontalAlignment(HorizontalAlignment::Center);
        _image.VerticalAlignment(VerticalAlignment::Center);
        _image.Visibility(winrt::Windows::UI::Xaml::Visibility::Collapsed);
        Grid::SetColumn(_image, 1);
        body.Children().Append(_image);

        _root.Children().Append(body);

        // Accept files/folders dragged in from Explorer (or other apps).
        _root.AllowDrop(true);
        _root.DragOver([](winrt::Windows::Foundation::IInspectable const&, winrt::Windows::UI::Xaml::DragEventArgs const& e) {
            if (e.DataView().Contains(winrt::Windows::ApplicationModel::DataTransfer::StandardDataFormats::StorageItems()))
            {
                e.AcceptedOperation(winrt::Windows::ApplicationModel::DataTransfer::DataPackageOperation::Link);
                e.Handled(true);
            }
        });
        _root.Drop([weak = get_weak()](winrt::Windows::Foundation::IInspectable const&, winrt::Windows::UI::Xaml::DragEventArgs const& e) -> winrt::fire_and_forget {
            auto self = weak.get();
            if (!self)
            {
                co_return;
            }
            if (!e.DataView().Contains(winrt::Windows::ApplicationModel::DataTransfer::StandardDataFormats::StorageItems()))
            {
                co_return;
            }
            auto dispatcher = winrt::Windows::System::DispatcherQueue::GetForCurrentThread();
            auto deferral = e.GetDeferral();
            // Everything below is wrapped so a thrown exception can neither escape
            // the fire_and_forget (which would terminate the process) nor skip
            // deferral.Complete() (which would dangle the drag operation).
            try
            {
                auto items = co_await e.DataView().GetStorageItemsAsync();
                co_await wil::resume_foreground(dispatcher);
                if (items && items.Size() > 0)
                {
                    fs::path p{ items.GetAt(0).Path().c_str() };
                    std::error_code ec;
                    if (fs::is_directory(p, ec))
                    {
                        self->_Navigate(p);
                    }
                    else
                    {
                        self->_Navigate(p.parent_path());
                        // Select the dropped file and preview it.
                        auto wanted = p.filename().wstring();
                        std::transform(wanted.begin(), wanted.end(), wanted.begin(), ::towlower);
                        for (size_t i = 0; i < self->_entries.size(); ++i)
                        {
                            auto name = self->_entries[i].filename().wstring();
                            std::transform(name.begin(), name.end(), name.begin(), ::towlower);
                            if (name == wanted)
                            {
                                self->_list.SelectedIndex(static_cast<int32_t>(i));
                                self->_LoadPreview(self->_entries[i]);
                                break;
                            }
                        }
                    }
                    self->_list.Focus(winrt::Windows::UI::Xaml::FocusState::Programmatic);
                }
            }
            catch (...)
            {
            }
            deferral.Complete();
        });

        _Populate();
    }

    void FileBrowserContent::_Populate()
    {
        _entries.clear();
        _list.Items().Clear();

        std::error_code ec;

        // Drive list view ("This PC"): list every available drive.
        if (_showingDrives)
        {
            wchar_t buf[512]{};
            const auto len = GetLogicalDriveStringsW(511, buf);
            for (wchar_t* p = buf; p < buf + len && *p != L'\0'; p += wcslen(p) + 1)
            {
                fs::path drive{ p };
                _entries.push_back(drive);
                _list.Items().Append(winrt::box_value(winrt::hstring{ drive.wstring() }));
            }
            _pathText.Text(L"Drives");
            if (!_entries.empty())
            {
                _list.SelectedIndex(0);
            }
            _title = winrt::hstring{ L"Files: Drives" };
            TitleChanged.raise(*this, nullptr);
            return;
        }

        // Parent entry. At a drive root (no real parent) it leads to the drive list.
        const auto atDriveRoot = !_cwd.has_parent_path() || _cwd.parent_path() == _cwd;
        _entries.push_back(atDriveRoot ? fs::path{} : _cwd.parent_path());
        _list.Items().Append(winrt::box_value(L"../"));

        // Bound the listing so a pathological directory (e.g. a huge node_modules)
        // can't freeze the UI thread while enumerating + inserting items.
        constexpr size_t maxEntries = 5000;
        bool truncated = false;
        std::vector<fs::path> dirs;
        std::vector<fs::path> files;
        if (fs::is_directory(_cwd, ec))
        {
            for (auto it = fs::directory_iterator{ _cwd, fs::directory_options::skip_permission_denied, ec };
                 !ec && it != fs::directory_iterator{};
                 it.increment(ec))
            {
                if (dirs.size() + files.size() >= maxEntries)
                {
                    truncated = true;
                    break;
                }
                std::error_code typeEc;
                if (it->is_directory(typeEc))
                {
                    dirs.push_back(it->path());
                }
                else
                {
                    files.push_back(it->path());
                }
            }
        }

        const auto byName = [](const fs::path& a, const fs::path& b) {
            auto an = a.filename().wstring();
            auto bn = b.filename().wstring();
            std::transform(an.begin(), an.end(), an.begin(), ::towlower);
            std::transform(bn.begin(), bn.end(), bn.begin(), ::towlower);
            return an < bn;
        };
        std::sort(dirs.begin(), dirs.end(), byName);
        std::sort(files.begin(), files.end(), byName);

        for (const auto& d : dirs)
        {
            _entries.push_back(d);
            _list.Items().Append(winrt::box_value(winrt::hstring{ d.filename().wstring() + L"/" }));
        }
        for (const auto& f : files)
        {
            _entries.push_back(f);
            _list.Items().Append(winrt::box_value(winrt::hstring{ f.filename().wstring() }));
        }

        if (truncated)
        {
            // Non-selectable marker (no matching _entries item, so Enter is a no-op).
            _list.Items().Append(winrt::box_value(winrt::hstring{ L"[only first " + std::to_wstring(maxEntries) + L" items shown]" }));
        }

        _pathText.Text(winrt::hstring{ _cwd.wstring() });
        if (!_entries.empty())
        {
            _list.SelectedIndex(0);
        }
        _UpdateTitle();
    }

    void FileBrowserContent::_UpdateTitle()
    {
        const auto leaf = _cwd.filename().wstring();
        _title = winrt::hstring{ L"Files: " + (leaf.empty() ? _cwd.wstring() : leaf) };
        TitleChanged.raise(*this, nullptr);
    }

    void FileBrowserContent::_Navigate(const fs::path& dir)
    {
        std::error_code ec;
        auto canonical = fs::weakly_canonical(dir, ec);
        _showingDrives = false;
        _cwd = ec ? dir : canonical;
        _Populate();
    }

    void FileBrowserContent::_GoUp()
    {
        if (_showingDrives)
        {
            return; // Already at the top (drive list).
        }
        if (!_cwd.has_parent_path() || _cwd.parent_path() == _cwd)
        {
            // At a drive root - go up to the drive list.
            _showingDrives = true;
            _Populate();
        }
        else
        {
            _Navigate(_cwd.parent_path());
        }
    }

    void FileBrowserContent::_OpenSelected()
    {
        const auto idx = _list.SelectedIndex();
        if (idx < 0 || static_cast<size_t>(idx) >= _entries.size())
        {
            return;
        }
        const auto& target = _entries[static_cast<size_t>(idx)];

        // In the drive list, any selection is a drive root to enter.
        if (_showingDrives)
        {
            _Navigate(target);
            return;
        }
        // An empty sentinel path is "../" at a drive root -> show the drive list.
        if (target.empty())
        {
            _showingDrives = true;
            _Populate();
            return;
        }

        std::error_code ec;
        if (fs::is_directory(target, ec))
        {
            _Navigate(target);
        }
        else
        {
            _LoadPreview(target);
        }
    }

    void FileBrowserContent::_LoadPreview(const fs::path& file)
    {
        // Invalidate any in-flight image load (it will see the bumped generation).
        const auto generation = ++_previewGeneration;

        // Pick the image viewer for known image types, otherwise the text viewer.
        auto ext = file.extension().wstring();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
        static const std::set<std::wstring> imageExts{
            L".png", L".jpg", L".jpeg", L".gif", L".bmp", L".ico", L".tif", L".tiff", L".webp"
        };
        if (imageExts.count(ext) != 0)
        {
            _LoadImage(file, generation);
            return;
        }

        // Text path: make sure the text viewer is the visible one.
        _image.Visibility(winrt::Windows::UI::Xaml::Visibility::Collapsed);
        _image.Source(nullptr);
        _preview.Visibility(winrt::Windows::UI::Xaml::Visibility::Visible);

        std::error_code ec;
        const auto size = fs::file_size(file, ec);
        constexpr uintmax_t cap = 512 * 1024;

        std::ifstream in{ file, std::ios::binary };
        if (!in)
        {
            _preview.Text(L"[unable to open file]");
            return;
        }
        std::string buf;
        buf.resize(static_cast<size_t>(std::min<uintmax_t>(ec ? cap : size, cap)));
        in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        buf.resize(static_cast<size_t>(in.gcount()));

        const auto truncatedNote = (!ec && size > cap) ? winrt::hstring{ L"\r\n\r\n[truncated - showing first 512 KB]" } : winrt::hstring{};
        try
        {
            const auto u = [](char c) { return static_cast<unsigned char>(c); };
            if (buf.size() >= 2 && u(buf[0]) == 0xFF && u(buf[1]) == 0xFE)
            {
                // UTF-16 LE (e.g. PowerShell redirected output) - decode directly.
                std::wstring_view w{ reinterpret_cast<const wchar_t*>(buf.data() + 2), (buf.size() - 2) / 2 };
                _preview.Text(winrt::hstring{ w } + truncatedNote);
            }
            else if (buf.size() >= 2 && u(buf[0]) == 0xFE && u(buf[1]) == 0xFF)
            {
                // UTF-16 BE - byte-swap then decode.
                std::wstring w;
                w.reserve((buf.size() - 2) / 2);
                for (size_t i = 2; i + 1 < buf.size(); i += 2)
                {
                    w.push_back(static_cast<wchar_t>((u(buf[i]) << 8) | u(buf[i + 1])));
                }
                _preview.Text(winrt::hstring{ w } + truncatedNote);
            }
            else
            {
                std::string_view sv{ buf };
                if (sv.size() >= 3 && u(sv[0]) == 0xEF && u(sv[1]) == 0xBB && u(sv[2]) == 0xBF)
                {
                    sv.remove_prefix(3); // strip UTF-8 BOM
                }
                if (sv.find('\0') != std::string_view::npos)
                {
                    _preview.Text(winrt::hstring{ L"[binary file - " + std::to_wstring(ec ? 0 : size) + L" bytes]" });
                    return;
                }
                _preview.Text(winrt::to_hstring(std::string{ sv }) + truncatedNote);
            }
        }
        catch (...)
        {
            _preview.Text(L"[unable to decode file as text]");
        }
    }

    winrt::fire_and_forget FileBrowserContent::_LoadImage(fs::path file, uint64_t generation)
    {
        auto self = get_strong();
        auto dispatcher = winrt::Windows::System::DispatcherQueue::GetForCurrentThread();

        // Do the (potentially large/slow) disk read and stream build off the UI
        // thread so previewing a big image doesn't freeze the terminal.
        co_await winrt::resume_background();

        winrt::Windows::Storage::Streams::InMemoryRandomAccessStream stream{ nullptr };
        bool ok = false;
        try
        {
            std::vector<uint8_t> bytes;
            std::ifstream in{ file, std::ios::binary | std::ios::ate };
            if (in)
            {
                std::streamoff sz = in.tellg();
                constexpr std::streamoff cap = 64 * 1024 * 1024;
                if (sz > cap)
                {
                    sz = cap;
                }
                bytes.resize(static_cast<size_t>(sz));
                in.seekg(0);
                in.read(reinterpret_cast<char*>(bytes.data()), sz);
                bytes.resize(static_cast<size_t>(in.gcount()));
            }
            if (!bytes.empty())
            {
                winrt::Windows::Storage::Streams::InMemoryRandomAccessStream s;
                winrt::Windows::Storage::Streams::DataWriter writer{ s };
                writer.WriteBytes(winrt::array_view<uint8_t const>{ bytes.data(), bytes.data() + bytes.size() });
                co_await writer.StoreAsync();
                writer.DetachStream();
                s.Seek(0);
                stream = s;
                ok = true;
            }
        }
        catch (...)
        {
            ok = false;
        }

        // Back to the UI thread to touch XAML.
        co_await wil::resume_foreground(dispatcher);

        // A newer selection happened while we were loading - don't clobber it.
        if (generation != _previewGeneration)
        {
            co_return;
        }

        if (!ok)
        {
            _image.Visibility(winrt::Windows::UI::Xaml::Visibility::Collapsed);
            _image.Source(nullptr);
            _preview.Visibility(winrt::Windows::UI::Xaml::Visibility::Visible);
            _preview.Text(L"[unable to read image]");
            co_return;
        }

        try
        {
            winrt::Windows::UI::Xaml::Media::Imaging::BitmapImage bmp;
            co_await bmp.SetSourceAsync(stream);
            if (generation != _previewGeneration)
            {
                co_return;
            }
            _image.Source(bmp);
            _image.Visibility(winrt::Windows::UI::Xaml::Visibility::Visible);
            _preview.Visibility(winrt::Windows::UI::Xaml::Visibility::Collapsed);
        }
        catch (...)
        {
            _image.Visibility(winrt::Windows::UI::Xaml::Visibility::Collapsed);
            _preview.Visibility(winrt::Windows::UI::Xaml::Visibility::Visible);
            _preview.Text(L"[unable to decode image]");
        }
    }

    void FileBrowserContent::_OnKeyDown(const winrt::Windows::Foundation::IInspectable& /*sender*/,
                                        const winrt::Windows::UI::Xaml::Input::KeyRoutedEventArgs& e)
    {
        switch (e.Key())
        {
        case winrt::Windows::System::VirtualKey::Enter:
            _OpenSelected();
            e.Handled(true);
            break;
        case winrt::Windows::System::VirtualKey::Back:
            _GoUp();
            e.Handled(true);
            break;
        case winrt::Windows::System::VirtualKey::D:
            // Jump straight to the drive list (filer-local, like ranger/yazi).
            ShowDrives();
            e.Handled(true);
            break;
        default:
            break;
        }
    }

    void FileBrowserContent::UpdateSettings(const CascadiaSettings& /*settings*/)
    {
        // Nothing to do.
    }

    winrt::Windows::UI::Xaml::FrameworkElement FileBrowserContent::GetRoot()
    {
        return _root;
    }

    winrt::Windows::Foundation::Size FileBrowserContent::MinimumSize()
    {
        return { 1, 1 };
    }

    void FileBrowserContent::Focus(winrt::Windows::UI::Xaml::FocusState reason)
    {
        if (_list)
        {
            _list.Focus(reason);
        }
    }

    void FileBrowserContent::Close()
    {
    }

    void FileBrowserContent::ShowDrives()
    {
        _showingDrives = true;
        _Populate();
        if (_list)
        {
            _list.Focus(winrt::Windows::UI::Xaml::FocusState::Programmatic);
        }
    }

    INewContentArgs FileBrowserContent::GetNewTerminalArgs(const BuildStartupKind /* kind */) const
    {
        // NOTE: like the other non-terminal panes (e.g. ScratchpadContent), only
        // the content type is round-tripped. Moving this pane to a new window or
        // restoring it does not preserve the current directory; it reopens at the
        // default location. Persisting the cwd would require a dedicated args type.
        return BaseContentArgs(L"fileBrowser");
    }

    winrt::hstring FileBrowserContent::Icon() const
    {
        static constexpr std::wstring_view glyph{ L"\xE8B7" }; // FolderHorizontal
        return winrt::hstring{ glyph };
    }

    winrt::Windows::UI::Xaml::Media::Brush FileBrowserContent::BackgroundBrush()
    {
        return _root.Background();
    }
}

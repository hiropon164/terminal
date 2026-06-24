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
                self->_list.Focus(winrt::Windows::UI::Xaml::FocusState::Programmatic);
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
            winrt::Windows::Foundation::Collections::IVectorView<winrt::Windows::Storage::IStorageItem> items{ nullptr };
            try
            {
                items = co_await e.DataView().GetStorageItemsAsync();
            }
            catch (...)
            {
            }
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

        std::vector<fs::path> dirs;
        std::vector<fs::path> files;
        if (fs::is_directory(_cwd, ec))
        {
            for (auto it = fs::directory_iterator{ _cwd, fs::directory_options::skip_permission_denied, ec };
                 !ec && it != fs::directory_iterator{};
                 it.increment(ec))
            {
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
        // Pick the image viewer for known image types, otherwise the text viewer.
        auto ext = file.extension().wstring();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
        static const std::set<std::wstring> imageExts{
            L".png", L".jpg", L".jpeg", L".gif", L".bmp", L".ico", L".tif", L".tiff", L".webp"
        };
        if (imageExts.count(ext) != 0)
        {
            _LoadImage(file);
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

        // Treat content with NUL bytes as binary - don't dump it as text.
        if (buf.find('\0') != std::string::npos)
        {
            _preview.Text(winrt::hstring{ L"[binary file - " + std::to_wstring(ec ? 0 : size) + L" bytes]" });
            return;
        }

        try
        {
            auto text = winrt::to_hstring(buf);
            if (!ec && size > cap)
            {
                text = text + L"\r\n\r\n[truncated - showing first 512 KB]";
            }
            _preview.Text(text);
        }
        catch (...)
        {
            _preview.Text(L"[unable to decode file as text]");
        }
    }

    winrt::fire_and_forget FileBrowserContent::_LoadImage(fs::path file)
    {
        auto self = get_strong();
        auto dispatcher = winrt::Windows::System::DispatcherQueue::GetForCurrentThread();

        // Read the file with raw IO so any path works (no app-container sandbox).
        std::vector<uint8_t> bytes;
        try
        {
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
        }
        catch (...)
        {
        }

        if (bytes.empty())
        {
            _image.Visibility(winrt::Windows::UI::Xaml::Visibility::Collapsed);
            _preview.Visibility(winrt::Windows::UI::Xaml::Visibility::Visible);
            _preview.Text(L"[unable to read image]");
            co_return;
        }

        winrt::Windows::Storage::Streams::InMemoryRandomAccessStream stream;
        winrt::Windows::Storage::Streams::DataWriter writer{ stream };
        writer.WriteBytes(winrt::array_view<uint8_t const>{ bytes.data(), bytes.data() + bytes.size() });
        co_await writer.StoreAsync();
        writer.DetachStream();
        stream.Seek(0);

        // Back to the UI thread to touch XAML.
        co_await wil::resume_foreground(dispatcher);

        try
        {
            winrt::Windows::UI::Xaml::Media::Imaging::BitmapImage bmp;
            co_await bmp.SetSourceAsync(stream);
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

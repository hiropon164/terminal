// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.
//
// Module Name:
// - Pane.h
//
// Abstract:
// - Panes are an abstraction by which the terminal can display multiple terminal
//   instances simultaneously in a single terminal window. While tabs allow for
//   a single terminal window to have many terminal sessions running
//   simultaneously within a single window, only one tab can be visible at a
//   time. Panes, on the other hand, allow a user to have many different
//   terminal sessions visible to the user within the context of a single window
//   at the same time. This can enable greater productivity from the user, as
//   they can see the output of one terminal window while working in another.
// - See doc/cascadia/Panes.md for a detailed description.
//
// Author:
// - Mike Griese (zadjii-msft) 16-May-2019

#pragma once

#include "TaskbarState.h"
#include "TerminalPaneContent.h"

// fwdecl unittest classes
namespace TerminalAppLocalTests
{
    class TabTests;
};

namespace winrt::TerminalApp::implementation
{
    struct Tab;
}

enum class Borders : int
{
    None = 0x0,
    Top = 0x1,
    Bottom = 0x2,
    Left = 0x4,
    Right = 0x8,
    All = 0xF
};
DEFINE_ENUM_FLAG_OPERATORS(Borders);

enum class SplitState : int
{
    None = 0,
    Horizontal = 1,
    Vertical = 2
};

struct PaneResources
{
    winrt::Windows::UI::Xaml::Media::SolidColorBrush focusedBorderBrush{ nullptr };
    winrt::Windows::UI::Xaml::Media::SolidColorBrush unfocusedBorderBrush{ nullptr };
    winrt::Windows::UI::Xaml::Media::SolidColorBrush broadcastBorderBrush{ nullptr };
};

class Pane : public std::enable_shared_from_this<Pane>
{
public:
    Pane(winrt::TerminalApp::IPaneContent content,
         const bool lastFocused = false);

    Pane(std::shared_ptr<Pane> first,
         std::shared_ptr<Pane> second,
         const SplitState splitType,
         const float splitPosition,
         const bool lastFocused = false);

    ~Pane();

    std::shared_ptr<Pane> GetActivePane();
    winrt::Microsoft::Terminal::Control::TermControl GetLastFocusedTerminalControl();
    winrt::TerminalApp::IPaneContent GetLastFocusedContent();
    winrt::Microsoft::Terminal::Control::TermControl GetTerminalControl() const;
    winrt::Microsoft::Terminal::Settings::Model::Profile GetFocusedProfile();
    bool IsConnectionClosed() const;

    // Method Description:
    // - If this is a leaf pane, return its profile.
    // - If this is a branch/root pane, return nullptr.
    winrt::Microsoft::Terminal::Settings::Model::Profile GetProfile() const
    {
        if (const auto& c{ _content.try_as<winrt::TerminalApp::TerminalPaneContent>() })
        {
            return c.GetProfile();
        }
        return nullptr;
    }

    winrt::Windows::UI::Xaml::Controls::Grid GetRootElement();
    winrt::TerminalApp::IPaneContent GetContent() const noexcept { return _IsLeaf() ? _content : nullptr; }

    bool WasLastFocused() const noexcept;
    void UpdateVisuals();
    void ClearActive();
    void SetActive();

    struct BuildStartupState
    {
        std::vector<winrt::Microsoft::Terminal::Settings::Model::ActionAndArgs> args;
        std::shared_ptr<Pane> firstPane;
        std::optional<uint32_t> focusedPaneId;
        uint32_t panesCreated;
    };
    BuildStartupState BuildStartupActions(uint32_t currentId, uint32_t nextId, winrt::TerminalApp::BuildStartupKind kind);
    winrt::Microsoft::Terminal::Settings::Model::INewContentArgs GetTerminalArgsForPane(winrt::TerminalApp::BuildStartupKind kind) const;

    void UpdateSettings(const winrt::Microsoft::Terminal::Settings::Model::CascadiaSettings& settings);
    bool ResizePane(const winrt::Microsoft::Terminal::Settings::Model::ResizeDirection& direction);
    std::shared_ptr<Pane> NavigateDirection(const std::shared_ptr<Pane> sourcePane,
                                            const winrt::Microsoft::Terminal::Settings::Model::FocusDirection& direction,
                                            const std::vector<uint32_t>& mruPanes);
    bool SwapPanes(std::shared_ptr<Pane> first, std::shared_ptr<Pane> second);

    std::shared_ptr<Pane> NextPane(const std::shared_ptr<Pane> pane);
    std::shared_ptr<Pane> PreviousPane(const std::shared_ptr<Pane> pane);

    std::pair<std::shared_ptr<Pane>, std::shared_ptr<Pane>> Split(winrt::Microsoft::Terminal::Settings::Model::SplitDirection splitType,
                                                                  const float splitSize,
                                                                  std::shared_ptr<Pane> pane);
    bool ToggleSplitOrientation();
    void EqualizeSplits();
    float CalcSnappedDimension(const bool widthOrHeight, const float dimension) const;
    std::optional<std::optional<winrt::Microsoft::Terminal::Settings::Model::SplitDirection>> PreCalculateCanSplit(const std::shared_ptr<Pane> target,
                                                                                                                   winrt::Microsoft::Terminal::Settings::Model::SplitDirection splitType,
                                                                                                                   const float splitSize,
                                                                                                                   const winrt::Windows::Foundation::Size availableSpace) const;
    void Shutdown();
    void Close();

    std::shared_ptr<Pane> AttachPane(std::shared_ptr<Pane> pane,
                                     winrt::Microsoft::Terminal::Settings::Model::SplitDirection splitType);
    std::shared_ptr<Pane> DetachPane(std::shared_ptr<Pane> pane);

    // Given a point relative to this pane's root element, returns the leaf pane
    // that contains it and sets `dir` to which edge of that leaf the point is
    // closest to (used by mouse drag-to-move to decide the insertion side).
    std::shared_ptr<Pane> LeafAndDirectionAt(winrt::Windows::Foundation::Point pos,
                                             winrt::Microsoft::Terminal::Settings::Model::SplitDirection& dir);

    // Clears the Alt+drag-to-move tint/state. Called by the owning Tab when the
    // drag (driven by its capture overlay) ends or is cancelled.
    void ClearMoveDragVisual();

    int GetLeafPaneCount() const noexcept;

    void Maximize(std::shared_ptr<Pane> zoomedPane);
    void Restore(std::shared_ptr<Pane> zoomedPane);

    std::optional<uint32_t> Id() noexcept;
    void Id(uint32_t id) noexcept;
    bool FocusPane(const uint32_t id);
    bool FocusPane(const std::shared_ptr<Pane> pane);
    std::shared_ptr<Pane> FindPane(const uint32_t id);

    void FinalizeConfigurationGivenDefault();

    bool ContainsReadOnly() const;

    void EnableBroadcast(bool enabled);
    void BroadcastKey(const winrt::Microsoft::Terminal::Control::TermControl& sourceControl, const WORD vkey, const WORD scanCode, const winrt::Microsoft::Terminal::Core::ControlKeyStates modifiers, const bool keyDown);
    void BroadcastChar(const winrt::Microsoft::Terminal::Control::TermControl& sourceControl, const wchar_t vkey, const WORD scanCode, const winrt::Microsoft::Terminal::Core::ControlKeyStates modifiers);
    void BroadcastString(const winrt::Microsoft::Terminal::Control::TermControl& sourceControl, const winrt::hstring& text);

    void UpdateResources(const PaneResources& resources);

    // Method Description:
    // - A helper method for ad-hoc recursion on a pane tree. Walks the pane
    //   tree, calling a function on each pane in a depth-first pattern.
    // - If that function returns void, then it will be called on every pane.
    // - Otherwise, iteration will continue until a value with operator bool
    //   returns true.
    // Arguments:
    // - f: The function to be applied to each pane.
    // Return Value:
    // - The value of the function applied on a Pane
    template<typename F>
    auto WalkTree(F f) -> decltype(f(shared_from_this()))
    {
        using R = std::invoke_result_t<F, std::shared_ptr<Pane>>;
        static constexpr auto IsVoid = std::is_void_v<R>;

        if constexpr (IsVoid)
        {
            f(shared_from_this());
            if (!_IsLeaf())
            {
                _firstChild->WalkTree(f);
                _secondChild->WalkTree(f);
            }
        }
        else
        {
            if (const auto res = f(shared_from_this()))
            {
                return res;
            }

            if (!_IsLeaf())
            {
                if (const auto res = _firstChild->WalkTree(f))
                {
                    return res;
                }
                return _secondChild->WalkTree(f);
            }

            return R{};
        }
    }

    template<typename F>
    std::shared_ptr<Pane> _FindPane(F f)
    {
        return WalkTree([f](const auto& pane) -> std::shared_ptr<Pane> {
            if (f(pane))
            {
                return pane;
            }
            return nullptr;
        });
    }

    void CollectTaskbarStates(std::vector<winrt::TerminalApp::TaskbarState>& states);

    til::event<winrt::delegate<>> ClosedByParent;
    til::event<winrt::Windows::Foundation::EventHandler<winrt::Windows::Foundation::IInspectable>> Closed;

    using gotFocusArgs = winrt::delegate<std::shared_ptr<Pane>, winrt::Windows::UI::Xaml::FocusState>;

    til::event<gotFocusArgs> GotFocus;
    til::event<winrt::delegate<std::shared_ptr<Pane>>> LostFocus;
    til::event<winrt::delegate<std::shared_ptr<Pane>>> Detached;
    // Raised while the user is Alt+dragging this (leaf) pane (the Point is the
    // current cursor position in window coordinates). The Tab uses it to make a
    // ghost follow the cursor.
    // Raised when an Alt+drag of this (leaf) pane begins. The Tab takes over from
    // here using a top-level capture overlay (so content like WebView2 / ListView
    // can't intercept the drag). The Pointer is the one to capture on the overlay.
    til::event<winrt::delegate<std::shared_ptr<Pane>, winrt::Windows::UI::Xaml::Input::Pointer>> MovePaneDragStarted;
    til::event<winrt::delegate<std::shared_ptr<Pane>, winrt::Windows::Foundation::Point>> MovePaneDragMoved;
    // Raised when an Alt+drag is aborted without a drop (e.g. the pointer capture
    // was lost), so the Tab can tear down the drag ghost.
    til::event<winrt::delegate<>> MovePaneDragCanceled;
    // Raised when the user finishes an Alt+drag of this (leaf) pane. The second
    // argument is the drop position in window coordinates; the Tab resolves it
    // to a target pane + insertion side and performs the move.
    til::event<winrt::delegate<std::shared_ptr<Pane>, winrt::Windows::Foundation::Point>> MovePaneRequested;

private:
    struct PanePoint;
    struct PaneNeighborSearch;
    struct SnapSizeResult;
    struct SnapChildrenSizeResult;
    struct LayoutSizeNode;

    winrt::Windows::UI::Xaml::Controls::Grid _root{};
    winrt::Windows::UI::Xaml::Controls::Border _borderFirst{};
    winrt::Windows::UI::Xaml::Controls::Border _borderSecond{};
    // A thin, hit-testable strip straddling the split boundary that lets the
    // user drag to resize the two child panes with the mouse.
    winrt::Windows::UI::Xaml::Controls::Border _resizeSeparator{ nullptr };
    bool _separatorDragging{ false };
    // True while the user is Alt+dragging this leaf pane to move it elsewhere.
    bool _movePaneDragging{ false };
    // A translucent tint shown over this leaf's content while it is being dragged.
    winrt::Windows::UI::Xaml::Controls::Border _dragHighlight{ nullptr };
    // The PointerPressed handler registered on _borderFirst that begins the
    // Alt+drag-to-move gesture. Kept so it can be removed in the destructor (it
    // captures a raw `this`).
    winrt::Windows::Foundation::IInspectable _movePressedHandler{ nullptr };

    PaneResources _themeResources;

#pragma region Properties that need to be transferred between child / parent panes upon splitting / closing
    std::shared_ptr<Pane> _firstChild{ nullptr };
    std::shared_ptr<Pane> _secondChild{ nullptr };
    SplitState _splitState{ SplitState::None };
    float _desiredSplitPosition;

    winrt::TerminalApp::IPaneContent _content{ nullptr };
#pragma endregion

    std::optional<uint32_t> _id;
    std::weak_ptr<Pane> _parentChildPath{};
    bool _lastActive{ false };
    winrt::event_token _firstClosedToken{ 0 };
    winrt::event_token _secondClosedToken{ 0 };

    winrt::Windows::UI::Xaml::UIElement::GotFocus_revoker _gotFocusRevoker;
    winrt::Windows::UI::Xaml::UIElement::LostFocus_revoker _lostFocusRevoker;
    winrt::TerminalApp::IPaneContent::CloseRequested_revoker _closeRequestedRevoker;

    Borders _borders{ Borders::None };

    bool _zoomed{ false };
    bool _broadcastEnabled{ false };

    bool _IsLeaf() const noexcept;
    bool _HasFocusedChild() const noexcept;
    void _SetupChildCloseHandlers();
    winrt::TerminalApp::IPaneContent _takePaneContent();
    void _setPaneContent(winrt::TerminalApp::IPaneContent content);
    bool _HasChild(const std::shared_ptr<Pane> child);
    winrt::TerminalApp::TerminalPaneContent _getTerminalContent() const;

    std::pair<std::shared_ptr<Pane>, std::shared_ptr<Pane>> _Split(winrt::Microsoft::Terminal::Settings::Model::SplitDirection splitType,
                                                                   const float splitSize,
                                                                   std::shared_ptr<Pane> newPane);

    void _CreateRowColDefinitions();
    void _ApplySplitDefinitions();
    void _SetupEntranceAnimation();
    void _UpdateBorders();
    Borders _GetCommonBorders();
    winrt::Windows::UI::Xaml::Media::SolidColorBrush _ComputeBorderColor();

    bool _Resize(const winrt::Microsoft::Terminal::Settings::Model::ResizeDirection& direction);

    std::shared_ptr<Pane> _FindParentOfPane(const std::shared_ptr<Pane> pane);
    std::pair<PanePoint, PanePoint> _GetOffsetsForPane(const PanePoint parentOffset) const;
    bool _IsAdjacent(const PanePoint firstOffset, const PanePoint secondOffset, const winrt::Microsoft::Terminal::Settings::Model::FocusDirection& direction) const;
    PaneNeighborSearch _FindNeighborForPane(const winrt::Microsoft::Terminal::Settings::Model::FocusDirection& direction,
                                            PaneNeighborSearch searchResult,
                                            const bool focusIsSecondSide,
                                            const PanePoint offset);
    PaneNeighborSearch _FindPaneAndNeighbor(const std::shared_ptr<Pane> sourcePane,
                                            const winrt::Microsoft::Terminal::Settings::Model::FocusDirection& direction,
                                            const PanePoint offset);

    void _CloseChild(const bool closeFirst);
    void _CloseChildRoutine(const bool closeFirst);

    void _Focus();
    void _FocusFirstChild();
    void _ContentGotFocusHandler(const winrt::Windows::Foundation::IInspectable& sender,
                                 const winrt::Windows::UI::Xaml::RoutedEventArgs& e);
    void _ContentLostFocusHandler(const winrt::Windows::Foundation::IInspectable& sender,
                                  const winrt::Windows::UI::Xaml::RoutedEventArgs& e);

    std::pair<float, float> _CalcChildrenSizes(const float fullSize) const;
    SnapChildrenSizeResult _CalcSnappedChildrenSizes(const bool widthOrHeight, const float fullSize) const;
    SnapSizeResult _CalcSnappedDimension(const bool widthOrHeight, const float dimension) const;
    void _AdvanceSnappedDimension(const bool widthOrHeight, LayoutSizeNode& sizeNode) const;
    winrt::Windows::Foundation::Size _GetMinSize() const;
    LayoutSizeNode _CreateMinSizeTree(const bool widthOrHeight) const;
    float _ClampSplitPosition(const bool widthOrHeight, const float requestedValue, const float totalSize) const;

    SplitState _convertAutomaticOrDirectionalSplitState(const winrt::Microsoft::Terminal::Settings::Model::SplitDirection& splitType) const;

    void _borderTappedHandler(const winrt::Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::Input::TappedRoutedEventArgs& e);

    void _SetupResizeSeparator();
    void _SeparatorPointerPressed(const winrt::Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs& e);
    void _SeparatorPointerMoved(const winrt::Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs& e);
    void _SeparatorPointerReleased(const winrt::Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs& e);
    void _SeparatorPointerEntered(const winrt::Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs& e);
    void _SeparatorPointerExited(const winrt::Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs& e);

    void _SetupMovePaneDrag();
    void _MovePanePointerPressed(const winrt::Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs& e);

    // Function Description:
    // - Returns true if the given direction can be used with the given split
    //   type.
    // - This is used for pane resizing (which will need a pane separator
    //   that's perpendicular to the direction to be able to move the separator
    //   in that direction).
    // - Also used for moving focus between panes, which again happens _across_ a separator.
    // Arguments:
    // - direction: The Direction to compare
    // - splitType: The SplitState to compare
    // Return Value:
    // - true iff the direction is perpendicular to the splitType. False for
    //   SplitState::None.
    template<typename T>
    static constexpr bool DirectionMatchesSplit(const T& direction,
                                                const SplitState& splitType)
    {
        if (splitType == SplitState::None)
        {
            return false;
        }
        else if (splitType == SplitState::Horizontal)
        {
            return direction == T::Up ||
                   direction == T::Down;
        }
        else if (splitType == SplitState::Vertical)
        {
            return direction == T::Left ||
                   direction == T::Right;
        }
        return false;
    }

    struct PanePoint
    {
        float x;
        float y;
        float scaleX;
        float scaleY;
    };

    struct PaneNeighborSearch
    {
        std::shared_ptr<Pane> source;
        std::shared_ptr<Pane> neighbor;
        PanePoint sourceOffset;
    };

    struct SnapSizeResult
    {
        float lower;
        float higher;
    };

    struct SnapChildrenSizeResult
    {
        std::pair<float, float> lower;
        std::pair<float, float> higher;
    };

    // Helper structure that builds a (roughly) binary tree corresponding
    // to the pane tree. Used for laying out panes with snapped sizes.
    struct LayoutSizeNode
    {
        float size;
        bool isMinimumSize;
        std::unique_ptr<LayoutSizeNode> firstChild;
        std::unique_ptr<LayoutSizeNode> secondChild;

        // These two fields hold next possible snapped values of firstChild and
        // secondChild. Although that could be calculated from these fields themselves,
        // it would be wasteful as we have to know these values more often than for
        // simple increment. Hence we cache that here.
        std::unique_ptr<LayoutSizeNode> nextFirstChild;
        std::unique_ptr<LayoutSizeNode> nextSecondChild;

        explicit LayoutSizeNode(const float minSize);
        LayoutSizeNode(const LayoutSizeNode& other);

        LayoutSizeNode& operator=(const LayoutSizeNode& other);
    };

    friend struct winrt::TerminalApp::implementation::Tab;
    friend class ::TerminalAppLocalTests::TabTests;
};

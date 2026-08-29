#pragma once
#include "imgui_internal.h"

namespace UITreeFlags{

	inline constexpr ImGuiTreeNodeFlags LeafFlags =
		ImGuiTreeNodeFlags_Leaf |
		ImGuiTreeNodeFlags_NoTreePushOnOpen |
		ImGuiTreeNodeFlags_SpanAvailWidth;

	inline constexpr ImGuiTreeNodeFlags BranchOpenedFlags =
		ImGuiTreeNodeFlags_DefaultOpen |
		ImGuiTreeNodeFlags_OpenOnArrow |
		ImGuiTreeNodeFlags_SpanAvailWidth;

	inline constexpr ImGuiTreeNodeFlags BranchClosedFlags =
		ImGuiTreeNodeFlags_OpenOnArrow |
		ImGuiTreeNodeFlags_SpanAvailWidth;

}

namespace UIFlagsDocking {

	// NoDecoration already covers NoTitleBar | NoResize | NoScrollbar | NoCollapse
	inline constexpr ImGuiWindowFlags MainDockWindowFlags =
		ImGuiWindowFlags_NoDecoration |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoBringToFrontOnFocus |
		ImGuiWindowFlags_NoNavFocus |
		ImGuiWindowFlags_NoBackground;

	// The app-wide toolbar strip above the dockspace. Same as the dockspace host,
	// minus NoBackground (the strip's band must be opaque) plus NoDocking so it
	// can't be dragged into the dockspace it sits above.
	inline constexpr ImGuiWindowFlags AppToolbarWindowFlags =
		ImGuiWindowFlags_NoDecoration |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoBringToFrontOnFocus |
		ImGuiWindowFlags_NoNavFocus |
		ImGuiWindowFlags_NoDocking |
		ImGuiWindowFlags_NoSavedSettings;

	inline constexpr ImGuiWindowFlags ModalPopupFlags =
		ImGuiWindowFlags_AlwaysAutoResize |
		ImGuiWindowFlags_NoMove;

}

namespace UITabBarFlags {

	// the main setup tab bar wants plain default behavior (tabs are not
	// reorderable unless ImGuiTabBarFlags_Reorderable is set)
	inline constexpr ImGuiTabBarFlags TabBarFlags = ImGuiTabBarFlags_None;

	inline constexpr ImGuiTabBarFlags InspectorTabBarFlags =
		ImGuiTabBarFlags_Reorderable |			// drag to reorder shown-field tabs
		ImGuiTabBarFlags_FittingPolicyScroll |	// scroll when the tabs overflow
		ImGuiTabBarFlags_TabListPopupButton;	// dropdown to jump to any field

}

namespace UIViewport {

	// The four tab viewports share one dock slot and are never submitted on the same
	// frame. ImGui identifies a window by whatever follows the last "###", so these
	// titles all resolve to a single window that stays alive across tab switches —
	// only its label and contents change. Without that, the slot's dock node loses
	// its only window for the frame after a switch, and an empty central node paints
	// ImGuiCol_DockingEmptyBg over the whole viewport, which reads as a blink.
	// Anything drawn under one of these titles must also set a window class with
	// UIDockFlags::NoDockWindowFlags, since the shared window keeps the last class
	// it was given.
	inline constexpr const char* SketchTitle = "Sketch View###Viewport";
	inline constexpr const char* MeshInspectorTitle = "Mesh Inspector###Viewport";
	inline constexpr const char* ResidualPlotTitle = "Residual Plot###Viewport";
	inline constexpr const char* ResultsTitle = "Results###Viewport";

	// The single window identity behind all four titles: ImHashStr() restarts at the
	// last "###" and skips it, so this hashes to the same window the titles above do.
	// Use it to address the shared slot by name (GUI::buildDefaultDockLayout docks it).
	inline constexpr const char* DockName = "###Viewport";

}

namespace UIDockFlags {

	inline constexpr ImGuiDockNodeFlags NoDockWindowFlags =
		ImGuiDockNodeFlags_NoTabBar |
		ImGuiDockNodeFlags_NoDockingOverMe;

	inline constexpr ImGuiDockNodeFlags BaseDockspaceFlags =
		ImGuiDockNodeFlags_NoWindowMenuButton;

	// Inner dockspace holding viewer tabs (DockingSpace). Each tab window is begun
	// with a p_open and so draws its own close button -- the node-level one would be
	// a second X right beside it.
	inline constexpr ImGuiDockNodeFlags ViewerDockspaceFlags =
		ImGuiDockNodeFlags_NoWindowMenuButton |
		ImGuiDockNodeFlags_NoCloseButton;

}

namespace UIColors {

	// The one accent in the app. It fills an active toolbar toggle and outlines the
	// selected dock tab, so the two read as the same "this is what you are acting
	// on" signal -- keep them the same color.
	inline constexpr ImU32 Accent        = IM_COL32(60, 140, 255, 255);
	inline constexpr ImU32 AccentHovered = IM_COL32(80, 160, 255, 255);
	inline constexpr ImU32 AccentActive  = IM_COL32(40, 120, 235, 255);

	// Idle toolbar buttons are transparent, so hover/press paint a wash over the
	// band behind them. Dark, because a white wash is invisible on light chrome.
	inline constexpr ImU32 HoverWash  = IM_COL32(0, 0, 0, 28);
	inline constexpr ImU32 ActiveWash = IM_COL32(0, 0, 0, 46);

}

namespace UIInputTextFlags {

	inline constexpr ImGuiInputTextFlags ConsoleInputFlags =
		ImGuiInputTextFlags_EnterReturnsTrue |
		ImGuiInputTextFlags_EscapeClearsAll |
		ImGuiInputTextFlags_CallbackHistory |
		ImGuiInputTextFlags_CallbackCompletion |
		ImGuiInputTextFlags_CallbackAlways;

}

namespace UIFlags {

	inline constexpr ImGuiWindowFlags StatusBarWindowFlags =
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoDocking;

	inline constexpr ImGuiWindowFlags AnimationWindowFlags =
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoCollapse;

	// NoDecoration already covers NoScrollbar
	inline constexpr ImGuiWindowFlags TemporaryWindowFlags =
		ImGuiWindowFlags_NoDecoration |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoScrollWithMouse |
		ImGuiWindowFlags_NoInputs |
		ImGuiWindowFlags_NoFocusOnAppearing |
		ImGuiWindowFlags_NoBringToFrontOnFocus;

	inline constexpr ImGuiTableFlags TableSimpleFlags =
		ImGuiTableFlags_Borders |
		ImGuiTableFlags_RowBg |
		ImGuiTableFlags_SizingStretchProp;

}

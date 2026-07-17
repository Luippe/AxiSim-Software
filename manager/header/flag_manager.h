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

}

namespace UIDockFlags {

	inline constexpr ImGuiDockNodeFlags NoDockWindowFlags =
		ImGuiDockNodeFlags_NoTabBar |
		ImGuiDockNodeFlags_NoDockingOverMe;

	inline constexpr ImGuiDockNodeFlags BaseDockspaceFlags =
		ImGuiDockNodeFlags_NoWindowMenuButton;

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

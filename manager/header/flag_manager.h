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

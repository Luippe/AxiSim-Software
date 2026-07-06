#pragma once
#include "imgui_internal.h"

namespace UIFlagsTree{

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

	inline constexpr ImGuiTreeNodeFlags BranchGroupFlags =
		ImGuiTreeNodeFlags_OpenOnArrow |
		ImGuiTreeNodeFlags_OpenOnDoubleClick |
		ImGuiTreeNodeFlags_SpanAvailWidth;

}

namespace UIFlagsDocking {
	inline constexpr ImGuiWindowFlags MainDockWindowFlags =
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoBringToFrontOnFocus |
		ImGuiWindowFlags_NoNavFocus |
		ImGuiWindowFlags_NoBackground |
		ImGuiWindowFlags_NoDecoration;
}

namespace UIFlags {

	inline constexpr ImGuiWindowFlags StatusBarWindowFlags =
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoDocking;

	inline constexpr ImGuiWindowFlags BaseDockWindowFlags =
		ImGuiWindowFlags_NoCollapse;

	inline constexpr ImGuiWindowFlags AnimationWindowFlags =
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoCollapse;

	inline constexpr ImGuiWindowFlags ResidualTabBarFlags =
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoMove;

	inline constexpr ImGuiTabBarFlags TabBarFlags = ImGuiTabItemFlags_NoReorder;

	inline constexpr ImGuiDockNodeFlags BaseDockspaceFlags =
		ImGuiDockNodeFlags_NoWindowMenuButton;

	inline constexpr ImGuiDockNodeFlags ResidualDockSpaceFlags =
		ImGuiDockNodeFlags_None |
		ImGuiDockNodeFlags_NoCloseButton;

	inline constexpr ImGuiWindowFlags TemporaryWindowFlags = 
		ImGuiWindowFlags_NoDecoration |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoScrollWithMouse |
		ImGuiWindowFlags_NoInputs |
		ImGuiWindowFlags_NoFocusOnAppearing |
		ImGuiWindowFlags_NoBringToFrontOnFocus;



	inline constexpr ImGuiInputTextFlags ConsoleMultilineInputFlags =
		ImGuiInputTextFlags_ReadOnly |
		ImGuiInputTextFlags_NoUndoRedo |
		ImGuiInputTextFlags_CallbackAlways;

	inline constexpr ImGuiInputTextFlags ConsoleInputFlags =
		ImGuiInputTextFlags_EnterReturnsTrue |
		ImGuiInputTextFlags_EscapeClearsAll |
		ImGuiInputTextFlags_CallbackHistory |
		ImGuiInputTextFlags_CallbackCompletion |
		ImGuiInputTextFlags_CallbackAlways;


	inline constexpr ImGuiTableFlags TableBoundaryFlags =
		ImGuiTableFlags_BordersInnerV |
		ImGuiTableFlags_BordersOuter |
		ImGuiTableFlags_RowBg |
		ImGuiTableFlags_SizingStretchProp;

	inline constexpr ImGuiTableFlags TableSimpleFlags =
		ImGuiTableFlags_Borders |
		ImGuiTableFlags_RowBg |
		ImGuiTableFlags_SizingStretchProp;

}
#pragma once
#include "imgui_internal.h"

namespace UIFlags {

	inline constexpr ImGuiTreeNodeFlags LeafFlags = 
		ImGuiTreeNodeFlags_Leaf |
		ImGuiTreeNodeFlags_NoTreePushOnOpen |
		ImGuiTreeNodeFlags_SpanAvailWidth;

	inline constexpr ImGuiTreeNodeFlags BranchFlags =
		ImGuiTreeNodeFlags_DefaultOpen |
		ImGuiTreeNodeFlags_OpenOnArrow |
		ImGuiTreeNodeFlags_SpanAvailWidth;

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
}


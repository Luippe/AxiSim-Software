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

	inline constexpr ImGuiDockNodeFlags BaseDockspaceFlags =
		ImGuiDockNodeFlags_NoWindowMenuButton;
}


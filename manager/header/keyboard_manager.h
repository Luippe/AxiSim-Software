#pragma once
#include "imgui.h"

namespace Shortcuts {

	inline constexpr ImGuiKeyChord defaultUndoShortcut =
		ImGuiMod_Ctrl | ImGuiKey_Z;

	inline constexpr ImGuiKeyChord defaultRedoShortcut =
		ImGuiMod_Ctrl | ImGuiKey_Y;

	inline constexpr ImGuiKeyChord defaultResetViewShortcut =
		ImGuiMod_Ctrl | ImGuiKey_H;

	inline constexpr ImGuiKeyChord defaultSelectToolShortcut =
		ImGuiKey_Escape;

	inline constexpr ImGuiKeyChord defaultRulerToolShortcut =
		ImGuiMod_Ctrl | ImGuiKey_R;

	inline constexpr ImGuiKeyChord defaultTrimToolShortcut =
		ImGuiMod_Ctrl | ImGuiKey_T;

	inline constexpr ImGuiKeyChord defaultEraseToolShortcut =
		ImGuiMod_Ctrl | ImGuiKey_E;

	inline constexpr ImGuiKeyChord defaultLineToolShortcut =
		ImGuiMod_Ctrl | ImGuiKey_L;

	inline constexpr ImGuiKeyChord defaultRectangleToolShortcut =
		ImGuiMod_Ctrl | ImGuiKey_W;

	inline constexpr ImGuiKeyChord defaultCircleToolShortcut =
		ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_C;

	inline constexpr ImGuiKeyChord defaultSaveProjectShortcut =
		ImGuiMod_Ctrl | ImGuiKey_S;

	inline constexpr ImGuiKeyChord defaultCopyShortcut =
		ImGuiMod_Ctrl | ImGuiKey_C;

	inline constexpr ImGuiKeyChord defaultPasteShortcut =
		ImGuiMod_Ctrl | ImGuiKey_V;

	inline ImGuiKeyChord undoShortcut = defaultUndoShortcut;

	inline ImGuiKeyChord redoShortcut = defaultRedoShortcut;

	inline ImGuiKeyChord resetViewShortcut = defaultResetViewShortcut;

	inline ImGuiKeyChord selectToolShortcut = defaultSelectToolShortcut;

	inline ImGuiKeyChord rulerToolShortcut = defaultRulerToolShortcut;

	inline ImGuiKeyChord trimToolShortcut = defaultTrimToolShortcut;

	inline ImGuiKeyChord eraseToolShortcut = defaultEraseToolShortcut;

	inline ImGuiKeyChord lineToolShortcut = defaultLineToolShortcut;

	inline ImGuiKeyChord rectangleToolShortcut = defaultRectangleToolShortcut;

	inline ImGuiKeyChord circleToolShortcut = defaultCircleToolShortcut;

	inline ImGuiKeyChord saveProjectShortcut = defaultSaveProjectShortcut;

	inline ImGuiKeyChord copyShortcut = defaultCopyShortcut;

	inline ImGuiKeyChord pasteShortcut = defaultPasteShortcut;

	inline ImGuiKeyChord* allShortcuts[] = {
		&selectToolShortcut,
		&rulerToolShortcut,
		&trimToolShortcut,
		&eraseToolShortcut,
		&lineToolShortcut,
		&rectangleToolShortcut,
		&circleToolShortcut,
		&resetViewShortcut,
		&undoShortcut,
		&redoShortcut,
		&saveProjectShortcut,
		&copyShortcut,
		&pasteShortcut
	};

	inline void resetShortcutsToDefault() {
		undoShortcut = defaultUndoShortcut;
		redoShortcut = defaultRedoShortcut;
		resetViewShortcut = defaultResetViewShortcut;
		selectToolShortcut = defaultSelectToolShortcut;
		rulerToolShortcut = defaultRulerToolShortcut;
		trimToolShortcut = defaultTrimToolShortcut;
		eraseToolShortcut = defaultEraseToolShortcut;
		lineToolShortcut = defaultLineToolShortcut;
		rectangleToolShortcut = defaultRectangleToolShortcut;
		circleToolShortcut = defaultCircleToolShortcut;
		saveProjectShortcut = defaultSaveProjectShortcut;
		copyShortcut = defaultCopyShortcut;
		pasteShortcut = defaultPasteShortcut;
	}

}

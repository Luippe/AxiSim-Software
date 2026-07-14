#pragma once
#include <vector>
#include <string>
#include <unordered_map>
#include <functional>
#include "imgui.h"

class GUI;
class Project;

// create a console for displaying messages and accepting user input
class Console {
public:

	Console(GUI& gui, Project& project);

	// add line to console
	void addLine(const std::string& s);

	// add line that tells a specific task was completed
	void addCompletionMessage(const std::string& s);

	void addCompletionTime(const std::string& object, float& ms);

	// add horizontal line
	void addSeparator();

	// execute command in console
	void executeCommand(const std::string& cmd);

	// clear the console
	void clear();

	// draw the console UI
	void draw();


private:

	using CommandArgs = std::vector<std::string>;
	using CommandFn = std::function<void(const CommandArgs&)>;

	GUI& gui;
	Project& project;

	struct Command {
		CommandFn run;
		std::string usage;
		std::string description;

		// valid objects for this action (second word), used for tab completion
		std::vector<std::string> objects;

		// valid values per object (third word), keyed by object name, used for
		// tab completion of the value token
		std::unordered_map<std::string, std::vector<std::string>> values;
	};

	std::vector<std::string> lines;
	bool autoScroll = true;
	bool scrollToBottom = false;
	char inputBuffer[256] = {};

	std::unordered_map<std::string, Command> commands;

	std::string getWord(const std::vector<std::string>& words, size_t index);


	void registerCommands();
	void registerSetCommands();
	void registerGetCommands();
	void registerCopyCommands();
	void registerSaveAndLoadCommands();
	void registerUtilityCommands();
	void registerShowCommands();


	void addCommand(const std::string& name, CommandFn function, const std::string& usage, const std::string& description, std::vector<std::string> objects = {}, std::unordered_map<std::string, std::vector<std::string>> values = {});

	// colormap names available for value completion (e.g. "set colormap <name>")
	std::vector<std::string> colormapNames() const;

	// check if auto scroll is on, if it is, scroll to bottom whener a line is added
	void checkAutoScroll();

	std::vector<std::string> commandHistory;
	int historyPos = -1;
	static int textEditCallbackStub(ImGuiInputTextCallbackData* data);
	int textEditCallback(ImGuiInputTextCallbackData* data);

	// ----------------- tab / dropdown autocomplete -----------------
	struct CompletionItem {
		std::string word;
		std::string description;
	};

	// where the word being completed sits in the input and which token it is
	struct CompletionContext {
		int wordStart = 0;			// byte offset of the token start
		int wordEnd = 0;			// byte offset of the cursor / token end
		int wordIndex = 0;			// 0 = action, 1 = object, ...
		std::string partial;		// text typed so far for this token
	};

	std::vector<CompletionItem> completionItems;	// current dropdown entries
	int completionIndex = 0;						// highlighted entry
	bool completionActive = false;					// dropdown is showing
	bool completionNavigated = false;				// user moved with arrows
	bool completionDismissed = false;				// stay closed after an accept / delete until the user types
	bool forceCompletion = false;					// Ctrl+Space force-opened the list (show even on empty input)
	bool completionJustAccepted = false;			// a completion was just inserted (Tab); keep the list closed
	std::string lastInput;							// detect edits between frames
	bool refocusInput = false;						// re-focus input next frame
	bool resetInputCursor = false;					// snap cursor to end next frame
	int inputCursorPos = 0;							// live caret position in the input
	ImVec2 lastInputMin{};							// input box rect from last frame,
	ImVec2 lastInputMax{};							// used to exclude it from click-to-focus

	CompletionContext getCompletionContext(const std::string& text, int cursor) const;
	std::vector<CompletionItem> computeMatches(const std::string& text, int cursor) const;

	// recompute the dropdown entries from the current input each frame
	void updateCompletionState(bool inputActive);

	// insert the highlighted entry into the input buffer (Enter / right arrow /
	// external accepts); Tab accepts inline through the InputText callback.
	void acceptCompletion();

	// draw the floating suggestion list just above the input box
	void drawCompletionPopup(const ImVec2& inputMin, const ImVec2& inputMax);

	// accept the highlighted entry from inside the InputText callback (Tab)
	void handleCompletion(ImGuiInputTextCallbackData* data);

};


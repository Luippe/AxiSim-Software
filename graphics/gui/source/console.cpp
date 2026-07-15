#include "console.h"

#include "imgui_internal.h"

#include <sstream>
#include <iomanip>
#include <format>
#include <algorithm>

#include "clipboard.h"

#include "project.h"
#include "gui.h"

#include "scene_view.h"

#include "file_manager.h"
#include "flag_manager.h"
#include "keyboard_manager.h"

#include "printer.h"
#include "console_keywords.h"

using namespace UIInputTextFlags;

// parse words separated by space
std::vector<std::string> parseWords(const std::string& line) {

	std::vector<std::string> words;
	std::istringstream iss(line);

	std::string word;
	while (iss >> word) {
		words.push_back(word);
	}
	return words;
}


// format a double to a specific precision and return it as a string
std::string formatPrecision(double value) {
	//std::ostringstream ss;

	//ss << std::fixed << std::setprecision(precision) << value;
	std::string s = std::format("{:.2f}", value);

	// Remove trailing zeros
	while (!s.empty() && s.back() == '0') {
		s.pop_back();
	}

	// Remove trailing decimal point
	if (!s.empty() && s.back() == '.') {
		s.pop_back();
	}

	return s;
}


Console::Console(GUI& gui, Project& project) :
	project(project),
	gui(gui){

	// initialize all commands and set the font
	registerCommands();

}

void Console::addCommand(const std::string& name, CommandFn function, const std::string& usage, const std::string& description, std::vector<std::string> objects, std::unordered_map<std::string, std::vector<std::string>> values) {
	commands[name] = Command{ function, usage, description, std::move(objects), std::move(values) };
}

std::vector<std::string> Console::colormapNames() const {
	std::vector<std::string> names;

	auto& cmap = gui.scene.colormap;
	for (int i = 0; i < IM_ARRAYSIZE(cmap.items); i++) {
		names.push_back(cmap.items[i]);
	}

	return names;
}

void Console::checkAutoScroll() {
	if (autoScroll) {
		scrollToBottom = true;
	}
}

std::string Console::getWord(const std::vector<std::string>& words, size_t index) {
	if (index < words.size()) {
		return words[index];
	}

	return "";
}

void Console::registerCommands() {
	registerSetCommands();
	registerShowCommands();
	registerGetCommands();
	registerCopyCommands();
	registerSaveAndLoadCommands();
	registerUtilityCommands();
}

void Console::registerShowCommands() {

	//addCommand("show", [this](const std::vector<std::string>& words) {

	//	std::string object = getWord(words, 1);


	//	if (object == "show") {

	//	}
	//	});


}

void Console::registerSetCommands() {


	addCommand("set", [this](const std::vector<std::string>& words) {

		SceneView& scene = gui.scene;


		std::string object = getWord(words, 1);
		std::string value = getWord(words, 2);


		// check which colormap to set
		if (object == "colormap" || object == "cmap") {
			bool found = false;

			for (int i = 0; i < IM_ARRAYSIZE(scene.colormap.items); i++) {
				if (value == scene.colormap.items[i]) {
					scene.colormap.setColormap(i);
					addLine("Set colormap to " + value);
					found = true;
					break;
				}
			}

			if (!found) {
				addLine("Invalid colormap: " + value);
			}
		}
		else {
			addLine("Invalid argument");
		}
		},
		"set <object> <value>",
		"Sets a program setting",
		{ "colormap", "cmap" },
		{
			{ "colormap", colormapNames() },
			{ "cmap", colormapNames() }
		}
	);
}

void Console::registerGetCommands() {
	addCommand("get", [this](const std::vector<std::string>& words) {

		SceneView& scene = gui.scene;

		std::string object = getWord(words, 1);
		std::string value = getWord(words, 2);

		if (object == "gpu") {
			if (value == "memory" || value == "mem") {
				size_t freeMem = 0;
				size_t totalMem = 0;

				cudaError_t err = cudaMemGetInfo(&freeMem, &totalMem);

				if (err != cudaSuccess) {
					addLine("Failed to fetch gpu memory");
					return;
				}

				double freeMB = freeMem / (1024.0 * 1024.0);
				double totalMB = totalMem / (1024.0 * 1024.0);
				double usedMB = totalMB - freeMB;

				addLine("Free: " + formatPrecision(freeMB) + "MB");
				addLine("Total: " + formatPrecision(totalMem) + "MB");
				addLine("Used: " + formatPrecision(usedMB) + "MB");
			}
			else {
				addLine("Invalid value: " + value);
			}
		}
		else if (object == "colormap") {

			if (value.empty()) {
				addLine("Current colormap: " + scene.colormap.getColormap());
			}
			else {
				const auto& cmap  = scene.colormap.getColormapValue(value);

				if (scene.colormap.isBlankColormap(cmap)) {
					addLine("Unknown colormap: " + value);
					return;
				}
				for (int i = 0; i < 256; i++) {
					std::ostringstream ss;

					ss << "{ "
						<< std::setw(3) << (int)cmap[i][0] << ", "
						<< std::setw(3) << (int)cmap[i][1] << ", "
						<< std::setw(3) << (int)cmap[i][2] << " }";

					addLine((ss.str()));
				}
			}
		}
		else {
			addLine("Invalid object: " + object);
		}
		},
		"get <object> <value>",
		"display a program setting",
		{ "gpu", "colormap" },
		{
			{ "gpu", { "memory", "mem" } },
			{ "colormap", colormapNames() }
		}
	);
}

void Console::registerCopyCommands() {
	addCommand("copy", [this](const std::vector<std::string>& words) {

		SceneView& scene = gui.scene;

		std::string object = getWord(words, 1);
		std::string value = getWord(words, 2);

		if (object == "residual") {
			gui.residualPlot.consoleCopy = true;
		}
		else if (object == "mesh") {
			gui.meshInspector.consoleCopy = true;
		}
		else if (object == "inspector") {
			gui.inspector.consoleCopy = true;
		}
		else if (object == "colormap") {

			std::ostringstream ss;

			const auto& cmap = value.empty() ? scene.colormap.getColormapValue() : scene.colormap.getColormapValue(value);

			for (int i = 0; i < 256; i++) {
				ss << "{ "
					<< std::setw(3) << (int)cmap[i][0] << ", "
					<< std::setw(3) << (int)cmap[i][1] << ", "
					<< std::setw(3) << (int)cmap[i][2] << " }\n";
			}
			copyTextToClipboard(ss.str());
			addLine("copied current RGB values to clipboard");
		}
		else {
			addLine("Invalid object: " + object);
		}
		},
		"copy <object>",
		"copies object to clipboard",
		{ "residual", "mesh", "inspector", "colormap" },
		{
			{ "colormap", colormapNames() }
		}
	);
}

void Console::registerSaveAndLoadCommands() {
	addCommand("save", [this](const std::vector<std::string>& words) {
		std::string object = getWord(words, 1);
		if (object == "project") {
			saveFromExplorerProject(project);
		}
		else {
			addLine("Invalid object: " + object);
		}
		},
		"save <object>",
		"save object to folder",
		{ "project" }
	);

	addCommand("load", [this](const std::vector<std::string>& words) {
		std::string object = getWord(words, 1);

		if (object == "project") {
			loadFromExplorerProject(project);
			project.mesh.updateAfterLoadingFile();
		}
		else {
			addLine("Invalid object: " + object);
		}
		},
		"load <object>",
		"load object from folder",
		{ "project" }
	);

}

void Console::registerUtilityCommands() {
	auto clearCommand = [this](const std::vector<std::string>& words) {
		clear();
	};


	addCommand("clear", clearCommand, "clear", "Clears the console");
	addCommand("clr", clearCommand, "clear", "Clears the console");

	addCommand("help", [this](const std::vector<std::string>& words) {
		addLine("Available commands:");

		for (const auto& [name, command] : commands) {
			addLine(" " + command.usage + " - " + command.description);
		}
		},
		"help",
		"Shows all available commands"
	);

	addCommand("tutorial", [this](const std::vector<std::string>& words) {
		gui.showingTutorial = !gui.showingTutorial;
		},
		"show tutorial",
		"Shows tutorial for AxiSim"
	);

	addCommand("ping", [this](const std::vector<std::string>& words) {
		addLine("pong!");
		},
		"pong!",
		"pong!"
	);
}

void Console::addLine(const std::string& s) {

	lines.push_back({ s });
	checkAutoScroll();
}

void Console::addCompletionMessage(const std::string& s) {
	lines.push_back({ "		" + s });
	checkAutoScroll();
}

void Console::addCompletionTime(const std::string& object, float& ms) {
	addSeparator();
	addLine(object + " completed in " + std::to_string(ms) + "ms");
	addSeparator();
}



void Console::addSeparator() {
	lines.push_back({ "-----------------------------------------" });
	checkAutoScroll();
}

void Console::clear() {
	lines.clear();
}

void Console::executeCommand(const std::string& cmd) {

	std::vector<std::string> words = parseWords(cmd);

	if (words.empty()) return;

	addLine("> " + cmd);

	std::string action = words[0];

	auto it = commands.find(action);
	if (it != commands.end()) {
		it->second.run(words);
	}
	else {
		addLine("Unknown command: " + action);
		addLine("Type 'help' for available commands.");
	}
}

int Console::textEditCallbackStub(ImGuiInputTextCallbackData* data) {
	Console* console = static_cast<Console*>(data->UserData);
	return console->textEditCallback(data);
}

int Console::textEditCallback(ImGuiInputTextCallbackData* data) {
	if (data->EventFlag == ImGuiInputTextFlags_CallbackAlways) {
		inputCursorPos = data->CursorPos;

		// after an external accept, snap the caret to the end and clear the
		// selection that re-focusing would otherwise create
		if (resetInputCursor) {
			data->CursorPos = data->BufTextLen;
			data->SelectionStart = data->BufTextLen;
			data->SelectionEnd = data->BufTextLen;
			inputCursorPos = data->BufTextLen;
			resetInputCursor = false;
		}
	}
	else if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion) {
		handleCompletion(data);
	}
	else if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory) {

		// while the dropdown is open, up/down move the highlight instead of
		// recalling command history
		if (completionActive && !completionItems.empty()) {
			if (data->EventKey == ImGuiKey_UpArrow) {
				completionIndex--;
				if (completionIndex < 0) {
					completionIndex = (int)completionItems.size() - 1;
				}
			}
			else if (data->EventKey == ImGuiKey_DownArrow) {
				completionIndex++;
				if (completionIndex >= (int)completionItems.size()) {
					completionIndex = 0;
				}
			}

			completionNavigated = true;
			return 0;
		}

		const int prevHistoryPos = historyPos;

		if (data->EventKey == ImGuiKey_UpArrow) {
			if (historyPos == -1) {
				historyPos = (int)commandHistory.size() - 1;
			}
			else if (historyPos > 0) {
				historyPos--;
			}
		}
		else if (data->EventKey == ImGuiKey_DownArrow) {
			if (historyPos != -1) {
				if (++historyPos >= (int)commandHistory.size()) {
					historyPos = -1;
				}
			}
		}

		if (prevHistoryPos != historyPos) {
			const char* historyStr =
				(historyPos >= 0) ? commandHistory[historyPos].c_str() : "";

			data->DeleteChars(0, data->BufTextLen);
			data->InsertChars(0, historyStr);
		}
	}

	return 0;
}

// Find the token being completed and which argument slot it is. The token runs
// from the previous space up to the cursor; the count of fully-typed words
// before it decides whether we're completing the action (0) or its object (1).
Console::CompletionContext Console::getCompletionContext(
	const std::string& text,
	int cursor
) const {
	CompletionContext ctx;

	int end = cursor;
	if (end > (int)text.size()) end = (int)text.size();
	if (end < 0) end = 0;

	int start = end;
	while (start > 0 && text[start - 1] != ' ' && text[start - 1] != '\t') {
		start--;
	}

	ctx.wordStart = start;
	ctx.wordEnd = end;
	ctx.partial = text.substr(start, end - start);
	ctx.wordIndex = (int)parseWords(text.substr(0, start)).size();

	return ctx;
}

// Build the dropdown entries that match the token at the cursor: action names
// for the first word, the action's objects for the second.
std::vector<Console::CompletionItem> Console::computeMatches(
	const std::string& text,
	int cursor
) const {
	CompletionContext ctx = getCompletionContext(text, cursor);
	std::vector<std::string> priorWords = parseWords(text.substr(0, ctx.wordStart));

	std::vector<CompletionItem> items;

	auto startsWith = [&](const std::string& candidate) {
		return candidate.size() >= ctx.partial.size() &&
			candidate.compare(0, ctx.partial.size(), ctx.partial) == 0;
		};

	if (ctx.wordIndex == 0) {
		for (const auto& [name, command] : commands) {
			if (startsWith(name)) {
				items.push_back({ name, command.description });
			}
		}
	}
	else if (ctx.wordIndex == 1 && !priorWords.empty()) {
		auto it = commands.find(priorWords[0]);
		if (it != commands.end()) {
			for (const std::string& obj : it->second.objects) {
				if (startsWith(obj)) {
					items.push_back({ obj, "" });
				}
			}
		}
	}
	else if (ctx.wordIndex == 2 && priorWords.size() >= 2) {
		// completing the value: candidates are the values registered for the
		// action's object (e.g. "set colormap <name>")
		auto it = commands.find(priorWords[0]);
		if (it != commands.end()) {
			auto vit = it->second.values.find(priorWords[1]);
			if (vit != it->second.values.end()) {
				for (const std::string& val : vit->second) {
					if (startsWith(val)) {
						items.push_back({ val, "" });
					}
				}
			}
		}
	}

	std::sort(
		items.begin(),
		items.end(),
		[](const CompletionItem& a, const CompletionItem& b) {
			return a.word < b.word;
		}
	);

	return items;
}

// Recompute the dropdown every frame from the current input, and decide whether
// it should be visible.
void Console::updateCompletionState(bool inputActive) {
	std::string text = inputBuffer;
	int cursor = (int)text.size();

	completionItems = computeMatches(text, cursor);

	if (completionIndex >= (int)completionItems.size()) completionIndex = 0;
	if (completionIndex < 0) completionIndex = 0;

	CompletionContext ctx = getCompletionContext(text, cursor);

	// first token (the command) needs at least one typed character, so an empty
	// input stays closed; later tokens (object / value) list candidates even on
	// an empty token, e.g. "set colormap " shows the colormap names
	bool showRule = !ctx.partial.empty() || ctx.wordIndex > 0;

	// showRule is a hard requirement: with an empty token there is nothing to
	// match, so the list stays closed even if Ctrl+Space was pressed. force only
	// lifts the dismissed latch (re-open after an accept/delete on a typed token).
	completionActive = inputActive && !completionItems.empty() &&
		showRule && (!completionDismissed || forceCompletion);
}

// Tab: accept the highlighted entry inline, from inside the InputText callback
// (editing through the callback keeps ImGui's internal buffer in sync).
void Console::handleCompletion(ImGuiInputTextCallbackData* data) {
	if (completionItems.empty()) return;
	if (completionIndex < 0 || completionIndex >= (int)completionItems.size()) return;

	std::string text(data->Buf, data->BufTextLen);
	CompletionContext ctx = getCompletionContext(text, data->CursorPos);

	const std::string& word = completionItems[completionIndex].word;

	data->DeleteChars(ctx.wordStart, ctx.wordEnd - ctx.wordStart);
	data->InsertChars(ctx.wordStart, word.c_str());

	completionActive = false;
	completionNavigated = false;
	completionDismissed = true;

	// this Tab edit changed the buffer from inside the callback; flag it so the
	// change-detector in draw() keeps the list closed instead of mistaking it
	// for user typing and re-opening the dropdown
	completionJustAccepted = true;
}

// Enter / right-arrow: accept the highlighted entry. These happen outside the
// InputText callback, so edit our buffer and force the widget to reload it.
void Console::acceptCompletion() {
	if (completionItems.empty()) return;
	if (completionIndex < 0 || completionIndex >= (int)completionItems.size()) return;

	std::string text = inputBuffer;
	int cursor = (int)text.size();
	CompletionContext ctx = getCompletionContext(text, cursor);

	const std::string& word = completionItems[completionIndex].word;

	std::string newText =
		text.substr(0, ctx.wordStart) + word + text.substr(ctx.wordEnd);

	if (newText.size() >= sizeof(inputBuffer)) {
		newText.resize(sizeof(inputBuffer) - 1);
	}

	std::snprintf(inputBuffer, sizeof(inputBuffer), "%s", newText.c_str());

	completionActive = false;
	completionNavigated = false;
	completionDismissed = true;

	// this programmatic edit changed the buffer; sync lastInput so draw() does
	// not mistake it for user typing and re-open the dropdown
	lastInput = inputBuffer;

	// While active, InputText keeps its own copy of the text, so an external
	// edit is ignored. Drop the active id and re-focus next frame to reload,
	// and reset the caret to the end (avoids the default select-all).
	ImGui::ClearActiveID();
	refocusInput = true;
	resetInputCursor = true;
}

// Floating suggestion list drawn just above the input box.
void Console::drawCompletionPopup(const ImVec2& inputMin, const ImVec2& inputMax) {
	if (completionItems.empty()) return;

	ImDrawList* dl = ImGui::GetForegroundDrawList();
	ImFont* font = ImGui::GetFont();
	float fontSize = ImGui::GetFontSize();

	const float padX = 8.0f;
	const float padY = 4.0f;
	const float rowH = fontSize + 6.0f;
	const int maxVisible = 8;

	int count = (int)completionItems.size();
	int visible = count < maxVisible ? count : maxVisible;

	// keep the highlighted row within the visible window
	int firstRow = 0;
	if (completionIndex >= maxVisible) {
		firstRow = completionIndex - maxVisible + 1;
	}

	// size the box to the widest "word   description"
	float wordColW = 0.0f;
	float descColW = 0.0f;
	for (const CompletionItem& item : completionItems) {
		wordColW = std::max(wordColW, ImGui::CalcTextSize(item.word.c_str()).x);
		if (!item.description.empty()) {
			descColW = std::max(descColW, ImGui::CalcTextSize(item.description.c_str()).x);
		}
	}

	float gap = descColW > 0.0f ? 24.0f : 0.0f;
	float contentW = wordColW + gap + descColW + padX * 2.0f + 12.0f;
	float inputW = inputMax.x - inputMin.x;
	float width = std::min(std::max(inputW, contentW), 640.0f);

	float height = visible * rowH + padY * 2.0f;

	ImVec2 boxMin(inputMin.x, inputMin.y - height - 4.0f);
	ImVec2 boxMax(inputMin.x + width, inputMin.y - 4.0f);

	// not enough room above -> drop the list below the input instead
	if (boxMin.y < 0.0f) {
		boxMin.y = inputMax.y + 4.0f;
		boxMax.y = boxMin.y + height;
	}

	dl->AddRectFilled(boxMin, boxMax, IM_COL32(28, 32, 40, 250), 4.0f);
	dl->AddRect(boxMin, boxMax, IM_COL32(90, 110, 140, 220), 4.0f, 0, 1.0f);

	for (int r = 0; r < visible; r++) {
		int i = firstRow + r;
		if (i >= count) break;

		const CompletionItem& item = completionItems[i];

		ImVec2 rowMin(boxMin.x + 2.0f, boxMin.y + padY + r * rowH);
		ImVec2 rowMax(boxMax.x - 2.0f, rowMin.y + rowH);

		if (i == completionIndex) {
			dl->AddRectFilled(rowMin, rowMax, IM_COL32(56, 92, 140, 255), 3.0f);
		}

		float textY = rowMin.y + (rowH - fontSize) * 0.5f;

		ImVec2 wordPos(rowMin.x + padX, textY);
		dl->AddText(font, fontSize, wordPos, IM_COL32(236, 239, 245, 255), item.word.c_str());

		if (!item.description.empty()) {
			ImVec2 descPos(rowMin.x + padX + wordColW + gap, textY);
			dl->AddText(font, fontSize, descPos, IM_COL32(150, 158, 172, 255), item.description.c_str());
		}
	}
}

void Console::draw() {

	ImGui::Begin("Console");

	ImFont* defaultFont = gui.appConfig.fonts.defaultFont;

	ImGui::PushFont(defaultFont);
	ImGui::BeginChild(
		"ConsoleOutput",
		ImVec2(0, -ImGui::GetFrameHeightWithSpacing()),
		false,
		ImGuiWindowFlags_HorizontalScrollbar
	);


	for (const std::string& line : lines) {
		ImGui::TextUnformatted(line.c_str());
	}


	if (scrollToBottom) {
		ImGui::SetScrollHereY(1.0f);
		scrollToBottom = false;
	}

	ImGui::EndChild();

	// clicking anywhere in the console (output area, padding, or dead space) reclaims
	// keyboard focus for the input so you can type immediately; skip clicks on the
	// input line itself so those place the caret where clicked instead of at the end.
	// The focus request is deferred to just after the InputText below, where
	// SetKeyboardFocusHere(-1) reliably wins over the child window that captured the
	// click -- focusing before the widget (as this used to) loses that race.
	bool clickToFocusInput =
		ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
		ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
		!ImGui::IsMouseHoveringRect(lastInputMin, lastInputMax);

	// re-focus the input after a completion accept that had to reset the widget
	if (refocusInput) {
		ImGui::SetKeyboardFocusHere();
		refocusInput = false;
	}

	ImGui::SetNextItemWidth(-FLT_MIN);
	bool submitted = ImGui::InputText(
		"##Console",
		inputBuffer,
		sizeof(inputBuffer),
		ConsoleInputFlags,
		&Console::textEditCallbackStub,
		this
	);

	ImVec2 inputMin = ImGui::GetItemRectMin();
	ImVec2 inputMax = ImGui::GetItemRectMax();
	bool inputActive = ImGui::IsItemActive();

	// remember the input box rect so next frame's click-to-focus can exclude it
	lastInputMin = inputMin;
	lastInputMax = inputMax;

	// apply the click-to-focus detected above, now that the InputText item exists.
	// SetKeyboardFocusHere(-1) targets the item submitted just above (the input).
	if (clickToFocusInput) {
		ImGui::SetKeyboardFocusHere(-1);
		resetInputCursor = true;	// snap the caret to the end on the reclaimed focus
	}

	// reset the highlight whenever the text actually changes
	if (lastInput != inputBuffer) {
		completionIndex = 0;
		completionNavigated = false;
		forceCompletion = false;

		if (completionJustAccepted) {
			// the buffer changed because a completion was inserted (Tab), not
			// because the user typed: keep the list closed
			completionDismissed = true;
		}
		else {
			// deleting characters should not pop the dropdown; only forward
			// typing (or an explicit Ctrl+Space) does
			completionDismissed = std::string(inputBuffer).size() < lastInput.size();
		}
	}
	completionJustAccepted = false;
	lastInput = inputBuffer;

	// Left Ctrl + Space force-opens the candidate list (even on empty input)
	if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) &&
		ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
		completionDismissed = false;
		forceCompletion = true;
	}

	// snapshot the dropdown's open state: pressing Enter deactivates the
	// InputText, so updateCompletionState() below clears completionActive this
	// frame before the submit check can tell that the user was navigating.
	bool completionWasActive = completionActive;

	updateCompletionState(inputActive);

	// right arrow at the end of the line accepts the highlighted entry
	bool caretAtEnd = inputCursorPos >= (int)std::string(inputBuffer).size();
	if (completionActive && caretAtEnd &&
		ImGui::IsKeyPressed(ImGuiKey_RightArrow, false)) {
		acceptCompletion();
		submitted = false;
	}

	if (submitted) {
		if (completionWasActive && completionNavigated) {
			// a dropdown entry is highlighted: accept it instead of running
			acceptCompletion();
		}
		else {
			std::string cmd = inputBuffer;
			scrollToBottom = true;
			if (!cmd.empty()) {
				executeCommand(cmd);
				if (commandHistory.empty() || commandHistory.back() != cmd) {
					commandHistory.push_back(cmd);
				}
			}
			inputBuffer[0] = '\0';
			historyPos = -1;
			lastInput.clear();
			completionActive = false;
			ImGui::SetKeyboardFocusHere(-1);
		}
	}

	if (completionActive) {
		drawCompletionPopup(inputMin, inputMax);
	}

	ImGui::PopFont();
	ImGui::End();
}
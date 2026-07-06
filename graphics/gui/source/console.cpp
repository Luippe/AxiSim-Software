#include "console.h"

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

#include "printer.h"
#include "console_keywords.h"

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

void Console::addCommand(const std::string& name, CommandFn function, const std::string& usage, const std::string& description, std::vector<std::string> objects) {
	commands[name] = Command{ function, usage, description, std::move(objects) };
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
	registerGetCommands();
	registerCopyCommands();
	registerSaveAndLoadCommands();
	registerUtilityCommands();
}

void Console::registerSetCommands() {


	addCommand("set", [this](const std::vector<std::string>& words) {

		SceneView& scene = gui.scene;


		std::string object = getWord(words, 1);
		std::string value = getWord(words, 2);

		// set shading
		
		// set precision

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
		{ "colormap", "cmap" }
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
		{ "gpu", "colormap" }
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
			addLine("copied RGB values to clipboard");
		}
		else {
			addLine("Invalid object: " + object);
		}
		},
		"copy <object>",
		"copies object to clipboard",
		{ "residual", "mesh", "inspector", "colormap" }
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
	if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion) {
		handleCompletion(data);
	}
	else if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory) {

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

void Console::handleCompletion(ImGuiInputTextCallbackData* data) {

	// The word being completed runs from the start of the current token up to
	// the cursor. Scan back from the cursor to the previous space to find it.
	const char* bufBegin = data->Buf;
	const char* wordEnd = data->Buf + data->CursorPos;
	const char* wordStart = wordEnd;

	while (wordStart > bufBegin && wordStart[-1] != ' ' && wordStart[-1] != '\t') {
		wordStart--;
	}

	std::string partial(wordStart, wordEnd);

	// Words fully typed before the current one decide which token this is:
	// 0 words before -> completing the action, 1 -> completing its object.
	std::vector<std::string> priorWords = parseWords(std::string(bufBegin, wordStart));
	int wordIndex = (int)priorWords.size();

	std::vector<std::string> candidates;

	if (wordIndex == 0) {
		// completing the action: candidates are all command names
		for (const auto& [name, command] : commands) {
			candidates.push_back(name);
		}
	}
	else if (wordIndex == 1) {
		// completing the object: candidates are the action's registered objects
		auto it = commands.find(priorWords[0]);
		if (it != commands.end()) {
			candidates = it->second.objects;
		}
	}

	// keep only candidates that start with what the user has typed so far
	std::vector<std::string> matches;
	for (const std::string& candidate : candidates) {
		if (candidate.size() >= partial.size() &&
			candidate.compare(0, partial.size(), partial) == 0) {
			matches.push_back(candidate);
		}
	}

	if (matches.empty()) {
		return;
	}

	std::sort(matches.begin(), matches.end());

	// completion text: the whole word for a single match, otherwise the longest
	// common prefix of all matches (fill in as far as it stays unambiguous)
	std::string completion = matches[0];
	for (size_t m = 1; m < matches.size(); m++) {
		size_t k = 0;
		while (k < completion.size() && k < matches[m].size() &&
			completion[k] == matches[m][k]) {
			k++;
		}
		completion.resize(k);
	}

	// replace the partial word with the completion
	int start = (int)(wordStart - bufBegin);
	int end = (int)(wordEnd - bufBegin);

	data->DeleteChars(start, end - start);
	data->InsertChars(start, completion.c_str());

	if (matches.size() == 1) {
		// unambiguous: finish the word and start the next argument
		data->InsertChars(data->CursorPos, " ");
	}
	else {
		// several options: show them so the user can pick
		std::string list;
		for (const std::string& match : matches) {
			list += "  " + match;
		}

		addLine("> " + std::string(bufBegin, wordEnd));
		addLine(list);
		scrollToBottom = true;
	}
}

void Console::draw() {

	ImGui::Begin("Console");

	ImFont* defaultFont = gui.appConfig.fonts.defaultFont;

	if (ImGui::Button("Clear")) {
		clear();
	}

	ImGui::SameLine();
	ImGui::Checkbox("Auto scroll", &autoScroll);
	ImGui::Separator();

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

	ImGui::SetNextItemWidth(-FLT_MIN);
	if (ImGui::InputText("##Console", inputBuffer, sizeof(inputBuffer), UIFlags::ConsoleInputFlags, &Console::textEditCallbackStub, this)) {
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
		ImGui::SetKeyboardFocusHere(-1);
	}

	ImGui::PopFont();
	ImGui::End();
}
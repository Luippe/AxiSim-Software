#include "console.h"

#include <sstream>
#include <iomanip>
#include <format>

#include "scene_view.h"
#include "console_keywords.h"
#include "imgui.h"
#include "printer.h"
#include "gui.h"

#include "manage_file.h"
#include "gui_manager.h"

struct InputFocusData {
	bool moveCursorToEnd = false;
};
struct MultilineScrollData {
	bool scrollToBottom = false;
};

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


Console::Console(GUI& gui, SceneView& scene) :
	scene(scene),
	gui(gui){
	registerCommands();
}

void Console::addCommand(const std::string& name, CommandFn function, const std::string& usage, const std::string& description) {
	commands[name] = Command{ function, usage, description };
}

std::string Console::getWord(const std::vector<std::string>& words, size_t index) {
	if (index < words.size()) {
		return words[index];
	}

	return "";
}

void Console::registerCommands() {
	registerRunCommands();
	registerSetCommands();
	registerGetCommands();
	registerCopyCommands();
	registerSaveAndLoadCommands();
	registerUtilityCommands();
}

void Console::registerRunCommands() {
	addCommand("run", [this](const std::vector<std::string>& words) {
		std::string object = getWord(words, 1);
			
		if (object == "mesh") {
			gui.meshGUI.setGridConfigEdits();
			scene.mesh.generate();
		}
		else if (object == "solver") {
			scene.solver.run();
		}
		else if (object == "results") {
			scene.results.generate();
			gui.inspector.generate();
		}
		else {
			addLine("Invalid argument");
		}
		},
		"run <mesh|solver|results>",
		"Runs a major program operation"
	);
}

void Console::registerSetCommands() {
	addCommand("set", [this](const std::vector<std::string>& words) {
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
					gui.inspector.generate();
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
		"Sets a program setting"
	);
}

void Console::registerGetCommands() {
	addCommand("get", [this](const std::vector<std::string>& words) {
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
			addLine("Current colormap: " + scene.colormap.getColormap());
		}
		else {
			addLine("Invalid object: " + object);
		}
		},
		"get <object> <value>",
		"display a program setting"
	);
}

void Console::registerCopyCommands() {
	addCommand("copy", [this](const std::vector<std::string>& words) {
		std::string object = getWord(words, 1);

		if (object == "residual") {
			gui.residualPlot.consoleCopy = true;
		}
		else if (object == "inspector") {
			gui.inspector.consoleCopy = true;
		}
		else {
			addLine("Invalid object: " + object);
		}
		},
		"copy <object>",
		"copies object to clipboard"
	);
}

void Console::registerSaveAndLoadCommands() {
	addCommand("save", [this](const std::vector<std::string>& words) {
		std::string object = getWord(words, 1);

		if (object == "mesh") {
			saveFromExplorerMesh(scene.mesh);
		}
		else if (object == "solver") {
			saveFromExplorerSolver(scene.solver);
		}
		else if (object == "residual") {
		
		}
		else {
			addLine("Invalid object: " + object);
		}
		},
		"save <object>",
		"save object to folder"
	);

	addCommand("load", [this](const std::vector<std::string>& words) {
		std::string object = getWord(words, 1);

		if (object == "mesh") {
			loadFromExplorerMesh(scene.mesh);
			scene.mesh.updateAfterLoadingFile();
			gui.meshGUI.getGridConfigEdits();
		}
		else if (object == "solver") {
			loadFromExplorerSolver(scene.solver);
		}
		else if (object == "residual") {

		}
		else {
			addLine("Invalid object: " + object);
		}
		},
		"load <object>",
		"load object from folder"
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
	scrollToBottom = true;
}

void Console::addCompletionMessage(const std::string& s) {
	lines.push_back({ "		" + s });
	scrollToBottom = true;
}

void Console::addCompletionTime(const std::string& object, float& ms) {
	addSeparator();
	addLine(object + " completed in " + std::to_string(ms) + "ms");
	addSeparator();
}

void Console::addSeparator() {
	lines.push_back({ "-----------------------------------------" });
	scrollToBottom = true;
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

void Console::handleEvents() {

}

static int inputCallback(ImGuiInputTextCallbackData* data) {
	InputFocusData* focusData = (InputFocusData*)(data->UserData);

	if (focusData->moveCursorToEnd) {
		data->CursorPos = data->BufTextLen;
		data->SelectionStart = data->BufTextLen;
		data->SelectionEnd = data->BufTextLen;

		focusData->moveCursorToEnd = false;
	}

	return 0;
}

static int MultilineScrollCallback(ImGuiInputTextCallbackData* data) {
	auto* scrollData = static_cast<MultilineScrollData*>(data->UserData);

	if (scrollData->scrollToBottom) {

		data->CursorPos = data->BufTextLen;
		data->SelectionStart = data->BufTextLen;
		data->SelectionEnd = data->BufTextLen;

		scrollData->scrollToBottom = false;
	}

	return 0;
}

void appendCharToInput(char* buf, size_t bufSize, ImWchar c) {
	if (c < 128) { // ASCII only
		size_t len = std::strlen(buf);

		if (len + 1 < bufSize) {
			buf[len] = (char)(c);
			buf[len + 1] = '\0';
		}
	}
};

void detectCharacterPress(ImGuiIO& io, static bool& outputWasFocused, ImWchar& forwardedChar, bool& focusInput) {
	// detect character BEFORE drawing the input widgets
	if (outputWasFocused && !io.KeyCtrl && !io.KeyAlt && !io.KeySuper) {
		for (int i = 0; i < io.InputQueueCharacters.Size; i++) {
			ImWchar c = io.InputQueueCharacters[i];

			if (c >= 32 && c != 127) {
				forwardedChar = c;
				focusInput = true;
			}
		}
	}
}

void drawOutput() {

}

void Console::draw() {

	ImGui::Begin("Console");

	ImGuiIO& io = ImGui::GetIO();

	if (ImGui::Button("Clear")) {
		clear();
	}

	ImGui::SameLine();
	ImGui::Checkbox("Auto scroll", &autoScroll);
	ImGui::Separator();

	ImGui::BeginChild(
		"ConsoleOutput",
		ImVec2(0, -ImGui::GetFrameHeightWithSpacing()),
		false,
		ImGuiWindowFlags_HorizontalScrollbar
	);

	for (const std::string& line : lines) {
		ImGui::TextUnformatted(line.c_str());
	}

	if (scrollToBottom || autoScroll) {
		ImGui::SetScrollHereY(1.0f);
		scrollToBottom = false;
	}

	ImGui::EndChild();
	char buf[256] = {};
	if (ImGui::InputText("##Console", buf, sizeof(buf), UIFlags::ConsoleInputFlags)) {
		std::string cmd = buf;
		scrollToBottom = true;
		if (!cmd.empty()) {
			executeCommand(cmd);
		}
		buf[0] = '\0';
		ImGui::SetKeyboardFocusHere(-1);
	}

	ImGui::End();
}

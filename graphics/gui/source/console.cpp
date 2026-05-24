#include "console.h"

#include <sstream>
#include <iomanip>
#include <format>

#include "clip.h"

#include "scene_view.h"
#include "console_keywords.h"
#include "printer.h"
#include "gui.h"

#include "file_manager.h"
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
		"display a program setting"
	);
}

void Console::registerCopyCommands() {
	addCommand("copy", [this](const std::vector<std::string>& words) {
		std::string object = getWord(words, 1);
		std::string value = getWord(words, 2);

		if (object == "residual") {
			gui.residualPlot.consoleCopy = true;
			addLine("copied to clipboard");
		}
		else if (object == "inspector") {
			gui.inspector.consoleCopy = true;
			addLine("copied to clipboard");
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
			clip::set_text(ss.str());
			addLine("copied to clipboard");
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

int Console::textEditCallbackStub(ImGuiInputTextCallbackData* data) {
	Console* console = static_cast<Console*>(data->UserData);
	return console->textEditCallback(data);
}

int Console::textEditCallback(ImGuiInputTextCallbackData* data) {
	if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory) {

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

void Console::handleEvents() {

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

	ImGui::End();
}

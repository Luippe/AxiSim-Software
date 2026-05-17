#include "console.h"

#include <sstream>
#include <iomanip>
#include <format>

#include "scene_view.h"
#include "console_keywords.h"
#include "imgui.h"
#include "printer.h"
#include "gui.h"

#include "gui_manager.h"

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
		"set <object> <value>",
		"Sets a program setting"
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

void Console::draw() {

	ImGui::Begin("Console");

	//if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) &&
	//	ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
	//	focusInputNextFrame = true;
	//}

	ImGuiIO& io = ImGui::GetIO();

	if (ImGui::Button("Clear")) {
		clear();
	}

	ImGui::SameLine();
	ImGui::Checkbox("Auto scroll", &autoScroll);
	ImGui::Separator();

	ImGui::BeginChild("ScrollingRegion", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), false, ImGuiWindowFlags_HorizontalScrollbar);

	for (const auto& line : lines) {
		ImGui::TextUnformatted(line.text.c_str());
	}

	// autoscroll
	if (scrollToBottom || (autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())) {
		ImGui::SetScrollHereY(1.0f);
	}

	scrollToBottom = false;

	ImGui::EndChild();
	ImGui::Separator();
	//if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
	//	ImGui::SetKeyboardFocusHere();
	//}

	char buf[256] = {};
	bool reclaimFocus = false;
	std::snprintf(buf, sizeof(buf), "%s", input.c_str());
	if (ImGui::InputText(" ", buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue)) {
		std::string cmd = buf;
		if (!cmd.empty()) {
			executeCommand(cmd);
		}
		input.clear();
		reclaimFocus = true;
	}

	// once enter is pressed, go back and get focus on the input box
	ImGui::SetItemDefaultFocus();
	if (reclaimFocus) {
		ImGui::SetKeyboardFocusHere(-1);
	}
	ImGui::End();
}

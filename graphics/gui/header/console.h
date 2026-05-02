#pragma once
#include <vector>
#include <string>
#include <unordered_map>
#include <functional>

class SceneView;
class GUI;


// create a console for displaying messages and accepting user input
class Console {
public:

	Console(GUI& gui, SceneView& scene);

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
	SceneView& scene;

	struct ConsoleLine {
		std::string text;
	};

	struct Command {
		CommandFn run;
		std::string usage;
		std::string description;
	};

	std::unordered_map<std::string, Command> commands;

	std::string getWord(const std::vector<std::string>& words, size_t index);

	void registerCommands();
	void registerRunCommands();
	void registerSetCommands();
	void registerGetCommands();
	void registerUtilityCommands();

	void addCommand(const std::string& name, CommandFn function, const std::string& usage, const std::string& description);

	std::vector<ConsoleLine> lines;
	std::string input;
	bool autoScroll = true;
	bool scrollToBottom = false;
};


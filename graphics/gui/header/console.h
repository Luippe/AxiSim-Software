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

	void addCommand(const std::string& name, CommandFn function, const std::string& usage, const std::string& description);

	// check if auto scroll is on, if it is, scroll to bottom whener a line is added
	void checkAutoScroll();

	std::vector<std::string> commandHistory;
	int historyPos = -1;
	static int textEditCallbackStub(ImGuiInputTextCallbackData* data);
	int textEditCallback(ImGuiInputTextCallbackData* data);

};


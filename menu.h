#pragma once
class SceneView;
class GUI;

class Menu {
public:
	Menu(GUI& gui, SceneView& scene);

	void render();

private:

	GUI& gui;
	SceneView& scene;

	// open selected file
	void drawOpen();

	// open selected files at launch
	void drawOpenAtLaunch();
	
	// save selected object as .bin file
	void drawSave();

};
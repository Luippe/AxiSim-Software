#pragma once
#include "imgui.h"

class Colormap;
class Results;

class Colorbar {
public:
	Colorbar(Colormap& colormap, Results& results) : colormap(colormap), results(results) {};

	void render();

private:

	const float barWidth = 20.0f;
	const float barHeight = 200.0f;
	const float tickLen = 6.0f;
	const float textOffset = 4.0f;
	int numTicks = 6;
	float dy = barHeight / (numTicks);

	ImVec2 posMin;
	ImVec2 posMax;
	ImDrawList* drawList;

	Colormap& colormap;
	Results& results;

	// draw only the colorbar
	void drawBar();

	// draw only the colorbar outlines
	void drawOutline();

	// draw only the labels
	void drawLabel();

	// draw only the ticks and numbers
	void drawTicks();

	// draw values besides each tick mark
	void drawTickValue();
};
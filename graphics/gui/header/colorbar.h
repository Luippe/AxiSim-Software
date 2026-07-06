#pragma once
#include "imgui.h"
#include <vector>
#include <optional>

class Colormap;
class Results;

enum class NumberFormat {
	Fixed,
	Scientific,
	General
};

class Colorbar {
public:
	Colorbar(Colormap& colormap, Results& results) : colormap(colormap), results(results) {};

	void render();

	// size the reserved strip to fit the tick values and the label. Call this
	// before the caller reserves horizontal space for the colorbar so nothing
	// gets clipped by the panel edge.
	void updateLayout();

	float width = 100.0f;
	int currentPrecision = 3;
	const char* formatOption[3] = { "Fixed", "Scientific", "General" };
	NumberFormat currentNumberFormat = NumberFormat::General;

private:

	// stores the data when user clicks on colormap
	struct FilterTickData {
		float mouseY = 0.0f;
		double value = 0.0f;
	};

	void resetFilterValues();
	std::optional<FilterTickData> filterValueAt;
	std::optional<FilterTickData> filterValueLower;
	std::optional<FilterTickData> filterValueUpper;

	const float barWidth = 20.0f;
	const float barHeight = 200.0f;
	const float tickLen = 6.0f;
	const float textOffset = 4.0f;
	int numTicks = 6;
	float dy = barHeight / (numTicks);

	// bar's left offset inside the reserved strip. Grown by updateLayout() so the
	// centered label and the tick values stay fully inside the strip.
	float barLeftPad = 8.0f;

	ImVec2 posMin;
	ImVec2 posMax;
	ImDrawList* drawList;

	Colormap& colormap;
	Results& results;

	// handle all mouse events
	void handleMouseEvent();

	// change number display depending on current format
	void formatTickValue(char* buf, size_t bufSize, double value, int precision);

	// precision actually used for the tick values (bumped so adjacent ticks stay
	// distinguishable on large-baseline fields)
	int computeTickPrecision();

	// widest formatted tick value / widest label word, used to size the strip
	float maxTickTextWidth(int precision);
	float maxLabelLineWidth();

	// draw only the colorbar
	void drawBar();

	// draw only the colorbar outlines
	void drawOutline();

	// draw only the labels. wraps and centers the label
	void drawLabel();

	// draw only the ticks and numbers
	void drawTicks();

	// draw the filter ticks when clicking on the colorbar
	void drawFilterTicks();

	// draw values besides each tick mark
	void drawTickValue();

	// get value at a given position on colorbar
	float getValueAtY(float mouseY);
};
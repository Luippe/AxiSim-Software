#pragma once
#include "imgui.h"
#include <vector>
#include <optional>
#include <string>

class Colormap;
class Results;
struct LengthScale;
struct VariableUnits;

enum class NumberFormat {
	Fixed,
	Scientific,
	General
};

class Colorbar {
public:
	Colorbar(
		Colormap& colormap,
		Results& results,
		const VariableUnits& variableUnits,
		const LengthScale& lengthScale
	) :
		colormap(colormap),
		results(results),
		variableUnits(variableUnits),
		lengthScale(lengthScale) {}

	// Draw the colorbar onto `drawList`, positioned inside the given canvas rect at
	// the stored (normalized) position. Pure rendering with no ImGui items, so it
	// works both for the live panel and for the off-screen copy -- which is how the
	// colorbar ends up baked into images sent to the clipboard.
	void draw(ImDrawList* drawList, const ImVec2& canvasMin, const ImVec2& canvasMax);

	// Process dragging (reposition inside the canvas) and filter-tick clicks for the
	// live panel. Call this before the canvas' own mouse handling. Returns true while
	// the pointer is over / dragging the colorbar so the caller can suppress panning,
	// probing and cell selection underneath it.
	bool interact(const ImVec2& canvasMin, const ImVec2& canvasMax);

	int currentPrecision = 3;
	const char* formatOption[3] = { "Fixed", "Scientific", "General" };
	NumberFormat currentNumberFormat = NumberFormat::General;

private:

	// geometry of the colorbar for a given canvas: the gradient bar rect, plus the
	// full bounding box that also encloses the label (above) and the ticks/values
	// (to the right). The bounding box is the drag/hit target.
	struct Layout {
		ImVec2 barMin;
		ImVec2 barMax;
		ImVec2 boxMin;
		ImVec2 boxMax;
	};

	Layout computeLayout(const ImVec2& canvasMin, const ImVec2& canvasMax);

	// normalized top-left of the gradient bar within the canvas, in [0,1]^2.
	// Dragging updates this; computeLayout() clamps it so the whole colorbar stays
	// inside the canvas regardless of canvas size (live panel or export).
	ImVec2 normPos = ImVec2(0.9f, 0.3f);

	// drag bookkeeping: distinguishes a reposition drag from a filter-tick click.
	bool dragging = false;
	ImVec2 pressMouse = ImVec2(0.0f, 0.0f);

	// filter tick values chosen by clicking the bar (stored as data values so they
	// track the bar when it is moved or the range changes)
	struct FilterTickData {
		double value = 0.0;
	};

	void resetFilterValues();
	void applyFilterClick(float localY);
	std::optional<FilterTickData> filterValueAt;
	std::optional<FilterTickData> filterValueLower;
	std::optional<FilterTickData> filterValueUpper;

	const float barWidth = 20.0f;
	const float barHeight = 200.0f;
	const float tickLen = 6.0f;
	const float textOffset = 4.0f;
	const float labelGap = 5.0f;	// gap between the label block and the top of the bar
	int numTicks = 6;
	float dy = barHeight / (numTicks);

	ImVec2 posMin;
	ImVec2 posMax;
	ImDrawList* drawList = nullptr;

	Colormap& colormap;
	Results& results;
	const VariableUnits& variableUnits;
	const LengthScale& lengthScale;

	// Current field label/unit and conversion from solver-base values to the
	// project's selected display units. Filters remain stored in base units.
	std::string currentLabelText() const;
	std::string currentUnitText() const;
	double valueForDisplay(double baseValue) const;

	// change number display depending on current format
	void formatTickValue(char* buf, size_t bufSize, double value, int precision);

	// precision actually used for the tick values (bumped so adjacent ticks stay
	// distinguishable on large-baseline fields)
	int computeTickPrecision();

	// widest formatted tick value / widest label word / number of label lines,
	// used to size the bounding box
	float maxTickTextWidth(int precision);
	float maxLabelLineWidth();
	int labelLineCount();

	// draw the gradient bar via the draw list at posMin..posMax
	void drawBar();

	// draw only the colorbar outline
	void drawOutline();

	// draw only the label. wraps and centers the label above the bar
	void drawLabel();

	// draw only the ticks and numbers
	void drawTicks();

	// draw the filter ticks chosen by clicking the colorbar
	void drawFilterTicks();

	// draw values beside each tick mark
	void drawTickValue();

	// value at a given local Y on the bar (0 = top, barHeight = bottom)
	float getValueAtY(float localY);

	// absolute screen Y on the bar for a given data value (inverse of getValueAtY)
	float yForValue(double value);
};

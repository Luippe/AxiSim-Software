#include "colorbar.h"

#include <sstream>
#include <algorithm>
#include <cmath>

#include "colormap.h"
#include "results.h"

#include "printer.h"


// ======================================================================
// -----------------------HELPER FUNCTION--------------------------------
// ======================================================================
void Colorbar::formatTickValue(char* buf, size_t bufSize, double value, int precision) {

	precision = std::clamp(precision, 0, 12);

	switch (currentNumberFormat) {
	case NumberFormat::Fixed:
		snprintf(buf, bufSize, "%.*f", precision, value);
		break;

	case NumberFormat::Scientific:
		snprintf(buf, bufSize, "%.*e", precision, value);
		break;

	case NumberFormat::General:
		snprintf(buf, bufSize, "%.*g", precision, value);
		break;
	}
}

// A field can sit on a large baseline with only a small fractional variation
// across the domain (e.g. an inlet-dominated concentration field with a thin
// depletion layer). At the user's chosen precision, adjacent ticks can then
// round to the same displayed string even though the underlying values differ.
// Bump precision up (never down) so ticks stay distinguishable regardless of
// the field's magnitude.
int Colorbar::computeTickPrecision() {

	if (!results.currentField) {
		return std::clamp(currentPrecision, 0, 12);
	}

	float dval = (results.currentField->vmax - results.currentField->vmin) / numTicks;
	double refValue = std::max(std::fabs((double)(results.currentField->vmax)), std::fabs((double)(results.currentField->vmin)));

	int precision = currentPrecision;

	if (dval > 0.0f && refValue > 0.0) {
		int magRef = (int)(std::floor(std::log10(refValue)));
		int magStep = (int)(std::floor(std::log10((double)(dval))));
		precision = std::clamp(std::max(currentPrecision, magRef - magStep + 2), 0, 12);
	}

	return precision;
}

float Colorbar::maxTickTextWidth(int precision) {

	if (!results.currentField) return 0.0f;

	float dval = (results.currentField->vmax - results.currentField->vmin) / numTicks;
	float maxW = 0.0f;

	for (int i = 0; i < numTicks + 1; i++) {
		char buf[32];
		double value = results.currentField->vmax - i * dval;

		formatTickValue(buf, sizeof(buf), value, precision);
		maxW = std::max(maxW, ImGui::CalcTextSize(buf).x);
	}

	return maxW;
}

float Colorbar::maxLabelLineWidth() {

	std::string text = results.fieldType[results.currentItem];
	std::stringstream ss(text);
	std::string word;

	float maxW = 0.0f;
	while (ss >> word) {
		maxW = std::max(maxW, ImGui::CalcTextSize(word.c_str()).x);
	}

	return maxW;
}

int Colorbar::labelLineCount() {

	std::string text = results.fieldType[results.currentItem];
	std::stringstream ss(text);
	std::string word;

	int n = 0;
	while (ss >> word) {
		n++;
	}

	return std::max(n, 1);
}

// ======================================================================
// -----------------------LAYOUT-----------------------------------------
// ======================================================================
Colorbar::Layout Colorbar::computeLayout(const ImVec2& canvasMin, const ImVec2& canvasMax) {

	int precision = computeTickPrecision();
	float tickTextW = maxTickTextWidth(precision);
	float labelHalf = maxLabelLineWidth() * 0.5f;

	float lineHeight = ImGui::GetTextLineHeight();
	float labelHeight = labelLineCount() * lineHeight;

	// distances from the bar's top-left corner to each edge of the bounding box
	float leftExtent = std::max(0.0f, labelHalf - barWidth * 0.5f);	// centered label overhang
	float rightExtent = barWidth + tickLen + textOffset + tickTextW;	// ticks + values
	float topExtent = labelHeight + labelGap;						// label block above
	float bottomExtent = barHeight;								// bar itself

	float canvasW = std::max(1.0f, canvasMax.x - canvasMin.x);
	float canvasH = std::max(1.0f, canvasMax.y - canvasMin.y);

	float barX = canvasMin.x + normPos.x * canvasW;
	float barY = canvasMin.y + normPos.y * canvasH;

	// clamp so the whole bounding box stays inside the canvas
	float minX = canvasMin.x + leftExtent;
	float maxX = canvasMax.x - rightExtent;
	float minY = canvasMin.y + topExtent;
	float maxY = canvasMax.y - bottomExtent;
	if (maxX < minX) maxX = minX;
	if (maxY < minY) maxY = minY;

	barX = std::clamp(barX, minX, maxX);
	barY = std::clamp(barY, minY, maxY);

	Layout L;
	L.barMin = ImVec2(barX, barY);
	L.barMax = ImVec2(barX + barWidth, barY + barHeight);
	L.boxMin = ImVec2(barX - leftExtent, barY - topExtent);
	L.boxMax = ImVec2(barX + rightExtent, barY + bottomExtent);
	return L;
}

// ======================================================================
// -----------------------DRAW CALLS-------------------------------------
// ======================================================================
void Colorbar::drawBar() {

	// draw the gradient straight into the draw list so the colorbar is part of the
	// same image as the field (no separate ImGui::Image widget). uv is flipped
	// vertically so the maximum value sits at the top of the bar.
	drawList->AddImage(
		(ImTextureID)(intptr_t)colormap.getTextureID(),
		posMin,
		posMax,
		ImVec2(0.0f, 1.0f),
		ImVec2(1.0f, 0.0f)
	);
}

void Colorbar::drawOutline() {
	drawList->AddRect(posMin, posMax, IM_COL32(255, 255, 255, 255), 0.0f, 0, 1.0f);
}

void Colorbar::drawTickValue() {

	float dval = (results.currentField->vmax - results.currentField->vmin) / numTicks;

	int precision = computeTickPrecision();

	for (int i = 0; i < numTicks + 1; i++) {
		float y = posMin.y + i * dy;
		char buf[32];
		double value = results.currentField->vmax - i * dval;

		formatTickValue(buf, sizeof(buf), value, precision);

		ImVec2 textSize = ImGui::CalcTextSize(buf);
		drawList->AddText(ImVec2(posMax.x + tickLen + textOffset, y - textSize.y * 0.5f), IM_COL32(255, 255, 255, 255), buf);
	}
}

float Colorbar::getValueAtY(float localY) {

	// 0 at top, 1 at bottom
	float yNorm = localY / barHeight;
	yNorm = std::clamp(yNorm, 0.0f, 1.0f);

	// flip since 0 is at top
	float t = 1.0f - yNorm;

	return results.currentField->vmin + t * (results.currentField->vmax - results.currentField->vmin);
}

float Colorbar::yForValue(double value) {

	double vmin = results.currentField->vmin;
	double vmax = results.currentField->vmax;

	double t = (vmax > vmin) ? (value - vmin) / (vmax - vmin) : 0.5;
	t = std::clamp(t, 0.0, 1.0);

	// value == vmax -> top (posMin.y); value == vmin -> bottom (posMin.y + barHeight)
	return posMin.y + (float)((1.0 - t) * barHeight);
}

void Colorbar::drawLabel() {

	std::string text = results.fieldType[results.currentItem];
	std::vector<std::string> lines;

	std::stringstream ss(text);
	std::string word;

	while (ss >> word) {
		lines.push_back(word);
	}

	float lineHeight = ImGui::GetTextLineHeight();
	float totalHeight = lines.size() * lineHeight;	// total text height

	float centerX = posMin.x + barWidth * 0.5f;
	float y = posMin.y - totalHeight - labelGap;

	for (const std::string& line : lines) {

		ImVec2 lineSize = ImGui::CalcTextSize(line.c_str());
		ImVec2 linePos(centerX - lineSize.x * 0.5f, y);

		drawList->AddText(linePos, IM_COL32(255, 255, 255, 255), line.c_str());

		y += lineHeight;

	}
}

void Colorbar::drawTicks() {

	for (int i = 0; i < numTicks + 1; i++) {

		float y = posMin.y + i * dy;
		drawList->AddLine(ImVec2(posMax.x, y), ImVec2(posMax.x + tickLen, y), IM_COL32(255, 255, 255, 255), 1.0f);
	}
}

void Colorbar::drawFilterTicks() {

	auto drawTick = [&](const std::optional<FilterTickData>& f) {
		if (!f.has_value()) return;
		float y = yForValue(f->value);
		drawList->AddLine(ImVec2(posMin.x, y), ImVec2(posMax.x, y), IM_COL32(255, 255, 255, 255), 5.0f);
	};

	drawTick(filterValueAt);
	drawTick(filterValueLower);
	drawTick(filterValueUpper);
}

// ======================================================================
// -----------------------EVENT HANDLE-----------------------------------
// ======================================================================
void Colorbar::applyFilterClick(float localY) {

	float value = getValueAtY(localY);

	if (results.currentCompareType == CompareType::Between || results.currentCompareType == CompareType::Exclude) {
		filterValueAt.reset();

		if (!filterValueLower.has_value()) {
			results.filterValues.valueLower = value;
			filterValueLower = FilterTickData{ value };
		}
		else if (!filterValueUpper.has_value()) {
			results.filterValues.valueUpper = value;
			filterValueUpper = FilterTickData{ value };
		}
		else {
			// both already set: replace whichever tick is closest to the click
			float clickY = posMin.y + localY;
			double distToLower = std::abs(clickY - yForValue(filterValueLower->value));
			double distToUpper = std::abs(clickY - yForValue(filterValueUpper->value));

			if (distToLower < distToUpper) {
				results.filterValues.valueLower = value;
				filterValueLower = FilterTickData{ value };
			}
			else {
				results.filterValues.valueUpper = value;
				filterValueUpper = FilterTickData{ value };
			}
		}

		// keep lower <= upper
		if (filterValueLower->value > filterValueUpper->value) {
			std::swap(filterValueLower, filterValueUpper);
			std::swap(results.filterValues.valueLower, results.filterValues.valueUpper);
		}
	}
	else {
		results.filterValues.valueAt = value;
		filterValueAt = FilterTickData{ value };
	}
}

void Colorbar::resetFilterValues() {
	if (results.currentCompareType == CompareType::Between || results.currentCompareType == CompareType::Exclude) {
		filterValueAt.reset();
	}
	else {
		filterValueLower.reset();
		filterValueUpper.reset();
	}
}

// ======================================================================
// -----------------------INTERACTION------------------------------------
// ======================================================================
bool Colorbar::interact(const ImVec2& canvasMin, const ImVec2& canvasMax) {

	if (results.fieldType.empty() ||
		results.currentItem < 0 ||
		results.currentItem >= (int)results.fieldType.size() ||
		!results.currentField) {
		return false;
	}

	resetFilterValues();

	Layout L = computeLayout(canvasMin, canvasMax);

	ImVec2 boxSize(L.boxMax.x - L.boxMin.x, L.boxMax.y - L.boxMin.y);

	// invisible hit target over the whole colorbar: grab anywhere to drag it
	ImGui::SetCursorScreenPos(L.boxMin);
	ImGui::InvisibleButton("##colorbarDrag", boxSize);

	bool hovered = ImGui::IsItemHovered();
	bool active = ImGui::IsItemActive();

	ImGuiIO& io = ImGui::GetIO();

	if (ImGui::IsItemActivated()) {
		pressMouse = io.MousePos;
		dragging = false;
	}

	if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
		dragging = true;

		float canvasW = std::max(1.0f, canvasMax.x - canvasMin.x);
		float canvasH = std::max(1.0f, canvasMax.y - canvasMin.y);

		// move the bar by the pointer delta, then snap the stored normalized
		// position back to the clamped, actually-drawn position so it can never
		// drift outside the canvas (no edge dead-zone).
		float newBarX = L.barMin.x + io.MouseDelta.x;
		float newBarY = L.barMin.y + io.MouseDelta.y;

		normPos.x = (newBarX - canvasMin.x) / canvasW;
		normPos.y = (newBarY - canvasMin.y) / canvasH;

		Layout clamped = computeLayout(canvasMin, canvasMax);
		normPos.x = (clamped.barMin.x - canvasMin.x) / canvasW;
		normPos.y = (clamped.barMin.y - canvasMin.y) / canvasH;
	}

	// a click with no drag sets a filter tick, but only if it landed on the bar
	if (ImGui::IsItemDeactivated() && !dragging) {
		if (pressMouse.x >= L.barMin.x && pressMouse.x <= L.barMax.x &&
			pressMouse.y >= L.barMin.y && pressMouse.y <= L.barMax.y) {
			applyFilterClick(pressMouse.y - L.barMin.y);
		}
	}

	if (hovered || active) {
		ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
	}

	return hovered || active;
}

// ======================================================================
// -----------------------MAIN DRAW--------------------------------------
// ======================================================================
void Colorbar::draw(ImDrawList* dl, const ImVec2& canvasMin, const ImVec2& canvasMax) {

	if (results.fieldType.empty() ||
		results.currentItem < 0 ||
		results.currentItem >= (int)results.fieldType.size() ||
		!results.currentField) {
		return;
	}

	Layout L = computeLayout(canvasMin, canvasMax);

	posMin = L.barMin;
	posMax = L.barMax;
	drawList = dl;
	dy = barHeight / numTicks;

	drawBar();
	drawOutline();
	drawLabel();
	drawTicks();
	drawTickValue();
	drawFilterTicks();
}

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

// ======================================================================
// -----------------------DRAW CALLS-------------------------------------
// ======================================================================
void Colorbar::drawBar() {

	ImVec2 avail = ImGui::GetContentRegionAvail();
	float yCenter = (avail.y - barHeight) * 0.5f;

	// barLeftPad is chosen by updateLayout() so the label and tick values fit
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + barLeftPad);
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + yCenter);

	ImGui::Image((ImTextureID)(intptr_t)colormap.getTextureID(), ImVec2(barWidth, barHeight), ImVec2(0.0f, 1.0f), ImVec2(1.0f,0.0f));
}

void Colorbar::drawOutline() {
	drawList->AddRect(posMin, posMax, IM_COL32(255, 255, 255, 255), 0.0f, 0, 1.0f);
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

void Colorbar::updateLayout() {

	const float minLeftPad = 8.0f;
	const float rightPad = 8.0f;

	// keep the centered label from spilling left onto the canvas: push the bar
	// right by however far the label overhangs the bar on each side
	float labelHalf = maxLabelLineWidth() * 0.5f;
	barLeftPad = std::max(minLeftPad, labelHalf - barWidth * 0.5f);

	int precision = computeTickPrecision();

	// right edge of the tick numbers, and of the centered label
	float valueRight =
		barLeftPad + barWidth + tickLen + textOffset + maxTickTextWidth(precision);
	float labelRight = barLeftPad + barWidth * 0.5f + labelHalf;

	width = std::max(valueRight, labelRight) + rightPad;
	width = std::max(width, 60.0f);
}

void Colorbar::drawTickValue() {

	float dval = (results.currentField->vmax - results.currentField->vmin) / numTicks;	// maybe fix this since it calculates this every frame

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
	float y = posMin.y - totalHeight - 5.0f;

	for (const std::string& line : lines) {

		ImVec2 lineSize = ImGui::CalcTextSize(line.c_str());
		ImVec2 linePos(centerX - lineSize.x * 0.5f, y);

		drawList->AddText(linePos, IM_COL32(255, 255, 255, 255),line.c_str());

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

	if (filterValueAt.has_value()) {
		drawList->AddLine(
			ImVec2(posMin.x, filterValueAt->mouseY),
			ImVec2(posMax.x, filterValueAt->mouseY),
			IM_COL32(255, 255, 255, 255),
			5.0f);
	}

	if (filterValueLower.has_value()) {
		drawList->AddLine(
			ImVec2(posMin.x, filterValueLower->mouseY),
			ImVec2(posMax.x, filterValueLower->mouseY),
			IM_COL32(255, 255, 255, 255),
			5.0f);
	}

	if (filterValueUpper.has_value()) {
		drawList->AddLine(
			ImVec2(posMin.x, filterValueUpper->mouseY),
			ImVec2(posMax.x, filterValueUpper->mouseY),
			IM_COL32(255, 255, 255, 255),
			5.0f);
	}
}

// ======================================================================
// -----------------------EVENT HANDLE-----------------------------------
// ======================================================================
void Colorbar::handleMouseEvent() {

	if (!ImGui::IsItemHovered()) return;

	// set filter value at
	if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {

		// get local mouse pos and value at that position
		ImVec2 mousePos = ImGui::GetIO().MousePos;
		ImVec2 localPos = ImVec2(mousePos.x - posMin.x, mousePos.y - posMin.y);
		float value = getValueAtY(localPos.y);

		if (results.currentCompareType == CompareType::Between || results.currentCompareType == CompareType::Exclude) {
			filterValueAt.reset();

			if (!filterValueLower.has_value()) {
				results.filterValues.valueLower= value;
				filterValueLower = FilterTickData{ mousePos.y, value };
			}
			else if (!filterValueUpper.has_value()) {
				results.filterValues.valueUpper = value;
				filterValueUpper = FilterTickData{ mousePos.y, value };
			}
			else {
				//check();
				// if both are already set, set the tick that is most closest to the mouse
				double distToLower = std::abs(mousePos.y - filterValueLower->mouseY);
				double distToUpper = std::abs(mousePos.y - filterValueUpper->mouseY);


				if (distToLower < distToUpper) {
					results.filterValues.valueLower = value;
					filterValueLower = FilterTickData{ mousePos.y, value };
				}
				else {
					results.filterValues.valueUpper = value;
					filterValueUpper = FilterTickData{ mousePos.y, value };
				}
			}

			// check which one is upper and which is lower. reorganize if needed
			if (filterValueLower->value > filterValueUpper->value) {
				//check();
				std::swap(filterValueLower, filterValueUpper);
				std::swap(results.filterValues.valueLower, results.filterValues.valueUpper);
			}
		}
		else {
			results.filterValues.valueAt = value;
			filterValueAt = FilterTickData{ mousePos.y, value };
		}
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
// -----------------------MAIN RENDER LOOP-------------------------------
// ======================================================================
void Colorbar::render() { 

	drawBar();

	posMin = ImGui::GetItemRectMin();
	posMax = ImGui::GetItemRectMax();
	drawList = ImGui::GetWindowDrawList();

	resetFilterValues();

	handleMouseEvent();

	drawOutline();
	drawLabel();
	drawTicks();
	drawTickValue();
	drawFilterTicks();

}
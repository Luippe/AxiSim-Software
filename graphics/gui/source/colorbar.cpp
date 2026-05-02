#include "colorbar.h"
#include "colormap.h"
#include "printer.h"
#include "results.h"

void Colorbar::drawBar() {

	ImVec2 avail = ImGui::GetContentRegionAvail();
	float xCenter = (avail.x - barWidth) * 0.5f;
	float yCenter = (avail.y - barHeight) * 0.5f;

	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + xCenter);
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + yCenter);

	ImGui::Image((ImTextureID)(intptr_t)colormap.getTextureID(), ImVec2(barWidth, barHeight), ImVec2(0.0f, 1.0f), ImVec2(1.0f,0.0f));
}

void Colorbar::drawOutline() {
	drawList->AddRect(posMin, posMax, IM_COL32(255, 255, 255, 255), 0.0f, 0, 1.0f);
}

void Colorbar::drawTickValue() {

	float dval = (results.currentField->vmax - results.currentField->vmin) / numTicks;	// maybe fix this since it calculates this every frame

	for (int i = 0; i < numTicks + 1; i++) {
		float y = posMin.y + i * dy;
		char buf[32];
		snprintf(buf, sizeof(buf), "%.5f", results.currentField->vmax - i * dval);

		ImVec2 textSize = ImGui::CalcTextSize(buf);
		drawList->AddText(ImVec2(posMax.x + tickLen + textOffset, y - textSize.y * 0.5f), IM_COL32(255, 255, 255, 255), buf);
	}
}

void Colorbar::drawLabel() {

	const char* label = "Concentration";
	ImVec2 textSize = ImGui::CalcTextSize(label);

	ImVec2 textPos(
		posMin.x + (barWidth - textSize.x) * 0.5f,
		posMin.y - 25.0f
	);

	drawList->AddText(textPos, IM_COL32(255, 255, 255, 255), label);
}

void Colorbar::drawTicks() {

	for (int i = 0; i < numTicks + 1; i++) {

		float y = posMin.y + i * dy;
		drawList->AddLine(ImVec2(posMax.x, y), ImVec2(posMax.x + tickLen, y), IM_COL32(255, 255, 255, 255), 1.0f);
	}
}


void Colorbar::render() { 

	ImGui::Begin("Colorbar");

	drawBar();

	posMin = ImGui::GetItemRectMin();
	posMax = ImGui::GetItemRectMax();
	drawList = ImGui::GetWindowDrawList();

	drawOutline();
	drawLabel();
	drawTicks();
	drawTickValue();

	ImGui::End();

}
#include "pch.h"
#include <format>
#include <algorithm>
#include <cmath>

#include "imgui_internal.h"

#include "residual_plot.h"

#include "solver.h"

#include "graphics_struct.h"

#include "flag_manager.h"
#include "printer.h"

namespace {
    constexpr double residualPlotFloor = 1.0e-30;

    double residualValueForPlot(double value) {
        if (!std::isfinite(value) || value <= residualPlotFloor) {
            return residualPlotFloor;
        }

        return value;
    }
}

ResidualPlot::ResidualPlot(Solver& solver, AppConfig& appConfig) :
    solver(solver),
    assets(appConfig.assets){

}

void ResidualPlot::resetState() {

    std::lock_guard<std::mutex> lock(mutex);

    marker.Marker = ImPlotMarker_Circle;
    marker.MarkerSize = 5.0f;
    marker.LineWeight = 0.0f;
    int activeTabID = residualDockSpace.getActiveTabID();

    tabs[activeTabID].plots.clear();
    tabs[activeTabID].clickedPos.clear();
    tabs[activeTabID].iterations.clear();

}


void ResidualPlot::clearPlot(int i) {
    tabs[i].clickedPos.clear();
}

void setToolTip(const char* text){
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", text);
    }
}

int findClosestX(const std::vector<double>& x, double mouseX) {

    if (x.empty()) return -1;

    int bestIndex = 0;
    auto it = std::lower_bound(x.begin(), x.end(), mouseX);

    if (it == x.begin()) {
        return 0;
    }

    if (it == x.end()) {
        return (int)(x.size() - 1);
    }

    int rightIndex = (int)(it - x.begin());
    int leftIndex = rightIndex - 1;

    double leftDist = std::abs(mouseX - x[leftIndex]);
    double rightDist = std::abs(mouseX - x[rightIndex]);

    if (leftDist <= rightDist) {
        return leftIndex;
    }

    return rightIndex;
}

int closestPlotY(std::vector<Plot>& plots, int idx, double mouseY) {
    int best = -1;
    double bestDist = DBL_MAX;
    double maxDist = 0.15;

    for (int p = 0; p < plots.size(); p++) {
        if (idx >= plots[p].y.size()) continue;

        double y = plots[p].y[idx];
        if (mouseY <= 0.0 || y <= 0.0) continue;

        double dist = std::abs(std::log10(mouseY) - std::log10(y));

        if (dist < bestDist) {
            bestDist = dist;
            best = p;
        }
    }

    if (bestDist > maxDist) return -1;
    return best;
}

void ResidualPlot::add(int currentIteration, const std::vector<ResidualPrintItem>& residualsToPrint) {
    std::lock_guard<std::mutex> lock(mutex);
    size_t idx = 0;
    int activeTabID = residualDockSpace.getActiveTabID();

    tabs[activeTabID].iterations.push_back((double)currentIteration);
    for (const ResidualPrintItem& item : residualsToPrint) {
        if (!item.enabled) continue;

        tabs[activeTabID].plots[idx++].y.push_back(
            residualValueForPlot(item.coeff->resVal)
        );
    }
}

bool ResidualPlot::copyActivePlotToClipboard() {

    GLint oldFBO, oldViewport[4];
    ImVec2 oldDisplaySize, oldFramebufferSize;
    offScreenFBO.create2DBuffer(pendingCopyWidth, pendingCopyHeight, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
    offScreenFBO.beginOffScreenImGuiRender(oldFBO, oldViewport, oldDisplaySize, oldFramebufferSize);

    // build imgui draw commands
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##ExportWindow", nullptr, UIFlags::TemporaryWindowFlags);

    // draw the plot which will be copied. but first set the axes to the correct limits
    ImPlot::SetNextAxesLimits(
        tabs[pendingCopyTabID].currentLimits.X.Min,
        tabs[pendingCopyTabID].currentLimits.X.Max,
        tabs[pendingCopyTabID].currentLimits.Y.Min,
        tabs[pendingCopyTabID].currentLimits.Y.Max,
        ImGuiCond_Always
    );

    if (ImPlot::BeginPlot("Solver Residuals", ImVec2((float)pendingCopyWidth, (float)pendingCopyHeight), ImPlotFlags_NoMouseText)) {

        drawPlotData(tabs[pendingCopyTabID]);

        ImPlot::EndPlot();
    }

    ImGui::End();
    ImGui::PopStyleVar();

    offScreenFBO.endOffScreenImGuiRender(oldFBO, oldViewport, oldDisplaySize, oldFramebufferSize);

    return true;
}

void ResidualPlot::setupAxes() {
    if (solver.solverRunning) {
        ImPlot::SetupAxes("Iteration", "Residual", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
    }
    else {
        ImPlot::SetupAxes("Iteration", "Residual");
    }
    ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);
}

void displayTextAtPos(double x, double y, ImPlotSpec& marker) {

    ImPlot::PlotScatter("##", &x, &y, 1, marker);
    std::string text = std::format("{:.0f}, {:.2e}", x, y);
    ImPlot::PlotText(text.c_str(), x, y, ImVec2(10.0f, -20.0f));

}


void ResidualPlot::setName(const std::vector<ResidualPrintItem>& residualsToPlot) {
    for (const ResidualPrintItem& residualPrint : residualsToPlot) {
        if (residualPrint.enabled) {
            int activeTabID = residualDockSpace.getActiveTabID();
            Plot plot;
            plot.name = residualPrint.name;
            tabs[activeTabID].plots.push_back(std::move(plot));
        }
    }
}

// ======================================================================
// -----------------------HANDLE EVENTS----------------------------------
// ======================================================================
void ResidualPlot::handlePlotEvents(ResidualPlotTab& tab) {


    if (!ImPlot::IsPlotHovered() || tab.iterations.empty()) return;

    ImPlotPoint mouse = ImPlot::GetPlotMousePos();

    idx = findClosestX(tab.iterations, mouse.x);
    p = closestPlotY(tab.plots, idx, mouse.y);

    if (p >= 0) {

        // change cursor and add marker on hovered point
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

        displayTextAtPos(tab.iterations[idx], tab.plots[p].y[idx], marker);

        // draw text at hovered location and store it if clicked
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            tab.clickedPos.push_back({
                tab.iterations[idx],
                tab.plots[p].y[idx]
                });
        }
    }
}

void ResidualPlot::handleKeyEvents() {

}

// ======================================================================
// -----------------------DRAW CALLS-------------------------------------
// ======================================================================
void ResidualPlot::drawPlotData(ResidualPlotTab& tab) {

    setupAxes();

    // draw plot
    for (const Plot& plot : tab.plots) {

        int count = (int)std::min(tab.iterations.size(), plot.y.size());

        if (count > 0) {
            ImPlot::PlotLine(plot.name.c_str(), tab.iterations.data(), plot.y.data(), count);
        }
    }

    // draw text
    for (const TextPos& textPos : tab.clickedPos) {
        displayTextAtPos(textPos.iteration, textPos.residual, marker);
    }
}

void ResidualPlot::drawPlot(ResidualPlotTab& tab) {

    if (tab.resetView) {
        ImPlot::SetNextAxesToFit();
        tab.resetView = false;
    }

    if (ImPlot::BeginPlot("Solver Residuals", ImVec2(-1, 400), ImPlotFlags_NoMouseText)) {

        drawPlotData(tab);

        handlePlotEvents(tab);

        tab.currentLimits = ImPlot::GetPlotLimits();

        ImPlot::EndPlot();
    }

    // if we want to copy the image
    if (tab.copyImageNextFrame) {

        ImVec2 plotSize = ImGui::GetItemRectSize();
        int activeTabID = residualDockSpace.getActiveTabID();

        pendingCopyTabID = activeTabID;
        pendingCopyWidth = (int)plotSize.x;
        pendingCopyHeight = (int)plotSize.y;
        pendingCopy = true;

        tab.copyImageNextFrame = false;

    }
}

void ResidualPlot::drawToolBar(ResidualPlotTab& tab, int i, ImGuiID currentDockID, ImGuiID& pendingAddDockID, ImGuiID dockspaceID) {

    ImGui::BeginChild("##toolbar", ImVec2(0.0f, toolbarHeight), false);

    if (ImGui::ImageButton("##AddTab", (ImTextureID)(intptr_t)assets.plusIcon.getTextureID(), ImVec2(iconSize, iconSize))) {
        pendingAddDockID = currentDockID != 0 ? currentDockID : dockspaceID;
    }

    setToolTip("Add new tab");
    ImGui::SameLine();

    if (ImGui::ImageButton("##ResetView", (ImTextureID)(intptr_t)assets.houseIcon.getTextureID(), ImVec2(iconSize, iconSize))) {
        tab.resetView = true;
    }

    setToolTip("Reset view");
    ImGui::SameLine();

    if (ImGui::ImageButton("##ClearPoints", (ImTextureID)(intptr_t)assets.clearIcon.getTextureID(), ImVec2(iconSize, iconSize))) {
        clearPlot(i);
    }

    setToolTip("Clear all selected points");
    ImGui::SameLine();

    if (ImGui::ImageButton("##CopyToClipboard", (ImTextureID)(intptr_t)assets.copyIcon.getTextureID(), ImVec2(iconSize, iconSize)) || consoleCopy) {
        tab.copyImageNextFrame = true;
        pendingCopy = true;
        consoleCopy = false;
    }

    setToolTip("Copy to clipboard");
    ImGui::SameLine();

    ImGui::EndChild();

}

// ======================================================================
// -----------------------MAIN DRAW LOOP---------------------------------
// ======================================================================
void ResidualPlot::draw() {

    auto dockInfo = residualDockSpace.renderDockSpace();

    residualDockSpace.drawTabs(
        tabs,
        dockInfo.dockspaceID,
        dockInfo.windowClass,
        [this](
            ResidualPlotTab& tab,
            int i,
            ImGuiID currentDockID,
            ImGuiID& pendingAddDockID,
            ImGuiID dockspaceID
            ) {
                drawToolBar(tab, i, currentDockID, pendingAddDockID, dockspaceID);
                drawPlot(tab);
        }
    );

    handleKeyEvents();
}

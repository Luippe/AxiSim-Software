#include "pch.h"
#include <format>
#include "residual_plot.h"
#include "graphics_struct.h"
#include "gui_manager.h"
#include "implot.h"
#include "solver.h"
#include "printer.h"
#include <algorithm>


ResidualPlot::ResidualPlot(Solver& solver, AppAssets& assets) :
    solver(solver),
    assets(assets){

}

void ResidualPlot::resetState() {

    std::lock_guard<std::mutex> lock(mutex);

    marker.Marker = ImPlotMarker_Circle;
    marker.MarkerSize = 5.0f;
    marker.LineWeight = 0.0f;
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

void ResidualPlot::addTab(ImGuiID targetDockID) {

    ResidualPlotTab tab;

    tab.id = nextTabID++;
    tab.name = "Residual Plot " + std::to_string(tab.id);
    tab.newlyCreated = true;
    tab.targetDockID = targetDockID;

    tabs.push_back(std::move(tab));
    activeTabID = (int)tabs.size() - 1;

}

void ResidualPlot::setName(const std::array<ResidualPrintItem, 6>& residualsToPlot) {
    for (const ResidualPrintItem& residualPrint : residualsToPlot) {
        if (residualPrint.enabled) {
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

void ResidualPlot::drawTabs(ImGuiID dockspaceID, const ImGuiWindowClass& residualClass) {

    int tabToClose = -1;
    ImGuiID pendingAddDockID = 0;

    for (int i = 0; i < tabs.size(); i++) {
        ResidualPlotTab& tab = tabs[i];

        bool open = true;

        // set window class and dock id. make newly created tabs always docked next the other tabs
        ImGui::SetNextWindowClass(&residualClass);

        if (tabs[i].newlyCreated) {
            ImGui::SetNextWindowDockID(tab.targetDockID, ImGuiCond_Always);
            tabs[i].newlyCreated = false;
        }
        else {
            ImGui::SetNextWindowDockID(dockspaceID, ImGuiCond_FirstUseEver);
        }


        // draw plot
        if (ImGui::Begin(tab.name.c_str(), &open)) {

            ImGuiID currentDockID = ImGui::GetWindowDockID();
       
            ImGui::PushID(tab.id);

            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
                activeTabID = i;
            }

            drawToolBar(tab, i, currentDockID, pendingAddDockID, dockspaceID);
            //printInt(currentDockID, pendingAddDockID);
            drawPlot(tab);
               
            ImGui::PopID();
        }

        ImGui::End();

        if (!open) {
            tabToClose = i;
        }
    }

    // remove tabs
    if (tabToClose != -1) {
        tabs.erase(tabs.begin() + tabToClose);

        activeTabID = (int)tabs.size() - 1; // move to the right tab after closing a tab
    }

    // add tab if plus button was pressed
    if (pendingAddDockID != 0 || tabs.empty()) {
        addTab(pendingAddDockID != 0 ? pendingAddDockID : dockspaceID);
    }
}

// ======================================================================
// -----------------------MAIN DRAW LOOP---------------------------------
// ======================================================================
void ResidualPlot::draw() {
	ImGui::Begin("Residual Plot", nullptr, UIFlags::ResidualTabBarFlags);

    std::lock_guard<std::mutex> lock(mutex); 

    // create dockspace id and class
    ImGuiID dockspaceID = ImGui::GetID("ResidualPlotDockSpace");
    ImGuiID classID = ImGui::GetID("ResidualPlotDockClass");

    ImGuiWindowClass residualClass;
    residualClass.ClassId = classID;
    residualClass.DockingAlwaysTabBar = true;

    // prevent unclassed windows from docking here, and prevents reisudal windows from docking into normal dockspace
    residualClass.DockingAllowUnclassed = false;

    //if (tabs.empty()) {
    //    addTab();
    //}

    ImGui::DockSpace(dockspaceID, ImGui::GetContentRegionAvail(), UIFlags::ResidualDockSpaceFlags, &residualClass);

	ImGui::End();

    drawTabs(dockspaceID, residualClass);

    handleKeyEvents();
}
#include "pch.h"
#include <format>
#include "residual_plot.h"
#include "gui_manager.h"
#include "implot.h"
#include "solver.h"
#include "printer.h"


ResidualPlot::ResidualPlot(Solver& solver) :
    solver(solver){

}

void ResidualPlot::resetState() {

    std::lock_guard<std::mutex> lock(mutex);

    marker.Marker = ImPlotMarker_Circle;
    marker.MarkerSize = 5.0f;
    marker.LineWeight = 0.0f;
    tabs[activeTabID].plots.clear();

    clickedPos.clear();

}

void ResidualPlot::clearPlot() {

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

void ResidualPlot::addTab() {

    ResidualPlotTab tab;
    tab.id = nextTabID++;
    tab.name = "Residual Plot " + std::to_string(tab.id);
    tab.newlyCreated = true;

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

void ResidualPlot::handleEvents(ResidualPlotTab& tab) {

    // always draw all stored markers and text
    for (const TextPos& textPos : clickedPos) {
        displayTextAtPos(textPos.iteration, textPos.residual, marker);
    }

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
            clickedPos.push_back({
                tab.iterations[idx],
                tab.plots[p].y[idx]
                });
        }
    }

    // double clicking removes all markers
    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        clickedPos.clear();
    }
}


void ResidualPlot::drawPlotTab(ResidualPlotTab& tab) {

    if (ImPlot::BeginPlot("Solver Residuals", ImVec2(-1, 420), ImPlotFlags_NoMouseText)) {

        setupAxes();

        for (const Plot& plot : tab.plots) {

            int count = (int)std::min(tab.iterations.size(), plot.y.size());

            if (count > 0) {
                ImPlot::PlotLine(plot.name.c_str(), tab.iterations.data(), plot.y.data(), count);
            }
        }

        handleEvents(tab);

        ImPlot::EndPlot();
    }
}
void ResidualPlot::drawTabs(ImGuiID dockspaceID, const ImGuiWindowClass& residualClass) {

    int tabToClose = -1;

    if (ImGui::BeginTabBar("ResidualPlotTabs", UIFlags::TabBarFlags)) {

        for (int i = 0; i < tabs.size(); i++) {
            ResidualPlotTab& tab = tabs[i];

            bool open = true;

            // set window class and dock id. make newly created tabs always docked next the other tabs
            ImGui::SetNextWindowClass(&residualClass);

            if (tabs[i].newlyCreated) {
                ImGui::SetNextWindowDockID(dockspaceID, ImGuiCond_Always);
                tabs[i].newlyCreated = false;
            }
            else {
                ImGui::SetNextWindowDockID(dockspaceID, ImGuiCond_FirstUseEver);
            }


            // draw tab
            if (ImGui::Begin(tab.name.c_str(), &open)) {

                if (ImGui::IsWindowFocused()) {
                    activeTabID = i;
                }

                drawPlotTab(tab);
   
            }

            ImGui::End();

            if (!open) {
                tabToClose = i;
            }
        }

        ImGui::EndTabBar();
    }

    // remove tabs
    if (tabToClose != -1) {
        tabs.erase(tabs.begin() + tabToClose);

        activeTabID = (int)tabs.size() - 1; // move to the right tab after closing a tab
    }

}

// ======================================================================
// -----------------------MAIN DRAW LOOP---------------------------------
// ======================================================================
void ResidualPlot::draw() {
	ImGui::Begin("Residual Plot", nullptr, UIFlags::ResidualTabBarFlags);

    std::lock_guard<std::mutex> lock(mutex); 

    ImGuiID dockspaceID = ImGui::GetID("ResidualPlotDockSpace");
    ImGuiID classID = ImGui::GetID("ResidualPlotDockClass");

    ImGuiWindowClass residualClass;
    residualClass.ClassId = classID;

    // prevent unclassed windows from docking here, and prevents reisudal windows from docking into normal dockspace
    residualClass.DockingAllowUnclassed = false;

    if (ImGui::SmallButton("+") || tabs.empty()) {
        addTab();
    }
    ImGui::Separator();

    ImGui::DockSpace(dockspaceID, ImGui::GetContentRegionAvail(), UIFlags::ResidualDockSpaceFlags, &residualClass);

	ImGui::End();

    drawTabs(dockspaceID, residualClass);

}
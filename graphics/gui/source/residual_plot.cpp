#include "pch.h"
#include <format>
#include <algorithm>
#include <cmath>

#include "imgui_internal.h"

#include "residual_plot.h"

#include "solver.h"

#include "app_struct.h"

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

    const int i = activeTabIndex();
    if (i < 0) return;

    tabs[i].plots.clear();
    tabs[i].clickedPos.clear();
    tabs[i].iterations.clear();

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

void ResidualPlot::add(int currentIteration, const std::unordered_map<std::string, ConfigResidual>& configResiduals) {
    std::lock_guard<std::mutex> lock(mutex);

    const int i = activeTabIndex();
    if (i < 0) return;

    size_t idx = 0;
    ResidualPlotTab& tab = tabs[i];

    tab.iterations.push_back((double)currentIteration);
    for (const auto& [name, configResidual] : configResiduals) {
        if (!configResidual.enabled) continue;

        tab.plots[idx++].y.push_back(
            residualValueForPlot(configResidual.resVal)
        );
    }
}

bool ResidualPlot::copyActivePlotToClipboard() {

    // the copy is consumed a frame after it was requested, so the tab may be gone
    const int i = tabIndexFromID(pendingCopyTabID);
    if (i < 0) return false;

    GLint oldFBO, oldViewport[4];
    ImVec2 oldDisplaySize, oldFramebufferSize;
    offScreenFBO.create2DBuffer(pendingCopyWidth, pendingCopyHeight, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
    offScreenFBO.beginOffScreenImGuiRender(oldFBO, oldViewport, oldDisplaySize, oldFramebufferSize);

    // build imgui draw commands
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##ExportWindow", nullptr, UIFlags::TemporaryWindowFlags);

    // draw the plot which will be copied. but first set the axes to the correct limits
    ImPlot::SetNextAxesLimits(
        tabs[i].currentLimits.X.Min,
        tabs[i].currentLimits.X.Max,
        tabs[i].currentLimits.Y.Min,
        tabs[i].currentLimits.Y.Max,
        ImGuiCond_Always
    );

    if (ImPlot::BeginPlot("Solver Residuals", ImVec2((float)pendingCopyWidth, (float)pendingCopyHeight), ImPlotFlags_NoMouseText)) {

        drawPlotData(tabs[i]);

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


void ResidualPlot::setName(const std::unordered_map<std::string, ConfigResidual>& residualsToPlot) {
    const int i = activeTabIndex();
    if (i < 0) return;

    for (const auto& [name, configResidual] : residualsToPlot) {
        if (configResidual.enabled) {
            Plot plot;
            plot.name = name;
            tabs[i].plots.push_back(std::move(plot));
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

        // `tab` is the one the toolbar flagged, so take its id straight -- no
        // need to go back through the dockspace's selection
        pendingCopyTabID = tab.id;
        pendingCopyWidth = (int)plotSize.x;
        pendingCopyHeight = (int)plotSize.y;
        pendingCopy = true;

        tab.copyImageNextFrame = false;

    }
}

int ResidualPlot::tabIndexFromID(int id) {
    for (int i = 0; i < (int)tabs.size(); i++) {
        if (tabs[i].id == id) {
            return i;
        }
    }

    return -1;
}

int ResidualPlot::activeTabIndex() {
    // getActiveTabID() is a tab's unique id, NOT its slot in `tabs` (closing a
    // tab shifts the rest), so resolve it to an index rather than indexing by id.
    return tabIndexFromID(residualDockSpace.getActiveTabID());
}

void ResidualPlot::drawAppToolBar() {

    beginToolbar();

    // Home leads the strip, matching the sketch/mesh/results toolbars. Every
    // button except Add acts on the selected plot, so with no tabs open there
    // is nothing to act on and they disable.
    const int i = activeTabIndex();

    // Locked out for the duration of a solve: the solver thread appends to
    // tabs[].plots while this strip runs, so adding, clearing or copying a plot
    // mid-run mutates the same vectors it is writing to.
    const bool solving = solver.solverRunning;

    ImGui::BeginDisabled(solving);

    const bool hasTab = i >= 0;

    // --- home ---
    // both buttons here act on the selected plot, so the section name dims with
    // them rather than heading a group that does nothing
    beginSection();
    ImGui::BeginDisabled(!hasTab);

    if (addImageButton("ResetView", "Home", "Reset view", assets.icon("house")) && hasTab) {
        tabs[i].resetView = true;
    }

    ImGui::SameLine();

    if ((addImageButton("Copy", "Copy", "Copy to clipboard", assets.icon("clipboard")) || consoleCopy) && hasTab) {
        tabs[i].copyImageNextFrame = true;
        pendingCopy = true;
        consoleCopy = false;
    }

    endSection("Home");
    ImGui::EndDisabled();

    // --- view ---
    beginSection();
    // Add targets whichever dock the selected plot lives in, but that dock id is
    // only resolved inside drawTabs(), so just park the request for draw().
    if (addImageButton("AddTab", "Add", "Add new plot tab", assets.icon("plus"))) {
        pendingAddTab = true;
    }

    ImGui::SameLine();

    ImGui::BeginDisabled(!hasTab);

    if (addImageButton("ClearPoints", "Clear", "Clear all selected points", assets.icon("circle-x")) && hasTab) {
        clearPlot(i);
    }

    ImGui::EndDisabled();
    endSection("View");

    ImGui::EndDisabled();

    endToolbar();
}

// ======================================================================
// -----------------------MAIN DRAW LOOP---------------------------------
// ======================================================================
void ResidualPlot::draw() {

    // draw() only runs while the Solver tab is open, so a gap in the frame numbers
    // means the user was on another tab and has just come back. The dock node
    // re-selects a tab on its own when these windows reappear, so restore the one
    // that was selected on the way out.
    const int frame = ImGui::GetFrameCount();

    if (frame - lastDrawnFrame > 1) {
        const int restoreID = residualDockSpace.getActiveTabID();
        if (restoreID != 0) {
            residualDockSpace.requestFocus(restoreID);
        }
    }

    lastDrawnFrame = frame;

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
                // consume an Add parked by the app toolbar; the dock to add into
                // is only known here. Only the selected tab answers, so the new
                // tab lands beside the plot the user was looking at.
                if (pendingAddTab && i == activeTabIndex()) {
                    pendingAddDockID = currentDockID != 0 ? currentDockID : dockspaceID;
                    pendingAddTab = false;
                }

                drawPlot(tab);
        }
    );

    // no tabs open (or none selected), so nothing above consumed it -- add to the
    // dockspace root instead
    if (pendingAddTab) {
        residualDockSpace.addTab(tabs, dockInfo.dockspaceID);
        pendingAddTab = false;
    }

    handleKeyEvents();
}

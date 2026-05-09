#include "pch.h"
#include <format>
#include "residual_plot.h"
#include "implot.h"
#include "solver.h"
#include "printer.h"


ResidualPlot::ResidualPlot(Solver& solver, std::initializer_list<std::string> residualNames) :
    solver(solver),
    names(residualNames){



    resetState();

}

void ResidualPlot::resetState() {

    std::lock_guard<std::mutex> lock(mutex);

    marker.Marker = ImPlotMarker_Circle;
    marker.MarkerSize = 5.0f;
    marker.LineWeight = 0.0f;

    iterations.clear();
    plots.clear();
    clickedPos.clear();
    plots.reserve(names.size());

    for (const std::string& name : names) {
        Plot plot;
        plot.name = name;
        plots.push_back(std::move(plot));
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

void ResidualPlot::handleEvents() {

    // always draw all stored markers and text
    for (const TextPos& textPos : clickedPos) {
        displayTextAtPos(textPos.iteration, textPos.residual, marker);
    }

    if (!ImPlot::IsPlotHovered() || iterations.empty()) return;

    ImPlotPoint mouse = ImPlot::GetPlotMousePos();

    idx = findClosestX(iterations, mouse.x);
    p = closestPlotY(plots, idx, mouse.y);

    if (p >= 0) {

        // change cursor and add marker on hovered point
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

        displayTextAtPos(iterations[idx], plots[p].y[idx], marker);

        // draw text at hovered location and store it if clicked
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            clickedPos.push_back({
                iterations[idx],
                plots[p].y[idx]
                });
        }
    }

    // double clicking removes all markers
    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        clickedPos.clear();
    }
    

}

// ======================================================================
// -----------------------MAIN DRAW LOOP--------------------------------
// ======================================================================
void ResidualPlot::draw() {
	ImGui::Begin("Residual Plot");

    std::lock_guard<std::mutex> lock(mutex); 

    if (ImPlot::BeginPlot("Solver Residuals", ImVec2(-1, 500), ImPlotFlags_NoMouseText)) {

        setupAxes();

        for (const Plot& plot : plots) {

            int count = (int)std::min(iterations.size(), plot.y.size());

            if (count > 0) {
                ImPlot::PlotLine(plot.name.c_str(), iterations.data(), plot.y.data(), count);
            }
        }

        handleEvents();

        ImPlot::EndPlot();
    }


	ImGui::End();
}
#include "pch.h"
#include "residual_plot.h"
#include "imgui.h"
#include "implot.h"

ResidualPlot::ResidualPlot(std::initializer_list<std::string> residualNames) :
    names(residualNames){
}

void ResidualPlot::draw() {
	ImGui::Begin("Residual Plot");

    std::lock_guard<std::mutex> lock(mutex); 

    size_t nResiduals = names.size();
    size_t nSamples = values.size() / names.size();

    if (nSamples == 0) {
        ImGui::Text("No residual data yet.");
        ImGui::End();
        return;
    }

    if (ImPlot::BeginPlot("Solver Residuals", ImVec2(-1, 500))) {

        ImPlot::SetupAxes(
            "Iteration",
            "Residual",
            ImPlotAxisFlags_AutoFit,
            ImPlotAxisFlags_AutoFit
        );

        ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);

        for (size_t r = 0; r < nResiduals; ++r) {

            std::vector<double> x;
            std::vector<double> y;

            x.reserve(nSamples);
            y.reserve(nSamples);

            for (size_t s = 0; s < nSamples; ++s) {

                double residual = (double)(values[s * nResiduals + r]);

                // log scale cannot show zero or negative values
                if (residual <= 0.0) {
                    continue;
                }

                double iter = iterations.size() == nSamples ? (double)(iterations[s]) : (double)(s);

                x.push_back(iter);
                y.push_back(residual);
            }

            ImPlot::PlotLine(
                names[r].c_str(),
                x.data(),
                y.data(),
                (int)(x.size())
            );
        }
        ImPlot::EndPlot();
    }
	ImGui::End();
}

void ResidualPlot::clear() {
    std::lock_guard<std::mutex> lock(mutex);
    iterations.clear();
    values.clear();
}
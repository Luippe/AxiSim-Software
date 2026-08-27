#include "mesh_plot.h"

#include <cmath>
#include <string>
#include <vector>

#include "imgui.h"
#include "implot.h"

#include "quality.h"

namespace {

	constexpr int histogramBins = 30;

	// Tall enough to read a distribution, short enough that both metrics fit the
	// window's default height without scrolling.
	constexpr float histogramHeight = 220.0f;

	// PlotHistogram bins over the min/max of what it is handed, so one NaN would
	// swallow the whole range -- and Quality stores every non-triangle cell as NaN.
	std::vector<double> measuredValues(const std::vector<double>& values) {
		std::vector<double> out;
		out.reserve(values.size());

		for (double v : values) {
			if (std::isfinite(v)) {
				out.push_back(v);
			}
		}

		return out;
	}

}

void drawMeshQualityHistogram(const std::vector<double>& values, const char* name, bool logCount) {

	const std::vector<double> measured = measuredValues(values);

	if (measured.empty()) {
		ImGui::TextDisabled("%s: no triangular cells to measure", name);
		return;
	}

	// "##" hides the title: the x axis already carries the metric's name, and a
	// stack of plots has no room to spend a row saying it twice.
	const std::string plotID = std::string("##hist_") + name;

	if (ImPlot::BeginPlot(plotID.c_str(), ImVec2(-1.0f, histogramHeight), ImPlotFlags_NoMouseText)) {

		// Both metrics are long-tailed -- a mesh with one sliver puts every other
		// cell in the first bin, and the tail is a single pixel next to it. A log
		// count is the only way to see the tail, but it cannot draw an empty bin,
		// so it is a toggle rather than the default.
		if (logCount) {
			ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);
		}

		ImPlot::SetupAxes(name, "cells", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);

		ImPlot::PlotHistogram("##cells", measured.data(), (int)measured.size(), histogramBins);

		ImPlot::EndPlot();
	}
}

void drawMeshQualityHistograms(const Quality& quality, bool& logCount) {

	// Every per-cell metric Quality owns. A third vector added there needs one row
	// here and nothing else.
	const struct {
		const char* name;
		const std::vector<double>& values;
	} metrics[] = {
		{ "aspect ratio", quality.aspectRatios },
		{ "element quality", quality.elementQuality },
	};

	ImGui::Checkbox("Log count", &logCount);

	for (const auto& metric : metrics) {
		drawMeshQualityHistogram(metric.values, metric.name, logCount);
	}
}

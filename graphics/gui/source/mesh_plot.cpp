#include "mesh_plot.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "imgui.h"
#include "implot.h"

#include "quality.h"

namespace {

	constexpr int histogramBins = 200;

	// Green reads clearly against the histogram fill without looking like another
	// series. Both bounds share one colour: which is which is never in doubt.
	const ImVec4 boundsColour = ImVec4(0.10f, 0.60f, 0.20f, 1.0f);

	// Tall enough to read a distribution. Three of these no longer fit the window's
	// default height, so the panel scrolls -- shrinking them to avoid that costs more
	// readability than the scroll does.
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

	// A bound's value printed just inside the top of the plot, clear of its own line:
	// the min's label to the right of it, the max's to the left, so neither runs off
	// the edge it sits against. Must be called between Begin/EndPlot.
	void drawBoundLabel(double value, bool toTheRight) {

		char text[32];
		std::snprintf(text, sizeof(text), "%.4g", value);

		const ImVec2 size = ImGui::CalcTextSize(text);
		const float gap = 6.0f;

		// PlotText centres on the point, so half the text is itself part of the offset
		// needed to clear the line. NoFit keeps the label out of the y auto-fit, which
		// would otherwise grow the axis every frame to enclose the text it just placed.
		ImPlotSpec spec;
		spec.Flags = ImPlotItemFlags_NoFit;

		const float dx = size.x * 0.5f + gap;
		const ImVec2 offset(toTheRight ? dx : -dx, size.y * 0.5f + 4.0f);

		ImPlot::PushStyleColor(ImPlotCol_InlayText, boundsColour);
		ImPlot::PlotText(text, value, ImPlot::GetPlotLimits().Y.Max, offset, spec);
		ImPlot::PopStyleColor();
	}

}

void drawMeshQualityHistogram(
	const std::vector<double>& values,
	const char* name,
	const char* sampleName,
	double xMin,
	double xMax,
	bool logCount
) {

	const std::vector<double> measured = measuredValues(values);

	if (measured.empty()) {
		ImGui::TextDisabled("%s: no triangular cells to measure", name);
		return;
	}

	// PlotHistogram drops anything outside the band instead of binning it, and the
	// cells outside are the ones worth knowing about -- a sliver at aspect 29.8
	// would otherwise leave no trace at all.
	int outside = 0;

	// The extremes are what the plot is read for, and they are exactly the bars that
	// are one pixel tall and easy to miss. measured is non-empty by the check above.
	double lo = measured[0];
	double hi = measured[0];

	for (double v : measured) {
		if (v < xMin || v > xMax) {
			outside++;
		}

		lo = std::min(lo, v);
		hi = std::max(hi, v);
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

		// The y axis carries no information a reader needs off this plot -- the shape of
		// the distribution is the point, not the count at any one bin -- so it keeps its
		// scale and loses its label, its numbers and the grid drawn from them.
		ImPlot::SetupAxes(
			name,
			nullptr,
			ImPlotAxisFlags_NoGridLines,
			ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_NoGridLines | ImPlotAxisFlags_NoTickLabels
		);

		// Always, not Once: the band is the metric's definition, so the axis holds
		// it across meshes and cannot be zoomed off it.
		ImPlot::SetupAxisLimits(ImAxis_X1, xMin, xMax, ImPlotCond_Always);

		ImPlot::PlotHistogram(
			"##cells",
			measured.data(),
			(int)measured.size(),
			histogramBins,
			1.0,
			ImPlotRange(xMin, xMax)
		);

		// A bound sitting on the band edge draws over the axis and says nothing the axis
		// did not already: aspect ratio bottoms out at exactly 1, element quality tops
		// out at exactly 1. Drop it, line and label together. The same test hides a
		// bound beyond the band, which is clipped and left to the count below anyway.
		const double edgeTolerance = 0.005 * (xMax - xMin);

		const bool showLo = lo > xMin + edgeTolerance;
		const bool showHi = hi < xMax - edgeTolerance;

		double bounds[2];
		int boundCount = 0;

		if (showLo) {
			bounds[boundCount++] = lo;
		}

		if (showHi) {
			bounds[boundCount++] = hi;
		}

		// after the bars, so the lines land on top of them
		if (boundCount > 0) {

			ImPlotSpec boundsSpec;
			boundsSpec.LineColor = boundsColour;
			boundsSpec.LineWeight = 2.0f;

			ImPlot::PlotInfLines("##bounds", bounds, boundCount, boundsSpec);
		}

		if (showLo) {
			drawBoundLabel(lo, true);
		}

		if (showHi) {
			drawBoundLabel(hi, false);
		}

		ImPlot::EndPlot();
	}

	if (outside > 0) {
		ImGui::TextDisabled(
			"%d of %d %s fall outside %.3g - %.3g",
			outside,
			(int)measured.size(),
			sampleName,
			xMin,
			xMax
		);
	}
}

void drawMeshQualityHistograms(const Quality& quality, bool& logCount) {

	// Every metric Quality owns, each over the band the matching overlay ramps across
	// (see MeshInspector::drawAspectRatio / drawElementQuality). Plane angle has no
	// overlay and no band to share: 0-180 is simply the range an angle can occupy, so
	// nothing ever falls outside it. Another vector added to Quality needs one row
	// here and nothing else.
	const struct {
		const char* name;
		const char* sampleName;
		const std::vector<double>& values;
		double xMin;
		double xMax;
	} metrics[] = {
		{ "aspect ratio", "cells", quality.aspectRatios, 1.0, 2.0 },
		{ "element quality", "cells", quality.elementQuality, 0.0, 1.0 },
		{ "plane angle (deg)", "angles", quality.planeAngles, 0.0, 180.0 },
	};

	ImGui::Checkbox("Log count", &logCount);

	for (const auto& metric : metrics) {
		drawMeshQualityHistogram(
			metric.values,
			metric.name,
			metric.sampleName,
			metric.xMin,
			metric.xMax,
			logCount
		);
	}
}

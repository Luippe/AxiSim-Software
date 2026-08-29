#pragma once

#include <vector>

class Quality;

// One histogram per metric Quality measures, stacked into the current ImGui
// window with the log-count toggle above them. logCount is owned by the caller so
// the choice survives the window being closed.
void drawMeshQualityHistograms(const Quality& quality, bool& logCount);

// A single metric's histogram, binned over a fixed [xMin, xMax] band and locked
// to it -- the same band the inspector's colour overlay ramps across, so a bar
// and a shaded cell can be read against each other. Values with no measurement
// (NaN) are dropped; measured values outside the band are counted out and
// reported under the plot rather than silently missing from it.
//
// sampleName is what one entry of values counts -- "cells" for the per-cell
// ratios, "angles" for plane angle, which stores three entries per cell.
void drawMeshQualityHistogram(
	const std::vector<double>& values,
	const char* name,
	const char* sampleName,
	double xMin,
	double xMax,
	bool logCount
);

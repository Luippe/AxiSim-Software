#pragma once

#include <vector>

class Quality;

// One histogram per metric Quality measures, stacked into the current ImGui
// window with the log-count toggle above them. logCount is owned by the caller so
// the choice survives the window being closed.
void drawMeshQualityHistograms(const Quality& quality, bool& logCount);

// A single metric's histogram. name labels the x axis; values with no measurement
// (NaN) are dropped rather than binned.
void drawMeshQualityHistogram(const std::vector<double>& values, const char* name, bool logCount);

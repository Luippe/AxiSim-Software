#pragma once
#include <string>
#include <mutex>
#include "imgui.h"
#include "implot.h"

struct Solver;

struct Plot {
    std::string name;
    std::vector<double> y;
};

struct TextPos {
    double iteration;
    double residual;
};

class ResidualPlot {

public:
	ResidualPlot(Solver& solver, std::initializer_list<std::string> residualNames);

	std::vector<std::string> names;
    std::vector<double> iterations;
    std::vector<Plot> plots;

    std::mutex mutex; // use lock to prevent solver thread from colliding with main thread

    // draw plot
	void draw();

    // reset state of residual plot before starting to plot
    void resetState();

    // template for adding residuals
    template<typename... Args>
    void add(int iter, Args... residuals) {
        constexpr size_t count = sizeof...(residuals);

        std::lock_guard<std::mutex> lock(mutex);

        if (count != names.size()) {
            throw std::runtime_error("Residual count does not match residual names.");
        }

        size_t idx = 0;

        iterations.push_back((double)iter);
        (plots[idx++].y.push_back(residuals), ...);

    }

private:

    int idx = 0;
    int p = 0;

    ImPlotSpec marker;


    std::vector<TextPos> clickedPos;

    Solver& solver;

    // setup axes
    void setupAxes();

    // handle events such as mouse clicks
    void handleEvents();
};
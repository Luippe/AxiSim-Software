#pragma once
#include <string>
#include <mutex>
#include <array>
#include "imgui.h"
#include "implot.h"
#include "solver_struct.h"


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
	ResidualPlot(Solver& solver);

    // add structs
    struct ResidualPlotTab {
        int id = 0;
        std::string name;
        std::vector<double> iterations;
        std::vector<Plot> plots;

        bool newlyCreated = true;
    };



    std::vector<std::string> names;

    std::mutex mutex; // use lock to prevent solver thread from colliding with main thread

    // main draw function
	void draw();

    // reset state of residual plot before starting to plot
    void resetState();

    // clear current plot
    void clearPlot();

    // set the variables which will be plotted onto the current plot
    void setName(const std::array<ResidualPrintItem, 6>& residualsToPlot);

    // add residuals to current plot
    template <size_t N>
    void add(int currentIteration, const std::array<ResidualPrintItem, N>& residualsToPrint) {

        std::lock_guard<std::mutex> lock(mutex);
        size_t idx = 0;
        tabs[activeTabID].iterations.push_back((double)currentIteration);
        for (const ResidualPrintItem& item : residualsToPrint) {
            if (!item.enabled) continue;
            
            tabs[activeTabID].plots[idx++].y.push_back(*item.residual);
        }
    }


private:


    int nextTabID = 1;
    int activeTabID = 1;
    int idx = 0;
    int p = 0;

    ImPlotSpec marker;

    std::vector<ResidualPlotTab> tabs;
    std::vector<TextPos> clickedPos;

    Solver& solver;

    // draw tabs
    void drawTabs(ImGuiID dockspaceID, const ImGuiWindowClass& residualClass);

    // setup axes
    void setupAxes();

    // handle events such as mouse clicks
    void handleEvents(ResidualPlotTab& tab);

    // add a new tab
    void addTab();

    // draw the specified tab
    void drawPlotTab(ResidualPlotTab& tab);
};
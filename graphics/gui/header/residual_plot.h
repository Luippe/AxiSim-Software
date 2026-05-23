#pragma once
#include <string>
#include <mutex>
#include <array>
#include "imgui.h"
#include "implot.h"
#include "solver_struct.h"
#include "buffer_manager.h"

class Solver;
struct AppAssets;

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
	ResidualPlot(Solver& solver, AppAssets& assets);

    // add structs
    struct ResidualPlotTab {
        int id = 0; // unique id per tab
        std::string name;

        ImPlotRect currentLimits;

        std::vector<double> iterations;
        std::vector<TextPos> clickedPos;
        std::vector<Plot> plots;

        bool newlyCreated = true;
        bool resetView = false;
        bool copyImageNextFrame = false;

        ImGuiID targetDockID = 0;   // current dock id
    };

    // copy to clipboard variables
    bool pendingCopy = false;
    bool consoleCopy = false;
    int pendingCopyTabID = 0;
    int pendingCopyWidth = 1600;
    int pendingCopyHeight = 420;

    std::mutex mutex; // use lock to prevent solver thread from colliding with main thread

    // main draw function
	void draw();

    // reset state of residual plot before starting to plot
    void resetState();

    // clear current plot of points
    void clearPlot(int i);

    // set the variables which will be plotted onto the current plot
    void setName(const std::array<ResidualPrintItem, 6>& residualsToPlot);

    // copy image to clipboard. DO NOT NEST INSIDE ANOTHER IMGUI FRAME
    bool copyActivePlotToClipboard();

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



    // toolbar icon variables
    float toolbarHeight = 25.0f;
    float iconSize = 15.0f;


    ImPlotSpec marker;
    std::vector<ResidualPlotTab> tabs;

    Solver& solver;
    AppAssets& assets;
    FrameBuffer offScreenFBO;

    // draw toolbar
    void drawToolBar(ResidualPlotTab& tab, int i, ImGuiID currentDockID, ImGuiID& pendingAddDockID, ImGuiID dockspaceID);

    // draw tabs
    void drawTabs(ImGuiID dockspaceID, const ImGuiWindowClass& residualClass);

    // setup axes
    void setupAxes();

    // handle events on each plot such as mouse clicks
    void handlePlotEvents(ResidualPlotTab& tab);

    // handle key presses
    void handleKeyEvents();

    // add a new tab
    void addTab(ImGuiID targetDockID);

    // draw the specified tab
    void drawPlot(ResidualPlotTab& tab);

    // draw only the plot data. must be inside a ImGui::BeginPlot
    void drawPlotData(ResidualPlotTab& tab);
};
#pragma once
#include <string>
#include <mutex>
#include <array>
#include "imgui.h"
#include "implot.h"

#include "base_surface_viewer.h"
#include "flag_manager.h"
#include "solver_struct.h"
#include "buffer_manager.h"

class Solver;
struct AppConfig;
struct AppAssets;

struct Plot {
    std::string name;
    std::vector<double> y;
};

struct TextPos {
    double iteration;
    double residual;
};

class ResidualPlot : public ToolbarHost {

public:
	ResidualPlot(Solver& solver, AppConfig& appConfig);

    // create dockspace to have multiple tabs
    DockingSpace residualDockSpace{ UIViewport::ResidualPlotTitle, "Residual Plot" };

    // add structs
    struct ResidualPlotTab : public DockingSpace::DockTab {
        ImPlotRect currentLimits;
        std::vector<double> iterations;
        std::vector<TextPos> clickedPos;
        std::vector<Plot> plots;
    };

    // copy to clipboard variables
    bool pendingCopy = false;
    bool consoleCopy = false;
    int pendingCopyTabID = 0; // DockTab::id, not a slot in `tabs`
    int pendingCopyWidth = 1600;
    int pendingCopyHeight = 420;

    std::mutex mutex; // use lock to prevent solver thread from colliding with main thread

    // main draw function
	void draw();

    // Drawn by GUI into the app-wide toolbar strip above the dockspace. Acts on
    // whichever plot tab is currently selected (residualDockSpace's active tab).
    void drawAppToolBar();

    // reset state of residual plot before starting to plot
    void resetState();

    // clear current plot of points
    void clearPlot(int i);

    // set the variables which will be plotted onto the current plot
    void setName(const std::unordered_map<std::string, ConfigResidual>& configResiduals);

    // copy image to clipboard. DO NOT NEST INSIDE ANOTHER IMGUI FRAME
    bool copyActivePlotToClipboard();

    // add residuals to current plot
    void add(int currentIteration, const std::unordered_map<std::string, ConfigResidual>& configResiduals);

private:

    int idx = 0;
    int p = 0;

    // Set by the app toolbar's Add button. The dock to add into is only known
    // once drawTabs() is running, and the strip is drawn before that, so the
    // request is parked here and consumed on this frame's draw().
    bool pendingAddTab = false;

    // slot in `tabs` for a DockTab::id, or -1 if no tab carries that id
    int tabIndexFromID(int id);

    // index into `tabs` of the plot the app toolbar acts on, or -1 if none
    int activeTabIndex();


    ImPlotSpec marker;
    std::vector<ResidualPlotTab> tabs;

    Solver& solver;
    AppAssets& assets;
    FrameBuffer offScreenFBO;

    // setup axes
    void setupAxes();

    // handle events on each plot such as mouse clicks
    void handlePlotEvents(ResidualPlotTab& tab);

    // handle key presses
    void handleKeyEvents();

    // draw the specified tab
    void drawPlot(ResidualPlotTab& tab);

    // draw only the plot data. must be inside a ImGui::BeginPlot
    void drawPlotData(ResidualPlotTab& tab);
};
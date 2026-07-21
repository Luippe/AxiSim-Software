#pragma once
#include <filesystem>
#include <string>
#include <vector>

#include "video_writer.h"	// VideoWriter member below

struct SceneView;
class Console;
class Inspector;
class Project;
class GUI;

// Playback control for a transient run. It owns no field data of its own: the
// frames live in Results (built from Solver::timeFrames), and this only decides
// WHICH frame is on screen, then asks Results to push it into the live fields.
class AnimationGUI {
public:

	AnimationGUI(Project& project, GUI& gui);

	void render();

	// ---------------- export ----------------
	//
	// Frames are captured one per app frame rather than in a single blocking
	// loop: each has to be pushed into the live fields and re-rendered offscreen
	// by the Inspector, and spreading that over frames keeps the window
	// responsive and lets the console report progress. GUI drives the capture
	// itself (see pendingExportFrame) because the offscreen pass needs the
	// export ImGui context, which is only swapped in after ImGui::Render().

	// What the captured frames are turned into. Chosen from the extension the
	// user picked in the save dialog.
	enum class ExportFormat {
		MP4,			// one H.264 file, written as the frames arrive
		PNGSequence		// frame_0000.png ... in a folder beside the chosen name
	};

	// Begin exporting to `target`. A .png target writes a numbered sequence into
	// a folder of its own beside it; anything else writes an mp4 to that exact
	// path. No-op when there is nothing to play or an export is already running.
	void beginExport(const std::filesystem::path& target);

	bool isExporting() const { return exporting; }

	// Set on the frames where the staged animation frame has been pushed into the
	// fields and is ready to be captured. GUI clears it when it takes the shot.
	bool pendingExportFrame = false;

	// Hand over the pixels Inspector just captured (bottom-up RGBA, empty on
	// failure), write them out, and move to the next frame -- finishing the
	// export after the last one.
	void onFrameCaptured(const std::vector<unsigned char>& pixels);

private:

	Project& project;
	SceneView& scene;
	Console& console;

	// the 2D results panel is what an export re-renders offscreen, one frame at
	// a time; the 3D scene has no offscreen path
	Inspector& inspector;

	int currentFrame = 0;
	int previousFrame = -1;		// -1 forces the first frame to be applied
	bool isPlaying = false;
	int maxFPS = 60;
	int minFPS = 1;
	int fps = 10;
	float accumulator = 0.0f;

	// ---------------- export state ----------------
	bool exporting = false;
	int exportIndex = 0;			// frame being written this app frame
	int exportFailures = 0;
	int exportFPS = 10;				// fps as it stood when the export started
	int captureWidth = 0;			// frame size latched by Inspector at the start
	int captureHeight = 0;

	ExportFormat format = ExportFormat::MP4;
	std::filesystem::path exportDir;		// PNGSequence: folder holding the frames
	std::filesystem::path exportTarget;		// MP4: the file being written
	VideoWriter video;

	// path for the frame currently staged (PNGSequence only)
	std::filesystem::path exportFramePath() const;

	// wrap up after the last frame, or after a failure that ended it early
	void finishExport();

	// stop an export early, reporting why (e.g. the frames went away mid-run)
	void cancelExport(const std::string& reason);

	// advance currentFrame based on time accumulated since the last draw
	void updateCurrentFrame(int frameCount);

	// handle mouse and keyboard events
	void handleEvents(int frameCount);

};

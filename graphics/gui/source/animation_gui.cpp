#include "animation_gui.h"

#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <system_error>

#include "imgui.h"

#include "project.h"
#include "gui.h"

#include "console.h"
#include "image_writer.h"
#include "inspector.h"
#include "scene_view.h"


namespace {

    // filesystem::path::string() converts with the active locale and throws when a
    // character does not survive it; the console renders UTF-8, so go through
    // u8string instead and a path with accents or CJK in it still prints.
    std::string toConsoleText(const std::filesystem::path& path) {
        std::u8string utf8 = path.u8string();
        return std::string(utf8.begin(), utf8.end());
    }
}

AnimationGUI::AnimationGUI(Project& project, GUI& gui) :
    project(project),
    scene(gui.scene),
    console(gui.console),
    inspector(gui.inspector) {

}

void AnimationGUI::updateCurrentFrame(int frameCount) {

    if (!isPlaying || frameCount <= 0) {
        return;
    }

    float dt = ImGui::GetIO().DeltaTime;
    accumulator += dt;

    float frameTime = 1.0f / (float)fps;

    while (accumulator >= frameTime) {
        accumulator -= frameTime;

        currentFrame++;

        if (currentFrame >= frameCount) {
            currentFrame = 0; // loop animation
        }
    }
}

void AnimationGUI::handleEvents(int frameCount) {

    if (!ImGui::IsWindowFocused()) return;

    // playback keys would fight the export for the frame index
    if (exporting) return;

    // play/pause
    if (ImGui::IsKeyPressed(ImGuiKey_Space)) {
        isPlaying = !isPlaying;
    }

    // step forward a frame
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
        currentFrame = std::clamp(currentFrame + 1, 0, frameCount - 1);
    }

    // step back a frame
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
        currentFrame = currentFrame - 1 < 0 ? 0 : currentFrame - 1;
    }
}

// ======================================================================
// -----------------------EXPORT-----------------------------------------
// ======================================================================
void AnimationGUI::beginExport(const std::filesystem::path& target) {

    if (exporting) {
        console.addLine("an animation export is already running");
        return;
    }

    Results& results = project.results;

    if (!results.hasAnimation()) {
        console.addLine("nothing to export -- a transient run has to capture frames first");
        return;
    }

    if (target.empty()) {
        return;
    }

    // fixes the frame size for the whole export, and fails when the results view
    // has never been drawn and so has no size to take
    if (!inspector.beginExportSequence()) {
        console.addLine("open the Results tab once before exporting -- the view has no size yet");
        return;
    }

    captureWidth = inspector.sequenceWidth();
    captureHeight = inspector.sequenceHeight();

    std::wstring extension = target.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), ::towlower);

    format = (extension == L".png")
        ? ExportFormat::PNGSequence
        : ExportFormat::MP4;

    const int frameCount = (int)results.animationFrames.size();

    if (format == ExportFormat::PNGSequence) {

        // The dialog names one PNG, but a sequence is a few hundred of them, so
        // they get a folder of their own beside the name that was picked instead
        // of being tipped into it.
        exportDir = target.parent_path() / (target.stem().wstring() + L"_frames");

        std::error_code ec;
        std::filesystem::create_directories(exportDir, ec);

        if (ec) {
            console.addLine("could not create export folder: " + toConsoleText(exportDir));
            return;
        }

        console.addLine(
            "exporting " + std::to_string(frameCount) +
            " frames to " + toConsoleText(exportDir)
        );
    }
    else {

        exportTarget = target;

		if (!video.open(target, captureWidth, captureHeight, fps)) {
			console.addLine(VideoWriter::supported()
				? "could not start the video encoder -- on Windows N/KN editions "
				  "this needs the Media Feature Pack. Export as .png instead to get "
				  "the frames."
				: "MP4 export is not available in this build; choose .png to export "
				  "a numbered frame sequence."
			);
            return;
        }

        console.addLine(
            "encoding " + std::to_string(frameCount) +
            " frames to " + toConsoleText(target)
        );
    }

    exportIndex = 0;
    exportFailures = 0;
    exportFPS = fps;
    exporting = true;
    isPlaying = false;
}

std::filesystem::path AnimationGUI::exportFramePath() const {

    wchar_t name[32];
    std::swprintf(name, sizeof(name) / sizeof(name[0]), L"frame_%04d.png", exportIndex);

    return exportDir / name;
}

void AnimationGUI::cancelExport(const std::string& reason) {

    if (!exporting) {
        return;
    }

    exporting = false;
    pendingExportFrame = false;

    if (format == ExportFormat::MP4) {
        video.close();
    }

    console.addLine("animation export stopped: " + reason);
}

void AnimationGUI::onFrameCaptured(const std::vector<unsigned char>& pixels) {

    if (!exporting) {
        return;
    }

    // an empty capture means the offscreen render itself failed
    bool ok = !pixels.empty();

    if (ok) {
		ok = (format == ExportFormat::PNGSequence)
			? writeRGBAToPNG(exportFramePath(), pixels.data(), captureWidth, captureHeight)
            : video.writeFrame(pixels.data());
    }

    if (!ok) {
        exportFailures++;
    }

    // A dead encoder cannot recover, and every later frame would fail the same
    // way, so stop rather than grinding through the rest of the run.
    if (!ok && format == ExportFormat::MP4) {
        exportIndex++;
        finishExport();
        return;
    }

    exportIndex++;

    if (exportIndex >= (int)project.results.animationFrames.size()) {
        finishExport();
    }
}

void AnimationGUI::finishExport() {

    exporting = false;

    const int written = exportIndex - exportFailures;

    if (format == ExportFormat::MP4) {

        // Finalize is what writes the mp4 index; a file that never gets it is
        // unplayable however many frames went in, so its result is the one that
        // decides whether this worked.
        bool finalized = video.close();

        if (finalized && exportFailures == 0) {
            console.addLine("wrote " + toConsoleText(exportTarget));

            if (video.encodedWidth() != captureWidth || video.encodedHeight() != captureHeight) {
                console.addLine(
                    "  (cropped to " + std::to_string(video.encodedWidth()) + "x" +
                    std::to_string(video.encodedHeight()) + " -- H.264 needs even dimensions)"
                );
            }
        }
        else {
            console.addLine(
                "video export failed after " + std::to_string(written) +
                " frames -- the file is incomplete"
            );
        }

        return;
    }

    if (exportFailures > 0) {
        console.addLine(
            "exported " + std::to_string(written) + " of " +
            std::to_string(exportIndex) + " frames -- " +
            std::to_string(exportFailures) + " could not be written"
        );
    }
    else {
        console.addLine("exported " + std::to_string(exportIndex) + " frames");
    }
}

// ======================================================================
// -----------------------MAIN DRAW LOOP---------------------------------
// ======================================================================
void AnimationGUI::render() {

    Results& results = project.results;

    // A steady run, or a transient run that captured a single frame, has nothing
    // to play -- leave the results views showing the final state untouched.
    if (!results.hasAnimation()) {
        cancelExport("the frames are gone");
        return;
    }

    const int frameCount = (int)results.animationFrames.size();

    // A re-solve can shorten the run, so never trust the previous index.
    currentFrame = std::clamp(currentFrame, 0, frameCount - 1);

    ImGui::SetNextWindowSize(ImVec2(scene.rectSize.x * 0.33f, 70.0f), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(scene.rectPos.x + scene.rectSize.x * 0.33f, scene.rectPos.y + scene.rectSize.y - 90), ImGuiCond_Always);

    ImGui::Begin("Animation", nullptr, UIFlags::AnimationWindowFlags);

    handleEvents(frameCount);

    // An export owns the frame index while it runs -- one frame per app frame, so
    // GUI's capture below sees each of them exactly once whatever the play rate is.
    if (exporting) {

        if (exportIndex >= frameCount) {
            cancelExport("the run got shorter while exporting");
        }
        else {
            isPlaying = false;
            currentFrame = exportIndex;
        }
    }

    ImGui::BeginDisabled(exporting);

    if (ImGui::Button(isPlaying ? "Pause" : "Play")) {
        isPlaying = !isPlaying;
    }

    ImGui::SameLine();

    if (ImGui::Button("Reset")) {
        currentFrame = 0;
        accumulator = 0.0f;
        isPlaying = false;
    }

    ImGui::SameLine();

    if (exporting) {
        ImGui::Text("exporting frame %d / %d", exportIndex + 1, frameCount);
    }
    else {
        ImGui::Text("t = %.4g   (%d fps)", results.animationFrames[currentFrame].time, fps);
    }

    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(255, 255, 255, 180));

    float plusW = ImGui::CalcTextSize("+").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    float minusW = ImGui::CalcTextSize("-").x + ImGui::GetStyle().FramePadding.x * 2.0f;

    float availW = ImGui::GetContentRegionAvail().x;
    float sliderW = availW - plusW - minusW;

    ImGui::SetNextItemWidth(sliderW);
    ImGui::SliderInt("##Frame", &currentFrame, 0, frameCount - 1);

    ImGui::SameLine(0.0f, 0.0f);
    if (ImGui::Button("-##frame")) {
        fps = std::max(minFPS, fps - 1);
    }

    ImGui::SameLine(0.0f,0.0f);
    if (ImGui::Button("+##frame")) {
        fps = std::min(maxFPS, fps + 1);
    }

    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    ImGui::EndDisabled();

    updateCurrentFrame(frameCount);

    // update whenever frame changes. previousFrame starts at -1 so the first draw
    // always applies a frame, otherwise the views would keep showing the final
    // state until the user scrubbed.
    if (currentFrame != previousFrame) {
        results.showAnimationFrame(currentFrame);
        previousFrame = currentFrame;
    }

    // Stage the capture only now that the frame's values are in the fields, so the
    // offscreen re-render after ImGui::Render() draws this frame and not the last.
    if (exporting) {
        pendingExportFrame = true;
    }

	ImGui::End();
}

#include "animation_gui.h"

#include <algorithm>

#include "imgui.h"

#include "project.h"
#include "gui.h"

#include "scene_view.h"


AnimationGUI::AnimationGUI(Project& project, GUI& gui) :
    project(project),
    scene(gui.scene) {

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
// -----------------------MAIN DRAW LOOP---------------------------------
// ======================================================================
void AnimationGUI::render() {

    Results& results = project.results;

    // A steady run, or a transient run that captured a single frame, has nothing
    // to play -- leave the results views showing the final state untouched.
    if (!results.hasAnimation()) {
        return;
    }

    const int frameCount = (int)results.animationFrames.size();

    // A re-solve can shorten the run, so never trust the previous index.
    currentFrame = std::clamp(currentFrame, 0, frameCount - 1);

    ImGui::SetNextWindowSize(ImVec2(scene.rectSize.x * 0.33f, 70.0f), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(scene.rectPos.x + scene.rectSize.x * 0.33f, scene.rectPos.y + scene.rectSize.y - 90), ImGuiCond_Always);

    ImGui::Begin("Animation", nullptr, UIFlags::AnimationWindowFlags);

    handleEvents(frameCount);

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
    ImGui::Text("t = %.4g   (%d fps)", results.animationFrames[currentFrame].time, fps);

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

    updateCurrentFrame(frameCount);

    // update whenever frame changes. previousFrame starts at -1 so the first draw
    // always applies a frame, otherwise the views would keep showing the final
    // state until the user scrubbed.
    if (currentFrame != previousFrame) {
        results.showAnimationFrame(currentFrame);
        previousFrame = currentFrame;
    }

	ImGui::End();
}

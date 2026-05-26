#include "animation_gui.h"
#include "imgui.h"
#include "scene_view.h"
#include "file_manager.h"
#include "printer.h"
#include "gui_manager.h"

AnimationGUI::AnimationGUI(SceneView& scene) :
scene(scene) {

}

void AnimationGUI::loadAnimation(const std::string& filename) {

	std::ifstream in(filename, std::ios::binary);

    frames.clear();

    std::vector<double> dr, dz;
    readAll(in, nr, nz, dr, dz);

    BoundaryConditionConfig uBC;
    BoundaryConditionConfig vBC;
    BoundaryConditionConfig pBC;
    readBoundaryConditionConfig(in, uBC, vBC, pBC);

    SolutionField uSol{{}, nr, nz + 1, dr, dz, CellStoreType::AXIAL};
    SolutionField vSol{{}, nr + 1, nz, dr, dz, CellStoreType::RADIAL };
    SolutionField pSol{{}, nr, nz, dr, dz, CellStoreType::CENTER };

    Field uField{ nz, nr };
    Field vField{ nz, nr };
    Field pField{ nz, nr };

    MinMaxGlobal uMinMax;
    MinMaxGlobal vMinMax;
    MinMaxGlobal pMinMax;

	while (true) {
		FlowFrame frame;

		if (!readBinary(in, frame.time)) break;
        if (!readBinary(in, uSol.field, vSol.field, pSol.field)) break;

        uField.generate(uSol, uBC);
        vField.generate(vSol, vBC);
        pField.generate(pSol, pBC);

        uMinMax.vmin = std::min(uField.vmin, uMinMax.vmin);
        uMinMax.vmax = std::max(uField.vmax, uMinMax.vmax);

        vMinMax.vmin = std::min(vField.vmin, vMinMax.vmin);
        vMinMax.vmax = std::max(vField.vmax, vMinMax.vmax);

        pMinMax.vmin = std::min(pField.vmin, pMinMax.vmin);
        pMinMax.vmax = std::max(pField.vmax, pMinMax.vmax);

        frame.fields.push_back(uField);
        frame.fields.push_back(vField);
        frame.fields.push_back(pField);
       
		frames.push_back(std::move(frame));

	}

    minmaxGlobals.push_back(uMinMax);
    minmaxGlobals.push_back(vMinMax);
    minmaxGlobals.push_back(pMinMax);

    updateCurrentField();
    isReady = true;
}

void AnimationGUI::updateCurrentFrame() {
    if (!isPlaying || frames.empty()) {
        return;
    }

    float dt = ImGui::GetIO().DeltaTime;
    accumulator += dt;

    float frameTime = 1.0f / (float)fps;

    while (accumulator >= frameTime) {
        accumulator -= frameTime;

        currentFrame++;

        if (currentFrame >= (int)(frames.size())) {
            currentFrame = 0; // loop animation
        }
    }
}

void AnimationGUI::updateCurrentField() {

    Field& currentField = frames[currentFrame].fields[scene.results.currentItem];
    scene.results.currentField = &currentField;
    scene.results.updateTextureBuffer(currentField.vertexValues.data());

    scene.results.currentField->setMinMax(
        minmaxGlobals[scene.results.currentItem].vmin,
        minmaxGlobals[scene.results.currentItem].vmax
    );

}

void AnimationGUI::handleEvents() {

    if (!ImGui::IsWindowFocused()) return;

    // play/pause
    if (ImGui::IsKeyPressed(ImGuiKey_Space)) {
        isPlaying = !isPlaying;
    }

    // step forward a frame
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
        currentFrame = std::clamp(currentFrame + 1, 0, (int)(frames.size() - 1));
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

    ImGui::SetNextWindowSize(ImVec2(scene.rectSize.x * 0.33f, 70.0f), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(scene.rectPos.x + scene.rectSize.x * 0.33f, scene.rectPos.y + scene.rectSize.y - 90), ImGuiCond_Always);

    ImGui::Begin("Animation", nullptr, UIFlags::AnimationWindowFlags);

    if (frames.empty()) {
        ImGui::Text("No animation loaded.");
        if (ImGui::Button("load")) {
            loadAnimation("flow_motion.bin");
        }
        ImGui::End();
        return;
    }

    handleEvents();

    if (ImGui::Button(isPlaying ? "Pause" : "Play")) {
        isPlaying = !isPlaying;
    }

    ImGui::SameLine();

    if (ImGui::Button("Reset")) {
        currentFrame = 0;
        accumulator = 0.0f;
        isPlaying = false;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(255, 255, 255, 180));

    float plusW = ImGui::CalcTextSize("+").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    float minusW = ImGui::CalcTextSize("-").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    float spacing = ImGui::GetStyle().ItemSpacing.x;

    float availW = ImGui::GetContentRegionAvail().x;
    float sliderW = availW - plusW - minusW;

    ImGui::SetNextItemWidth(sliderW);
    ImGui::SliderInt("##Frame", &currentFrame, 0, frames.size() - 1);

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

    updateCurrentFrame();

    // update whenever frame changes
    if (currentFrame != previousFrame && isReady) {
        updateCurrentField();
        previousFrame = currentFrame;
    }

	ImGui::End();
}
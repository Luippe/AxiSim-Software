#include "animation_gui.h"
#include "imgui.h"
#include "scene_view.h"
#include "manage_file.h"
#include "printer.h"


AnimationGUI::AnimationGUI(SceneView& scene) :
scene(scene) {

}


void AnimationGUI::loadAnimation(const std::string& filename) {

	std::ifstream in(filename, std::ios::binary);

    frames.clear();

    float vmin, vmax;
    double dr, dz;
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

    //vmin = *std::min_element(processedData.begin(), processedData.end());
    //vmax = *std::max_element(processedData.begin(), processedData.end());
    float vminTemp, vmaxTemp;

	while (true) {
		FlowFrame frame;

		if (!readBinary(in, frame.time)) break;

        if (!readBinary(in, uSol.field, vSol.field, pSol.field)) break;

        uField.generate(uSol, uBC);
        vField.generate(vSol, vBC);
        pField.generate(pSol, pBC);

        vminTemp = *std::min_element(uField.processedData.begin(), uField.processedData.end());
        vmaxTemp = *std::max_element(uField.processedData.begin(), uField.processedData.end());
        uField.vmin = std::min(uField.vmin, vminTemp);
        uField.vmax = std::max(uField.vmax, vmaxTemp);

        vminTemp = *std::min_element(vField.processedData.begin(), vField.processedData.end());
        vmaxTemp = *std::max_element(vField.processedData.begin(), vField.processedData.end());
        vField.vmin = std::min(vField.vmin, vminTemp);
        vField.vmax = std::max(vField.vmax, vmaxTemp);

        vminTemp = *std::min_element(pField.processedData.begin(), pField.processedData.end());
        vmaxTemp = *std::max_element(pField.processedData.begin(), pField.processedData.end());
        pField.vmin = std::min(pField.vmin, vminTemp);
        pField.vmax = std::max(pField.vmax, vmaxTemp);

        frame.u = uField.processedData;
        frame.v = vField.processedData;
        frame.p = pField.processedData;

		frames.push_back(std::move(frame));
	}

    textureBuffer.createBuffer(GL_R32F, nz + 1, nr + 1, GL_RED, GL_FLOAT, nullptr);

    isReady = true;
}

void AnimationGUI::updateFlowAnimation() {
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

void AnimationGUI::updateAnimationTexture() {

    scene.results.currentField->textureBuffer.updateBuffer(nz + 1, nr + 1, GL_RED, GL_FLOAT, frames[currentFrame][scene.results.currentItem].data());

}

void AnimationGUI::render() {

    ImGui::SetNextWindowSize(ImVec2(scene.rectSize.x * 0.3f, 80.0f), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(scene.rectPos.x + 0.3f * scene.rectSize.x, scene.rectPos.y + scene.rectSize.y - 90), ImGuiCond_Always);

    ImGui::Begin("Animation", 
        nullptr, 
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse);

    if (frames.empty()) {
        ImGui::Text("No animation loaded.");
        if (ImGui::Button("load")) {
            loadAnimation("flow_motion.bin");
        }
        ImGui::End();
        return;
    }

    if (ImGui::IsWindowFocused()) {
        if (ImGui::IsKeyPressed(ImGuiKey_Space)) {
            isPlaying = !isPlaying;
        }
    }

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

    int endFrame = frames.size() - 1;
    ImGui::SliderInt("##Frame", &currentFrame, 0, endFrame);

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


    if (isReady) {
        updateFlowAnimation();
        updateAnimationTexture();
    }

	ImGui::End();
}
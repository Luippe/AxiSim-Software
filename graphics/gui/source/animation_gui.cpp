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

    double dr = 0;
    double dz = 0;

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

	while (true) {
		FlowFrame frame;

		if (!readValue(in, frame.time)) break;

        if (!readVector(in, uSol.field)) break;
        if (!readVector(in, vSol.field)) break;
        if (!readVector(in, pSol.field)) break;

        uField.generate(uSol, uBC);
        vField.generate(vSol, vBC);
        pField.generate(pSol, pBC);

        frame.u = uField.processedData;
        frame.v = vField.processedData;
        frame.p = pField.processedData;

		frames.push_back(std::move(frame));
	}

    textureBuffer.createBuffer(GL_R32F, nz + 1, nr + 1, GL_RED, GL_FLOAT, nullptr);

    isReady = true;
}
void AnimationGUI::updateFlowAnimation() {
    if (!playing || frames.empty()) {
        return;
    }

    float dt = ImGui::GetIO().DeltaTime;
    accumulator += dt;

    float frameTime = 1.0f / (float)fps;

    while (accumulator >= frameTime) {
        accumulator -= frameTime;

        currentFrame++;

        if (currentFrame >= static_cast<int>(frames.size())) {
            currentFrame = 0; // loop animation
        }
    }
}

void AnimationGUI::updateAnimationTexture() {

    textureBuffer.updateBuffer(nz + 1, nr + 1, GL_RED, GL_FLOAT, frames[currentFrame].u.data());
    scene.results.setCurrentTextureBuffer(textureBuffer);
}

void AnimationGUI::render() {

    ImGui::Begin("Animation");

    if (frames.empty()) {
        ImGui::Text("No animation loaded.");
        if (ImGui::Button("load")) {
            loadAnimation("flow_motion.bin");
        }
        ImGui::End();
        return;
    }

    if (ImGui::Button(playing ? "Pause" : "Play")) {
        playing = !playing;
    }

    ImGui::SameLine();

    if (ImGui::Button("Reset")) {
        currentFrame = 0;
        accumulator = 0.0f;
        playing = false;
    }

    ImGui::SliderInt("FPS", &fps, 1, 60);

    int maxFrame = (int)(frames.size()) - 1;

    ImGui::SliderInt("Frame", &currentFrame, 0, maxFrame);

    const FlowFrame& frame = frames[currentFrame];

    ImGui::Text("Time: %.6f s", frame.time);
    ImGui::Text("Frame: %d / %d", currentFrame, maxFrame);

    if (isReady) {
        updateFlowAnimation();
        updateAnimationTexture();
    }

	ImGui::End();
}
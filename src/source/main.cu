#include "mesh.h"
#include "camera.h"
#include "manage_file.h"
#include "setting.cuh"
#include "display.h"
#include "shader.h"
#include "renderer.h"
#include "gui.h"
#include "scene_view.h"
#include "graphics_struct.h"
#include "solver.h"
#include <GLFW/glfw3.h>
#include "imgui_impl_opengl3.h"

int main() {

	Camera camera;
	Display disp(camera);

	Renderer renderer;
	Bounding bound(renderer);

	SceneView scene(disp, camera, renderer, bound);

	GUI gui(disp.window, scene);
	double prevTime = glfwGetTime();
	int frameCount = 0;

	int counter = 0;


	while (!glfwWindowShouldClose(disp.window)) {
		glfwPollEvents();

		double currentTime = glfwGetTime();
		frameCount++;
		if (currentTime - prevTime >= 1.0) { // If last update was more than 1 second ago
			//std::cout << "FPS: " << frameCount << std::endl;

			frameCount = 0;
			prevTime = currentTime;
		}

		gui.newFrame();


		// draw scene
		scene.render();

		//if (counter == 0) {

		//	counter++;
		//}
		//scene.solver.runSimple();

		//int displayWidth, displayHeight;
		//glfwGetFramebufferSize(disp.window, &displayWidth, &displayHeight);
		//glViewport(0, 0, displayWidth, displayHeight);
		//glClearColor(0.4f, 0.4f, 0.4f, 1.0f);
		//glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		int displayWidth, displayHeight;
		glfwGetFramebufferSize(disp.window, &displayWidth, &displayHeight);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glViewport(0, 0, displayWidth, displayHeight);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		//glm::vec3 pos =  camera.getPosition();
		gui.render();

		// check and call events and swap the buffers
		glfwSwapBuffers(disp.window);
		//break;
	}

	// properly shutdown
	scene.solver.shutdown();
	cudaStreamDestroy(scene.solver.stream);
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
	glfwDestroyWindow(disp.window);
	glfwTerminate();

    return 0;
}

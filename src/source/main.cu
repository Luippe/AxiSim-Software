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

struct ClassContext {
	Camera* camera;
	Display* disp;
	SceneView* scene;
	GUI* gui;
};

// mouse movement callbacks
bool firstMouse = true;
double lastx = 400.0, lasty = 400.0;
double initx = 400.0, inity = 400.0;

// mouse callback
bool dragging = false;
bool rotating = false;
bool showPointValue = true;

int main() {

	Camera camera;
	Display disp(camera);

	Renderer renderer;
	Bounding bound(renderer);

	SceneView scene(disp, camera, renderer, bound);

	//load_velocity(scene.solver.g, scene.solver.f);

	GUI gui(disp.window, scene);
	double prevTime = glfwGetTime();
	int frameCount = 0;

	// store classes needed in callback function
	ClassContext classes{ &camera, &disp, &scene, &gui};
	glfwSetWindowUserPointer(disp.window, &classes);

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
	}

	// properly shutdown
	scene.solver.shutdown();
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
	glfwDestroyWindow(disp.window);
	glfwTerminate();

    return 0;
}

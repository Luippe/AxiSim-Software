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

// callback to zoom in and out when scroll
void scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {

	ClassContext* classes = static_cast<ClassContext*>(glfwGetWindowUserPointer(window));
	if (classes->scene->hovered && classes->scene->focused) {
		classes->camera->fov -= (float)yoffset;
		classes->camera->calculateZoom(yoffset);
		if (classes->camera->fov < 1.0f)	classes->camera->fov = 1.0f;
		if (classes->camera->fov > 90.0f)	classes->camera->fov = 90.0f;
	}
}

// callback to look around when mouse moves
void mouseCallback(GLFWwindow* window, double xpos, double ypos) {

	ImGuiIO& io = ImGui::GetIO();
	ImGui_ImplGlfw_CursorPosCallback(window, xpos, ypos);
	ClassContext* classes = static_cast<ClassContext*>(glfwGetWindowUserPointer(window));

	if (classes->scene->hovered && classes->scene->focused) {
		
		if (firstMouse) {
			lastx = xpos;
			lasty = ypos;
			firstMouse = false;
		}

		if (dragging) {

			float dx = (float)(xpos - lastx);
			float dy = (float)(lasty - ypos); // reversed since y-coordinates go from bottom to top
			classes->camera->calculatePan(dx, dy);

		}

		if (rotating) {
			classes->camera->calculateRotation(glm::vec2((float)lastx, (float)lasty), glm::vec2((float)xpos, (float)ypos));
		}
		lastx = xpos;
		lasty = ypos;
	}
}

// callback mouse button callback
void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {

	ImGuiIO& io = ImGui::GetIO();
	ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);
	
	ClassContext* classes = static_cast<ClassContext*>(glfwGetWindowUserPointer(window));

	if (classes->scene->hovered && classes->scene->focused) {
		if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
			glfwGetCursorPos(window, &initx, &inity);
			dragging = true;
		}

		if (button == GLFW_MOUSE_BUTTON_MIDDLE && action == GLFW_PRESS) {
			rotating = true;
		}

		// register as clicked on screen
		if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE) {

			classes->scene->picker.pick();

		}

		// if any of the mouse button is released, reset boolean variables
		if (action == GLFW_RELEASE) {
			dragging = false;
			rotating = false;
		}
	}
}

void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
	ImGuiIO& io = ImGui::GetIO();
	ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);
}

void charCallback(GLFWwindow* window, unsigned int c)
{
	ImGui_ImplGlfw_CharCallback(window, c);
}

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

	// set mouse and scroll callback
	glfwSetCursorPosCallback(disp.window, mouseCallback);
	glfwSetScrollCallback(disp.window, scrollCallback);
	glfwSetMouseButtonCallback(disp.window, mouseButtonCallback);
	glfwSetKeyCallback(disp.window, keyCallback);
	glfwSetCharCallback(disp.window, charCallback);

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

#include "display.h"
#include "camera.h"
#include <iostream>
#include <GLFW/glfw3.h>

Display::Display(Camera& camera) : camera(camera) {	// initialize shader

	// initialize display
	glfwInit();

	// get primary monitor
	GLFWmonitor* monitor = glfwGetPrimaryMonitor();
	const GLFWvidmode* mode = glfwGetVideoMode(monitor);

	glfwWindowHint(GLFW_SAMPLES, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);
	
	window = glfwCreateWindow(mode->width, mode->height, "OpenGL Demo", nullptr, nullptr);
	glfwMakeContextCurrent(window);
	glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

	// initialize glad
	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
		std::cout << "Failed to initialize GLAD" << std::endl;
	}

	// set width and height to the monitor's resolution
	glfwGetWindowSize(window, &width, &height);
	aspect = (float)width / (float)height;
	worldWidth = 2.0f * camera.zoom * aspect;
	worldHeight = 2.0f * camera.zoom;

	// enable depth testing
	glEnable(GL_DEPTH_TEST);

	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);

	glFrontFace(GL_CCW);

	// enable anti alias
	glEnable(GL_MULTISAMPLE);

	// enable vsync
	glfwSwapInterval(1);

	// tell GLFW to capture our mouse
	//glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);


}

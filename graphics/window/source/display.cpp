#include "display.h"
#include "camera.h"
#include <iostream>
#include <GLFW/glfw3.h>


Display::Display() {

	// initialize display
	glfwInit();
	
	// get monitor
	GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);
	GLFWmonitor* monitor = monitors[monitorIndex];
	const GLFWvidmode* mode = glfwGetVideoMode(monitor);

	//for (int i = 0; i < monitorCount; i++) {
	//	int x, y;
	//	glfwGetMonitorPos(monitors[i], &x, &y);

	//	const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);

	//	std::cout << i << ": " << glfwGetMonitorName(monitors[i])
	//		<< " pos=(" << x << ", " << y << ") "
	//		<< "size=" << mode->width << "x" << mode->height
	//		<< "\n";
	//}
	
	//glfwWindowHint(GLFW_SAMPLES, 4); only enable if you want MSAA on the actual glfw window framebuffer
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);
	
	window = glfwCreateWindow(mode->width, mode->height, "AxiSim", nullptr, nullptr);

	glfwMakeContextCurrent(window);
	glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

	// initialize glad
	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
		std::cout << "Failed to initialize GLAD" << std::endl;
	}
	//std::cout << "Vendor:   " << glGetString(GL_VENDOR) << "\n";
	//std::cout << "Renderer: " << glGetString(GL_RENDERER) << "\n";
	// set width and height to the monitor's resolution
	glfwGetWindowSize(window, &width, &height);


	// enable depth testing
	glEnable(GL_DEPTH_TEST);

	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);

	glFrontFace(GL_CCW);

	// enable anti alias
	glEnable(GL_MULTISAMPLE);

	// enable vsync
	glfwSwapInterval(1);

}

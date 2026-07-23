#include "display.h"
#include <iostream>
#include <stdexcept>
#include <GLFW/glfw3.h>


Display::Display() {

	// initialize display
	if (!glfwInit()) {
		throw std::runtime_error("Failed to initialize GLFW");
	}
	
	// Pick the requested monitor when it exists, otherwise use GLFW's primary
	// monitor. The previous release unconditionally selected monitor 1, which
	// dereferenced past the array on the common single-monitor setup.
	GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);
	GLFWmonitor* monitor = (monitors && monitorIndex >= 0 && monitorIndex < monitorCount)
		? monitors[monitorIndex]
		: glfwGetPrimaryMonitor();
	const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : nullptr;

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
#ifdef __APPLE__
	// macOS only creates 3.2+ core contexts when forward compatibility is set.
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
	glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);
	
	const int initialWidth = mode ? mode->width : 1280;
	const int initialHeight = mode ? mode->height : 720;
	window = glfwCreateWindow(initialWidth, initialHeight, "AxiSim", nullptr, nullptr);
	if (!window) {
		glfwTerminate();
		throw std::runtime_error("Failed to create the AxiSim OpenGL window");
	}

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

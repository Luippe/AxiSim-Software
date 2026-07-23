#pragma once

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif

#include <glad/glad.h>

#include <GLFW/glfw3.h>

class Camera3D;
struct GLFWwindow;

// resize window callback function
static void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
	glViewport(0, 0, width, height);
}

// display class
class Display {
public:

	Display();

	int monitorIndex = 0;
	int monitorCount = 0;
	bool fullScreen = false;

	int width, height;

	int windowedX, windowedY;
	int windowedW, windowedH;

	GLFWwindow* window = nullptr;

};

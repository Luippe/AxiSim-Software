#pragma once
#include <glad/glad.h>

class Camera;
struct GLFWwindow;

// resize window callback function
static void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
	glViewport(0, 0, width, height);
}

// display class
class Display {
public:

	int monitorIndex = 2;	// on release, this would run on a nvidia gpu
	int monitorCount = 0;
	bool fullScreen = false;

	int width, height;
	float worldWidth, worldHeight, aspect;
	int windowedX, windowedY;
	int windowedW, windowedH;

	Camera& camera;
	GLFWwindow* window = nullptr;

	Display(Camera& camera);	// initialize shader



};

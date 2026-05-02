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

	GLFWwindow* window = nullptr;

	Display(Camera& camera);	// initialize shader

	int width, height;
	float worldWidth, worldHeight, aspect;
	Camera& camera;

};

#pragma once
#define GLM_ENABLE_EXPERIMENTAL
#include <vector>
#include <glm/vec3.hpp>
#include <glm/vec2.hpp>
#include <glm/gtc/quaternion.hpp>

// create spaced 1d vector
std::vector<double> linspace(double start, double end, std::size_t num);

// print 2d vector
void print2D(std::vector<std::vector<int>> vec);

// print 1d vector
void print1D(std::vector<double> vec);

// multiply a vector and a constant
std::vector<double> vec_const_mul(std::vector<double> v, double c);

// get the dot product between two vectors, clamped from -1 to 1
float dotClamp(glm::vec3 A, glm::vec3 B);

// given two normalized 3D vectors, get the quaternion from first vector to second vector
glm::quat getQuat(glm::vec3 A, glm::vec3 B);

// convert given mouse coordinates to device coordinates
glm::vec2 getNormalizedDeviceCoords(float xpos, float ypos, int width, int height);


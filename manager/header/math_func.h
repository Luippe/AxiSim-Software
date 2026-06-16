#pragma once
#include <vector>
#include <string>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/quaternion.hpp>
#include <glm/fwd.hpp>

#include "boundary_struct.h"

// create spaced 1d vector
std::vector<double> linspace(double start, double end, std::size_t N, double bias);

// multiply a vector and a constant
std::vector<double> vec_const_mul(std::vector<double> v, double c);

// get the dot product between two vectors, clamped from -1 to 1
float dotClamp(glm::vec3 A, glm::vec3 B);

// given two normalized 3D vectors, get the quaternion from first vector to second vector
glm::quat getQuat(glm::vec3 A, glm::vec3 B);

// convert given mouse coordinates to device coordinates
glm::vec2 getNormalizedDeviceCoords(float xpos, float ypos, int width, int height);

// scale mat4 by some value
glm::mat4 scaleMat4(const glm::mat4& mat, double scale);

// converts a string to lower case
std::string toLower(std::string str);

// mesh
double distance(Vec2 a, Vec2 b);

double pathLength(const std::vector<Vec2>& points);

Vec2 interpolatePath(
	const std::vector<Vec2>& points,
	double t
);

double biasedT(double s, double bias);

bool edgeInRange(const BoundaryEdge& e, std::size_t n);


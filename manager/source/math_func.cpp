#include "math_func.h"

#include <algorithm>
#include <cctype>
#include <glm/vec3.hpp>
#include <glm/vec2.hpp>
#include <iostream>

std::vector<double> linspace(double start, double end, std::size_t num) {

	// reserve space
	std::vector<double> x;
	x.reserve(num);

	if (num == 0) {
		return x;
	}

	if (num == 1) {
		x.push_back(start);
	}

	double step = (end - start) / static_cast<double>(num - 1);

	for (std::size_t i = 0; i < num; ++i) {
		x.push_back(start + step * static_cast<double>(i));
	}

	return x;
}


std::vector<double> vec_const_mul(std::vector<double> v, double c) {
	std::vector<double> res(v.size());
	for (int i = 0; i < v.size(); ++i) {
		res[i] = c * v[i];
	}
	return res;
}

float dotClamp(glm::vec3 A, glm::vec3 B) {
	return glm::clamp(glm::dot(A, B), -1.0f, 1.0f);
}

glm::quat getQuat(glm::vec3 A, glm::vec3 B) {
	float dotAB = dotClamp(A, B);

	glm::vec3 rotAxis = glm::cross(A, B);
	float angle = std::acos(dotAB);
	return glm::angleAxis(angle, glm::normalize(rotAxis));
}

glm::vec2 getNormalizedDeviceCoords(float xpos, float ypos, int width, int height) {
	float x = ((2.0f * xpos / (float)width) - 1.0f);
	float y = (1.0f - (2.0f * ypos) / (float)height);
	return glm::vec2(x, y);
}

std::string toLower(std::string str) {
	for (char& c : str) {
		c = (char)std::tolower((unsigned char)c);
	}
	return str;
}
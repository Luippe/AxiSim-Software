#include "math_func.h"

#include <algorithm>
#include <cctype>
#include <glm/vec3.hpp>
#include <glm/vec2.hpp>
#include <iostream>

std::vector<double> linspace(double start, double end, std::size_t N, double bias) {

	// reserve space
	std::vector<double> values;
	values.reserve(N);

	if (N <= 0) {
		return values;
	}

	if (N == 1) {
		values.push_back(start);
		return values;
	}

	for (int i = 0; i < N; i++) {
		double t = (double)(i) / (double)(N - 1);

		double biasedT = std::pow(t, bias);

		double x = start + biasedT * (end - start);

		values.push_back(x);
	}

	return values;
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

glm::mat4 scaleMat4(const glm::mat4& mat, double scale) {
	return glm::scale(mat, glm::vec3(scale));
}

std::string toLower(std::string str) {
	for (char& c : str) {
		c = (char)std::tolower((unsigned char)c);
	}
	return str;
}
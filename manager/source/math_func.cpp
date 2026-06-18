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

// mesh
double distance(Vec2 a, Vec2 b) {
	double dz = b.z - a.z;
	double dr = b.r - a.r;

	return std::sqrt(dz * dz + dr * dr);
}

double pathLength(const std::vector<Vec2>& points) {
	double length = 0.0;

	for (int i = 0; i < (int)(points.size()) - 1; i++) {
		length += distance(points[i], points[i + 1]);
	}

	return length;
}

Vec2 interpolatePath(
	const std::vector<Vec2>& points,
	double t
) {
	if (points.empty()) {
		return {};
	}

	if (points.size() == 1) {
		return points[0];
	}

	double totalLength = pathLength(points);

	if (totalLength <= 1e-30) {
		return points.front();
	}

	double target = t * totalLength;
	double accumulated = 0.0;

	for (int i = 0; i < (int)(points.size()) - 1; i++) {
		Vec2 a = points[i];
		Vec2 b = points[i + 1];

		double len = distance(a, b);

		if (accumulated + len >= target) {
			double localT = (target - accumulated) / len;

			Vec2 p{};
			p.z = a.z + localT * (b.z - a.z);
			p.r = a.r + localT * (b.r - a.r);

			return p;
		}

		accumulated += len;
	}

	return points.back();
}

double biasedT(double s, double bias) {
	if (bias <= 0.0) {
		return s;
	}

	if (std::abs(bias - 1.0) < 1e-12) {
		return s;
	}

	return std::pow(s, bias);
}

bool edgeInRange(const BoundaryEdge& e, std::size_t n) {
	return e.v0 >= 0 && e.v1 >= 0 &&
		e.v0 < (int)n && e.v1 < (int)n;
}

Vec2 closestPointOnSegment(Vec2 p, Vec2 a, Vec2 b) {
	double abZ = b.z - a.z;
	double abR = b.r - a.r;

	double apZ = p.z - a.z;
	double apR = p.r - a.r;

	double ab2 = abZ * abZ + abR * abR;
	if (ab2 <= 1e-30) return a;

	double t = (apZ * abZ + apR * abR) / ab2;
	t = std::clamp(t, 0.0, 1.0);

	return Vec2{
		a.z + t * abZ,
		a.r + t * abR
	};
}
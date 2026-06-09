#include "mouse_picker.h"

#include "project.h"


#include "printer.h"
#include "results.h"
#include "camera.h"
#include "scene_view.h"
#include "console.h"
#include "setting.cuh"
#include "solver_struct.h"
#include "math_func.h"

MousePicker::MousePicker(Project& project, SceneView& scene) :
	project(project),
	scene(scene),
	bound(scene.bound),
	camera(scene.camera),
	results(project.results),
	g(project.config.g){
}

void MousePicker::pick() {

	if (dataPick()) return;

	axisBBPick();

}

void MousePicker::update() {

	currentRay = calculateMouseRay();

}

void MousePicker::setDimensions(int w, int h, ImVec2 pos) {

	width = w;
	height = h;
	rectPos = pos;

}

glm::vec3 MousePicker::getCurrentRay() {
	return currentRay;
};

glm::vec3 MousePicker::calculateMouseRay() {
	ImVec2 mouse = ImGui::GetMousePos();   // absolute mouse position
	glm::vec2 mousePos(mouse.x - rectPos.x, mouse.y - rectPos.y); // relative mouse position to the top left corner of the window
	glm::vec2 normalizedCoords = getNormalizedDeviceCoords(mousePos.x, mousePos.y, width, height);
	glm::vec4 clipCoords(normalizedCoords, -1.0f, 1.0f);
	glm::vec4 eyeCoords = toEyeCoords(clipCoords);
	glm::vec3 worldRay = toWorldCoords(eyeCoords);
	return worldRay;
}

glm::vec4 MousePicker::toEyeCoords(glm::vec4 clipCoords) {
	glm::mat4 invertedProjection = glm::inverse(camera.projection);
	glm::vec4 eyeCoords = invertedProjection * clipCoords;
	return glm::vec4(eyeCoords.x, eyeCoords.y, -1.0f, 0.0f);
}

glm::vec3 MousePicker::toWorldCoords(glm::vec4 eyeCoords) {
	glm::mat4 invertedView = glm::inverse(camera.view);
	glm::vec4 rayWorld = invertedView * eyeCoords;
	return glm::normalize(glm::vec3(rayWorld.x, rayWorld.y, rayWorld.z));
}

bool MousePicker::BBIntersect(BoundingBox& box, float& t) {

	// AABB method (slab method)
	float tMin = 0.0f;
	float tMax = FLT_MAX;

	for (int i = 0; i < 3; i++) {

		float inversedDir = 1.0f / (currentRay[i]);
		float t0 = (box.min[i] - camera.position[i]) * inversedDir;
		float t1 = (box.max[i] - camera.position[i]) * inversedDir;
		if (t0 > t1) std::swap(t0, t1);

		tMin = std::max(tMin, t0);
		tMax = std::min(tMax, t1);

		if (tMax < tMin) {
			return false;
		}
	}
	t = tMin;
	return true;
}

bool MousePicker::capIntersect(const glm::vec3& capCenter, const glm::vec3& capNormal, float radius, float& t) {
	float t0 = glm::dot(capCenter - camera.position, capNormal) / glm::dot(currentRay, capNormal);
	if (t0 < 0.0f) return false;

	glm::vec3 P = camera.position + t0 * currentRay - capCenter;
	// radial distance from cap center, ignoring axis direction
	glm::vec3 radial = P - glm::dot(P, capNormal) * capNormal;
	float rad = glm::dot(radial, radial);

	if (rad <= results.currentOuter * results.currentOuter && rad >= results.currentInner * results.currentInner) {
		if (t > t0) {
			t = t0;
		}
	}
	else {
		return false;
	}

	return true;
}

bool MousePicker::ringIntersect(float radius, float& t) {

	bool collided = false;
	glm::vec3 cylinderDirection = glm::vec3(1.0f, 0.0f, 0.0f);
	glm::vec3 position = camera.position;
	glm::vec3 OC = position;
	glm::vec3 Dperp = currentRay - glm::dot(currentRay, cylinderDirection) * cylinderDirection;
	glm::vec3 OCperp = OC - glm::dot(OC, cylinderDirection) * cylinderDirection;

	float a = glm::dot(Dperp, Dperp);
	float b = 2.0f * glm::dot(Dperp, OCperp);
	float c = glm::dot(OCperp, OCperp) - radius * radius;

	if (std::abs(a) > 1e-6) {
		float disc = b * b - 4.0f * a * c;

		if (disc >= 0.0f) {

			float sqrtDisc = sqrt(disc);
			float t1 = (-b - sqrtDisc) / (2.0f * a);
			float t2 = (-b + sqrtDisc) / (2.0f * a);

			if (t1 < 0.0f && t2 < 0.0f) {
				return false;
			}

			// check if t0 and t1 is within the cylinder length
			if (t1 > 0.0f) {
				glm::vec3 P1 = position + t1 * currentRay;
				float y1 = glm::dot(P1, cylinderDirection);
				if (y1 >= results.currentFront && y1 <= results.currentBack && t1 < t) {
					t = t1;
					collided = true;
				}
			}

			if (t2 > 0.0f) {
				glm::vec3 P2 = position + t2 * currentRay;
				float y2 = glm::dot(P2, cylinderDirection);
				if (y2 >= results.currentFront && y2 <= results.currentBack && t2 < t) {
					t = t2;
					collided = true;
				}
			}
		}
	}

	if (collided) return true;
	else return false;
}

bool MousePicker::dataPick() {

	if (!(project.currentTab == ViewTab::TAB_RESULTS)) return false;

	float t = FLT_MAX;
	glm::vec3 cylinderDirection = glm::vec3(1.0f, 0.0f, 0.0f);
	// get the location of intersect
	capIntersect({ results.currentFront, 0.0f, 0.0f },  -cylinderDirection, results.currentOuter, t);
	capIntersect({ results.currentBack, 0.0f, 0.0f }, cylinderDirection, results.currentOuter, t);
	ringIntersect(results.currentOuter, t);

	// get value at given location and print to console
	if (t != FLT_MAX && results.isReady) {
		glm::vec3 P = camera.position + t * currentRay;
		float val = results.currentField->getData(P);
		std::string s = " (" + 
			std::to_string(P.x) + ", " +
			std::to_string(P.y) + ", " +
			std::to_string(P.z) + ")";
		console->addLine("Value: " + std::to_string(val) + " at " + s);
		printf("Data value: %f at %f, %f, %f\n", val, P.x, P.y, P.z);
		return true;
	}

	return false;
}

void MousePicker::axisBBPick() {

	float tClosest = FLT_MAX;
	int pickedID = -1;

	for (int i = 0; i < bound.axisBB.size(); i++) {
		float t;
		if (BBIntersect(bound.axisBB[i], t)) {
			if (t < tClosest) {
				tClosest = t;
				pickedID = bound.axisBB[i].ID;
			}
		}
	}

	if (pickedID != -1) {
		glm::vec3 axis = getPickedAxis(pickedID);
		camera.updateTargetRotation(axis);
	}
}

glm::vec3 MousePicker::getPickedAxis(int& pickedID) {
	glm::vec3 axis = glm::vec3(0.0f);
	axis[pickedID % 3] = pickedID < 3 ? -1 : 1;
	return axis;
}
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
	camera(scene.camera),
	results(project.results),
	g(project.config.g){
}

void MousePicker::pick() {

	dataPick();

}

void MousePicker::update() {

	ImVec2 mouse = ImGui::GetMousePos();   // absolute mouse position
	glm::vec2 mousePos(mouse.x - rectPos.x, mouse.y - rectPos.y); // relative mouse position to the top left corner of the window
	glm::vec2 normalizedCoords = getNormalizedDeviceCoords(mousePos.x, mousePos.y, width, height);

	const glm::mat4 invertedView = glm::inverse(camera.view);

	if (camera.projectionType == ProjectionType::Orthographic) {

		// parallel rays: the direction is the same everywhere and the pixel
		// slides the origin across the view plane
		const float halfHeight = camera.viewHalfHeight();
		const float halfWidth = halfHeight * ((float)width / (float)std::max(height, 1));

		glm::vec4 eyeCoords(normalizedCoords.x * halfWidth, normalizedCoords.y * halfHeight, 0.0f, 1.0f);

		rayOrigin = glm::vec3(invertedView * eyeCoords);
		currentRay = camera.getFront();
	}
	else {

		glm::vec4 clipCoords(normalizedCoords, -1.0f, 1.0f);
		glm::vec4 eyeCoords = glm::inverse(camera.projection) * clipCoords;

		// force a pure direction down the view axis; only valid because a
		// perspective frustum has a single shared apex
		eyeCoords = glm::vec4(eyeCoords.x, eyeCoords.y, -1.0f, 0.0f);

		rayOrigin = camera.position;
		currentRay = glm::normalize(glm::vec3(invertedView * eyeCoords));
	}
}

void MousePicker::setDimensions(int w, int h, ImVec2 pos) {

	width = w;
	height = h;
	rectPos = pos;

}

glm::vec3 MousePicker::getCurrentRay() {
	return currentRay;
};

bool MousePicker::capIntersect(const glm::vec3& capCenter, const glm::vec3& capNormal, float innerRadius, float outerRadius, float& t) {
	float t0 = glm::dot(capCenter - rayOrigin, capNormal) / glm::dot(currentRay, capNormal);
	if (t0 < 0.0f) return false;

	glm::vec3 P = rayOrigin + t0 * currentRay - capCenter;
	// radial distance from cap center, ignoring axis direction
	glm::vec3 radial = P - glm::dot(P, capNormal) * capNormal;
	float rad = glm::dot(radial, radial);

	if (rad <= outerRadius * outerRadius && rad >= innerRadius * innerRadius) {
		if (t > t0) {
			t = t0;
		}
	}
	else {
		return false;
	}

	return true;
}

bool MousePicker::ringIntersect(float radius, float front, float back, float& t) {

	bool collided = false;
	glm::vec3 cylinderDirection = glm::vec3(1.0f, 0.0f, 0.0f);
	glm::vec3 position = rayOrigin;
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
				if (y1 >= front && y1 <= back && t1 < t) {
					t = t1;
					collided = true;
				}
			}

			if (t2 > 0.0f) {
				glm::vec3 P2 = position + t2 * currentRay;
				float y2 = glm::dot(P2, cylinderDirection);
				if (y2 >= front && y2 <= back && t2 < t) {
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
	if (!results.isReady || !results.currentField) return false;

	float t = FLT_MAX;
	glm::vec3 cylinderDirection = glm::vec3(1.0f, 0.0f, 0.0f);
	const float front = results.g.zFace.empty() ? 0.0f : static_cast<float>(results.g.zFace.front());
	const float back = results.g.zFace.empty() ? static_cast<float>(results.g.L) : static_cast<float>(results.g.zFace.back());
	const float inner = results.g.rFace.empty() ? 0.0f : static_cast<float>(results.g.rFace.front());
	const float outer = results.g.rFace.empty() ? static_cast<float>(results.g.R) : static_cast<float>(results.g.rFace.back());

	// get the location of intersect
	capIntersect({ front, 0.0f, 0.0f },  -cylinderDirection, inner, outer, t);
	capIntersect({ back, 0.0f, 0.0f }, cylinderDirection, inner, outer, t);
	ringIntersect(outer, front, back, t);

	// get value at given location and print to console
	if (t != FLT_MAX) {
		glm::vec3 P = rayOrigin + t * currentRay;
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


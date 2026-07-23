#include "axis_gizmo.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace {

	constexpr float PI = 3.14159265359f;

	// local +X maps onto the axis each arm points down; the other two columns
	// only have to complete a right-handed frame, since the arm is a solid of
	// revolution about its own axis
	glm::mat3 axisBasis(float degrees, const glm::vec3& about) {
		return glm::mat3(glm::rotate(glm::mat4(1.0f), glm::radians(degrees), about));
	}

}


AxisGizmo::AxisGizmo() :
	shader("graphics/shaders/gizmo.vert", "graphics/shaders/gizmo.frag")
{
	generate();
}


// ======================================================================
// -------------------------- MESH BUILDING -----------------------------
// ======================================================================
void AxisGizmo::push(const glm::vec3& position, const glm::vec3& normal, const glm::vec3& color, float id) {
	vertices.push_back({ position, normal, color, id });
}


void AxisGizmo::addFrustum(const glm::mat3& basis, float x0, float x1, float r0, float r1, const glm::vec3& color, float id) {

	const int n = std::max(3, segments);

	const float dx = x1 - x0;
	const float dr = r1 - r0;

	for (int k = 0; k < n; k++) {

		const float t0 = 2.0f * PI * (float)k / (float)n;
		const float t1 = 2.0f * PI * (float)(k + 1) / (float)n;

		// radial directions at the two ends of this slice
		const glm::vec3 d0(0.0f, std::cos(t0), std::sin(t0));
		const glm::vec3 d1(0.0f, std::cos(t1), std::sin(t1));

		// the slant tangent in the (axial, radial) plane is (dx, dr), so the
		// outward normal in that plane is (-dr, dx) -- this gives a pure radial
		// normal for a cylinder and a tilted one for a cone head
		const glm::vec3 n0 = basis * glm::normalize(glm::vec3(-dr, 0.0f, 0.0f) + dx * d0);
		const glm::vec3 n1 = basis * glm::normalize(glm::vec3(-dr, 0.0f, 0.0f) + dx * d1);

		const glm::vec3 a0 = basis * (glm::vec3(x0, 0.0f, 0.0f) + r0 * d0);
		const glm::vec3 a1 = basis * (glm::vec3(x0, 0.0f, 0.0f) + r0 * d1);
		const glm::vec3 b0 = basis * (glm::vec3(x1, 0.0f, 0.0f) + r1 * d0);
		const glm::vec3 b1 = basis * (glm::vec3(x1, 0.0f, 0.0f) + r1 * d1);

		push(a0, n0, color, id);
		push(a1, n1, color, id);
		push(b1, n1, color, id);

		push(a0, n0, color, id);
		push(b1, n1, color, id);
		push(b0, n0, color, id);
	}
}


void AxisGizmo::addDisk(const glm::mat3& basis, float x, float radius, const glm::vec3& color, float id) {

	const int n = std::max(3, segments);

	const glm::vec3 normal = basis * glm::vec3(-1.0f, 0.0f, 0.0f);
	const glm::vec3 center = basis * glm::vec3(x, 0.0f, 0.0f);

	for (int k = 0; k < n; k++) {

		const float t0 = 2.0f * PI * (float)k / (float)n;
		const float t1 = 2.0f * PI * (float)(k + 1) / (float)n;

		const glm::vec3 p0 = basis * glm::vec3(x, radius * std::cos(t0), radius * std::sin(t0));
		const glm::vec3 p1 = basis * glm::vec3(x, radius * std::cos(t1), radius * std::sin(t1));

		// wound so the front face looks back down the arm
		push(center, normal, color, id);
		push(p1, normal, color, id);
		push(p0, normal, color, id);
	}
}


void AxisGizmo::addArm(const glm::mat3& basis, const glm::vec3& color, float id) {

	const float headStart = std::clamp(1.0f - headLength, 0.05f, 0.95f);

	// shaft from the hub out to where the head begins
	addFrustum(basis, 0.0f, headStart, shaftRadius, shaftRadius, color, id);

	// flared base of the head, so the step from shaft to head is not hollow
	addDisk(basis, headStart, headRadius, color, id);

	// conical head closing at the arm tip
	addFrustum(basis, headStart, 1.0f, headRadius, 0.0f, color, id);
}


void AxisGizmo::addHub() {

	const int slices = std::max(4, segments);
	const int rings = std::max(3, hubRings);

	auto unit = [](float phi, float theta) {
		return glm::vec3(
			std::sin(phi) * std::cos(theta),
			std::cos(phi),
			std::sin(phi) * std::sin(theta)
		);
		};

	for (int i = 0; i < rings; i++) {

		const float phi0 = PI * (float)i / (float)rings;
		const float phi1 = PI * (float)(i + 1) / (float)rings;

		for (int j = 0; j < slices; j++) {

			const float th0 = 2.0f * PI * (float)j / (float)slices;
			const float th1 = 2.0f * PI * (float)(j + 1) / (float)slices;

			const glm::vec3 n00 = unit(phi0, th0);
			const glm::vec3 n01 = unit(phi0, th1);
			const glm::vec3 n10 = unit(phi1, th0);
			const glm::vec3 n11 = unit(phi1, th1);

			// the hub is never a click target, so it takes an id no arm uses
			push(hubRadius * n00, n00, colorHub, (float)ArmNone);
			push(hubRadius * n10, n10, colorHub, (float)ArmNone);
			push(hubRadius * n11, n11, colorHub, (float)ArmNone);

			push(hubRadius * n00, n00, colorHub, (float)ArmNone);
			push(hubRadius * n11, n11, colorHub, (float)ArmNone);
			push(hubRadius * n01, n01, colorHub, (float)ArmNone);
		}
	}
}


void AxisGizmo::generate() {

	vertices.clear();

	const glm::vec3 colors[3] = { colorX, colorY, colorZ };

	const glm::mat3 positive[3] = {
		glm::mat3(1.0f),
		axisBasis(90.0f, glm::vec3(0.0f, 0.0f, 1.0f)),
		axisBasis(-90.0f, glm::vec3(0.0f, 1.0f, 0.0f))
	};

	for (int i = 0; i < 3; i++) {
		addArm(positive[i], colors[i], (float)(ArmPosX + i));
	}

	// the hub belongs to the always-drawn block, so it sits ahead of the split
	addHub();

	// ---- everything past here is revealed only on hover ----
	negativeStart = (int)vertices.size();

	const glm::mat3 negative[3] = {
		axisBasis(180.0f, glm::vec3(0.0f, 0.0f, 1.0f)),
		axisBasis(-90.0f, glm::vec3(0.0f, 0.0f, 1.0f)),
		axisBasis(90.0f, glm::vec3(0.0f, 1.0f, 0.0f))
	};

	for (int i = 0; i < 3; i++) {
		// scaling the basis shrinks the whole arrow uniformly; the shader
		// re-normalizes, so the scaled normals still come out right
		addArm(
			negative[i] * negativeArmLength,
			colors[i] * negativeDim,
			(float)(ArmNegX + i)
		);
	}

	upload();
}


int AxisGizmo::drawCount() const {
	return showNegativeArms ? (int)vertices.size() : negativeStart;
}


void AxisGizmo::upload() {

	if (vertices.empty()) return;

	buffer.createBuffer(vertices.size() * sizeof(VertexShaded), vertices.data());
	buffer.bind();
	buffer.enableAttribute(0, 3, GL_FLOAT, sizeof(VertexShaded), (void*)offsetof(VertexShaded, position));
	buffer.enableAttribute(1, 3, GL_FLOAT, sizeof(VertexShaded), (void*)offsetof(VertexShaded, normal));
	buffer.enableAttribute(2, 3, GL_FLOAT, sizeof(VertexShaded), (void*)offsetof(VertexShaded, color));
	buffer.enableAttribute(3, 1, GL_FLOAT, sizeof(VertexShaded), (void*)offsetof(VertexShaded, id));
	buffer.unbind();
}


// ======================================================================
// ----------------------------- DRAWING --------------------------------
// ======================================================================
float AxisGizmo::overlayHalfExtent() const {
	return 1.0f + headRadius + 0.12f;
}


bool AxisGizmo::overlayRect(int viewportWidth, int viewportHeight, int& x, int& y, int& size) const {

	size = std::min(overlaySizePx, std::min(viewportWidth, viewportHeight));
	if (size <= 0) return false;

	x = std::max(0, viewportWidth - size - overlayMarginPx);
	y = std::min(overlayMarginPx, std::max(0, viewportHeight - size));

	return true;
}


void AxisGizmo::drawAtOrigin(const glm::mat4& model, const glm::mat4& view, const glm::mat4& projection) {

	if (vertices.empty()) return;

	shader.use();
	shader.loadTransformationMatrix(model, view, projection);
	shader.SetFloat("ambient", ambient);
	shader.SetFloat("uHighlight", (float)highlightArm);

	buffer.bind();
	glDrawArrays(GL_TRIANGLES, 0, (GLsizei)drawCount());
	buffer.unbind();
}


void AxisGizmo::drawOverlay(const glm::mat4& sceneView, int viewportWidth, int viewportHeight) {

	if (vertices.empty()) return;

	int x = 0, y = 0, size = 0;
	if (!overlayRect(viewportWidth, viewportHeight, x, y, size)) return;

	GLint prevViewport[4];
	glGetIntegerv(GL_VIEWPORT, prevViewport);

	glViewport(x, y, size, size);

	// the triad owns its corner outright: clear only that square's depth so it
	// never gets buried inside the scene geometry
	const GLboolean scissorWasOn = glIsEnabled(GL_SCISSOR_TEST);
	GLint prevScissor[4];
	glGetIntegerv(GL_SCISSOR_BOX, prevScissor);

	glEnable(GL_SCISSOR_TEST);
	glScissor(x, y, size, size);
	glClear(GL_DEPTH_BUFFER_BIT);

	if (scissorWasOn) {
		glScissor(prevScissor[0], prevScissor[1], prevScissor[2], prevScissor[3]);
	}
	else {
		glDisable(GL_SCISSOR_TEST);
	}

	// orientation only -- dropping the translation column pins the triad to the
	// corner while it still spins with the camera
	const glm::mat4 view = glm::mat4(glm::mat3(sceneView));

	// orthographic, so the arms keep equal on-screen length in every direction
	const float half = overlayHalfExtent();
	const glm::mat4 projection = glm::ortho(-half, half, -half, half, -10.0f, 10.0f);

	drawAtOrigin(glm::mat4(1.0f), view, projection);

	glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
}


// ======================================================================
// ----------------------------- PICKING --------------------------------
// ======================================================================
glm::vec3 AxisGizmo::armDirection(int arm) const {

	glm::vec3 dir(0.0f);

	if (arm < ArmPosX || arm >= ArmCount) return dir;

	dir[arm % 3] = (arm < ArmNegX) ? 1.0f : -1.0f;
	return dir;
}


float AxisGizmo::armLength(int arm) const {
	return (arm < ArmNegX) ? 1.0f : negativeArmLength;
}


bool AxisGizmo::overlayContains(const glm::vec2& localMouse, int viewportWidth, int viewportHeight) const {

	int x = 0, y = 0, size = 0;
	if (!overlayRect(viewportWidth, viewportHeight, x, y, size)) return false;

	// the image is drawn flipped, so a mouse measured from the top-left lands in
	// the framebuffer's bottom-left coordinates once y is mirrored
	const float fx = localMouse.x;
	const float fy = (float)viewportHeight - localMouse.y;

	return fx >= (float)x && fx <= (float)(x + size)
		&& fy >= (float)y && fy <= (float)(y + size);
}


int AxisGizmo::pickOverlay(
	const glm::mat4& sceneView,
	const glm::vec2& localMouse,
	int viewportWidth,
	int viewportHeight
) const {

	if (vertices.empty()) return ArmNone;

	int x = 0, y = 0, size = 0;
	if (!overlayRect(viewportWidth, viewportHeight, x, y, size)) return ArmNone;

	// the image is drawn flipped, so a mouse measured from the top-left lands in
	// the framebuffer's bottom-left coordinates once y is mirrored
	const float fx = localMouse.x;
	const float fy = (float)viewportHeight - localMouse.y;

	const float u = (fx - (float)x) / (float)size;
	const float v = (fy - (float)y) / (float)size;

	if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) return ArmNone;

	// into the overlay's orthographic box
	const float half = overlayHalfExtent();
	const glm::vec2 mouse(
		(2.0f * u - 1.0f) * half,
		(2.0f * v - 1.0f) * half
	);

	// the overlay view is rotation-only and the projection orthographic, so an
	// arm is just a 2D segment from the hub to its projected tip
	const glm::mat3 rotation = glm::mat3(sceneView);

	// clicks inside the hub are ambiguous -- every arm passes through it -- so
	// they belong to no arm at all
	const float hubGuard = hubRadius * 1.25f;
	if (glm::length(mouse) < hubGuard) return ArmNone;

	const float pickRadius = headRadius + 0.045f;

	int best = ArmNone;
	float bestDepth = -FLT_MAX;

	// an arm nobody can see is not a click target
	const int lastArm = showNegativeArms ? ArmCount : ArmNegX;

	for (int arm = ArmPosX; arm < lastArm; arm++) {

		const glm::vec3 tip = rotation * (armLength(arm) * armDirection(arm));
		const glm::vec2 tip2(tip.x, tip.y);

		const float len2 = glm::dot(tip2, tip2);

		// an arm pointing straight at (or away from) the eye collapses into the
		// hub, where nothing is pickable
		if (len2 < 1.0e-8f) continue;

		float t = glm::dot(mouse, tip2) / len2;
		t = std::clamp(t, 0.0f, 1.0f);

		const glm::vec2 closest = t * tip2;

		if (glm::length(mouse - closest) > pickRadius) continue;
		if (glm::length(closest) < hubGuard) continue;

		// front-most wins: with an orthographic projection the eye-space depth
		// at the closest point is just the interpolated tip depth
		const float depth = t * tip.z;

		if (depth > bestDepth) {
			bestDepth = depth;
			best = arm;
		}
	}

	return best;
}


glm::vec3 AxisGizmo::snapAxis(int arm, const glm::mat4& sceneView) const {

	const glm::vec3 dir = armDirection(arm);
	if (dir == glm::vec3(0.0f)) return glm::vec3(0.0f, 0.0f, 1.0f);

	// third row of the view rotation is the world direction from the look-at
	// target back toward the eye
	const glm::vec3 toEye(sceneView[0][2], sceneView[1][2], sceneView[2][2]);

	// already looking down this arm: flip, so three visible arms reach all six
	// standard views
	if (glm::dot(glm::normalize(toEye), dir) > 0.999f) {
		return -dir;
	}

	return dir;
}

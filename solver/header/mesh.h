#pragma once
#include "setting.cuh"
#include "buffer_manager.h"
#include "graphics_struct.h"

class Shader;
class Console;
class Config;

class Mesh {
public:

	glm::vec3 color = { 0.0f, 0.0f, 0.0f };
	std::vector<Vertex> vertices;
	std::vector<unsigned int> indices;

	Mesh(Shader& shader, Config& config);

	GridConfig& g;

	// render the mesh, wireframe, and outline
	void render();

	int nseg = 64;	// number of vertices on the circle
	float ntheta; // angle of each triangle in circle
	bool showMesh = true;
	bool showFill = true;
	bool isReady = false;

	glm::vec3 cylinderDirection = { 1.0f, 0.0f, 0.0f };

	void generate();

	void updateAfterLoadingFile();

	Console* console = nullptr;

private:

	Shader& shader;
	void clearAll();
	void createVertices();
	void drawMesh();

};

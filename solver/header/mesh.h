#pragma once
#include "setting.cuh"
#include "buffer_manager.h"
#include "graphics_struct.h"

class Shader;
class Console;
class Config;

class Mesh {
public:

	int colFront;
	int colBack;
	int rowTop;
	int rowBot;
	glm::vec3 color = { 0.0f, 0.0f, 0.0f };
	std::vector<Vertex> vertices;
	std::vector<Vertex> verticesCV;
	std::vector<unsigned int> indices;
	std::vector<unsigned int> indicesCV;
	std::vector<ControlVolume> cv;

	Mesh(Shader& shader, Config& config);

	GridConfig& g;

	// render the mesh, wireframe, and outline
	void render();

	int nseg = 16;	// number of vertices on the circle
	float ntheta; // angle of each triangle in circle
	bool showMesh = true;
	bool showFill = true;
	bool isReady = false;

	glm::vec3 cylinderDirection = { 1.0f, 0.0f, 0.0f };

	float currentOuter = g.R;
	float currentFront = 0.0;
	float currentBack = g.L;
	float currentInner = 0.0;

	void generate();

	void updateAfterLoadingFile();

	Console* console = nullptr;

	VertexBuffer cvBuffer;
	ElementBuffer cvElementBuffer;

private:

	Shader& shader;

	void createBuffer();
	void clearAll();
	void createVertices();
	void createCVIndex();
	void drawMesh();

};

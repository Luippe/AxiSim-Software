#pragma once
#include <glm/fwd.hpp>
class Camera;

// shader class to make it easier to manage shaders
class Shader {
public:
	unsigned int ID;
	unsigned int VBO;

	Shader(const char* vertexPath, const char* fragmentPath);

	//void init_shader(const char* vertexPath, const char* fragmentPath);
	void use();
	void SetFloat(const char* name, float value);
	void SetInt(const char* name, int value);
	void SetVec3(const char* name, float x, float y, float z);
	void SetVec3(const char* name, glm::vec3& value);
	void SetMat4(const char* name, const glm::mat4& matrix);
	void SetBool(const char* name, bool value);
	void SetColor(const char* name, const glm::vec3& vec);
	void loadTransformationMatrix(Camera& camera);

};

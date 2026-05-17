#include "pch.h"
#include "shader.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <glm/gtc/type_ptr.hpp>


GLint Shader::getUniformLocation(const char* name) {
	std::string key(name);

	auto it = uniformLocationCache.find(key);
	if (it != uniformLocationCache.end()) {
		return it->second;
	}

	GLint location = glGetUniformLocation(ID, name);
	uniformLocationCache[key] = location;

	return location;

}

void Shader::SetFloat(const char *name, float value) {
	glUniform1f(getUniformLocation(name), value);
}

void Shader::SetInt(const char* name, int value) {
	glUniform1i(getUniformLocation(name), value);
}

void Shader::SetVec3(const char *name, float x, float y, float z) {
	glUniform3f(getUniformLocation(name), x, y, z);
}

void Shader::SetVec3(const char* name, glm::vec3 &value) {
	glUniform3fv(getUniformLocation(name), 1, &value[0]);
}

void Shader::SetMat4(const char *name, const glm::mat4 &matrix) {
	glUniformMatrix4fv(getUniformLocation(name), 1, GL_FALSE, glm::value_ptr(matrix));
}
void Shader::SetBool(const char* name, bool value) {
	glUniform1i(getUniformLocation(name), (int)value);
}

void Shader::SetColor(const char* name, const glm::vec3& vec) {
	glUniform3f(getUniformLocation(name), vec.x, vec.y, vec.z);
}

void Shader::loadTransformationMatrix(const glm::mat4& model, const glm::mat4& view, const glm::mat4& projection) {
	use();
	SetMat4("model", model);
	SetMat4("view", view);
	SetMat4("projection", projection);
}

Shader::Shader(const char* vertexPath, const char* fragmentPath) {

	// retrieve the vertex/fragment source code from filePath
	std::string vertexCode;
	std::string fragmentCode;
	std::ifstream vShaderFile;
	std::ifstream fShaderFile;

	// test exceptions
	vShaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
	fShaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);

	try
	{
		// open files
		vShaderFile.open(vertexPath);
		fShaderFile.open(fragmentPath);
		std::stringstream vShaderStream, fShaderStream;

		// read file's buffer contents into streams
		vShaderStream << vShaderFile.rdbuf();
		fShaderStream << fShaderFile.rdbuf();

		// convert stream into string
		vertexCode = vShaderStream.str();
		fragmentCode = fShaderStream.str();

	}
	catch (std::ifstream::failure e)
	{
		std::cout << "ERROR::SHADER::FILE_NOT_SUCCESFULLY_READ" << std::endl;
	}

	const char* vShaderCode = vertexCode.c_str();
	const char* fShaderCode = fragmentCode.c_str();


	// compile shaders
	unsigned int vertex, fragment;
	int success;
	char infoLog[512];

	// vertex shader
	vertex = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vertex, 1, &vShaderCode, NULL);
	glCompileShader(vertex);

	// print compile errors if any
	glGetShaderiv(vertex, GL_COMPILE_STATUS, &success);
	if (!success)
	{
		glGetShaderInfoLog(vertex, 512, NULL, infoLog);
		std::cout << "ERROR::SHADER::VERTEX::COMPILATION_FAILED\n" << infoLog << std::endl;
	}

	// fragment shader
	fragment = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fragment, 1, &fShaderCode, NULL);
	glCompileShader(fragment);

	// print compile errors if any
	glGetShaderiv(fragment, GL_COMPILE_STATUS, &success);
	if (!success)
	{
		glGetShaderInfoLog(fragment, 512, NULL, infoLog);
		std::cout << "ERROR::SHADER::VERTEX::COMPILATION_FAILED\n" << infoLog << std::endl;
	}

	// shader program
	ID = glCreateProgram();
	glAttachShader(ID, vertex);
	glAttachShader(ID, fragment);
	glLinkProgram(ID);

	// print linking errors if any
	glGetProgramiv(ID, GL_LINK_STATUS, &success);
	if (!success)
	{
		glGetProgramInfoLog(ID, 512, NULL, infoLog);
		std::cout << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n" << infoLog << std::endl;
	}

	// delete shaders as they are linked into our program
	glDeleteShader(vertex);
	glDeleteShader(fragment);

}

void Shader::use()
{
	glUseProgram(ID);
}

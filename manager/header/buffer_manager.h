#pragma once
#include <glad/glad.h>

class FrameBuffer {
public:
	FrameBuffer() {};
	void bind();
	void unbind();
	void createBuffer(int width, int height, int samples = 4);
	void resolve();
	unsigned int getTexture();
	int width, height;

private:
	void deleteBuffer();
	unsigned int FBO = 0;
	unsigned int resolveFBO = 0;
	unsigned int colorRBO = 0;
	unsigned int depthRBO = 0;
	unsigned int texture = 0;

};

class VertexBuffer {
public:
	VertexBuffer() {};
	void bind();
	void unbind();
	void createBuffer(GLsizeiptr size, const void* data);
	void enableAttribute(GLuint index, GLint size, GLenum type, GLsizei stride, const void* pointer);

private:
	void deleteBuffer();
	unsigned int VAO = 0;
	unsigned int VBO = 0;
};

class ElementBuffer {
public:
	ElementBuffer() {};
	void createBuffer(GLsizeiptr size, const void* data);

private:
	void deleteBuffer();
	unsigned int EBO = 0;
};


class TextureBuffer {
public:
	TextureBuffer() {};
	void bind();
	void unbind();
	void createBuffer(GLenum target, int nx, int ny, GLenum format, const void* data);
	void updateBuffer(GLenum target, int nx, int ny, GLenum format, const void* data);
	unsigned int getTextureID();
private:
	void deleteBuffer();
	unsigned int TBO = 0;
};
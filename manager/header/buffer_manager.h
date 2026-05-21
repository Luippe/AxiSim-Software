#pragma once
#include <glad/glad.h>
#include <vector>

class FrameBuffer {
public:
	FrameBuffer() {};
	void bind();
	void unbind();


	// read the framebuffer and return rgba pixel values
	std::vector<unsigned char> readPixelsRGBA();

	// create a frame buffer with MSAA
	void createBuffer(int width, int height, int samples = 4);

	// create a frame buffer with no MSAA and colorRBO and depthRBO, useful for 2D images
	void createSimpleBuffer(int width, int height, GLenum internalFormat, GLenum format, GLenum type);

	// resolve frame buffer before drawing
	void resolve();

	unsigned int getTextureID();
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
	void bindVBO();
	void unbind();
	void unbindVBO();
	void createBuffer(GLsizeiptr size, const void* data);
	void enableAttribute(GLuint index, GLint size, GLenum type, GLsizei stride, const void* pointer);
	void copyBuffer(GLuint srcBuffer, GLsizeiptr size);
	void bufferSubData(GLsizeiptr size, const void* data);
	GLuint getVBO();
	GLuint getVAO();

private:
	void deleteBuffer();
	unsigned int VAO = 0;
	unsigned int VBO = 0;
};

class ElementBuffer {
public:

	ElementBuffer() {};
	void createBuffer(GLsizeiptr size, const void* data);
	void copyBuffer(GLuint srcBuffer, GLsizeiptr size);
	GLuint getEBO();

private:

	void deleteBuffer();
	unsigned int EBO = 0;
};


class TextureBuffer {
public:
	TextureBuffer() {};
	void bind();
	void unbind();

	// create texture buffer using pixel data
	void createBuffer(GLenum target, int nx, int ny, GLenum format, GLenum type, const void* data);

	// create texture buffer using PNG images
	void createBuffer(const char* path);

	// update texture buffer, mostly used to update it with new data
	void updateBuffer(int nx, int ny, GLenum format, GLenum type, const void* data);

	// getter for texture id
	unsigned int getTextureID();

private:

	void deleteBuffer();
	unsigned int TBO = 0;
};
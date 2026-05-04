#include "buffer_manager.h"
#include <cstdio>
// --------------------------Frame Buffer---------------------------

void FrameBuffer::createBuffer(int width, int height, int samples) {

	this->width = width;
	this->height = height;

	if (FBO) {
		deleteBuffer();
	}

	glGenFramebuffers(1, &FBO);
	glBindFramebuffer(GL_FRAMEBUFFER, FBO);

	// multisampled color renderbuffer
	glGenRenderbuffers(1, &colorRBO);
	glBindRenderbuffer(GL_RENDERBUFFER, colorRBO);
	glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGB8, width, height);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, colorRBO);

	// multisampled depth stencil renderbuffer
	glGenRenderbuffers(1, &depthRBO);
	glBindRenderbuffer(GL_RENDERBUFFER, depthRBO);
	glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH24_STENCIL8, width, height);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depthRBO);

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		printf("MSAA framebuffer is not yet complete\n");
	}
	// resolve framebuffer
	glGenFramebuffers(1, &resolveFBO);
	glBindFramebuffer(GL_FRAMEBUFFER, resolveFBO);

	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		printf("Resolve framebuffer is not complete\n");
	}

	unbind();

}

void FrameBuffer::deleteBuffer() {
	glDeleteFramebuffers(1, &FBO);
	glDeleteFramebuffers(1, &resolveFBO);
	glDeleteRenderbuffers(1, &colorRBO);
	glDeleteRenderbuffers(1, &depthRBO);
	glDeleteTextures(1, &texture);
}

void FrameBuffer::bind() {
	glBindFramebuffer(GL_FRAMEBUFFER, FBO);
}

void FrameBuffer::unbind() {
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void FrameBuffer::resolve() {

	glBindFramebuffer(GL_READ_FRAMEBUFFER, FBO);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolveFBO);

	glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);

	unbind();
}

unsigned int FrameBuffer::getTexture() {
	return texture;
}

// --------------------------Vertex Buffer---------------------------

void VertexBuffer::bind() {
	glBindVertexArray(VAO);
}

void VertexBuffer::bindVBO() {
	glBindBuffer(GL_ARRAY_BUFFER, VBO);
}

void VertexBuffer::unbind() {
	glBindVertexArray(0);
}

void VertexBuffer::unbindVBO() {
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void VertexBuffer::createBuffer(GLsizeiptr size, const void* data) {

	if (VAO) {
		deleteBuffer();
	}

	glGenVertexArrays(1, &VAO);
	glGenBuffers(1, &VBO);

	bind();

	glBindBuffer(GL_ARRAY_BUFFER, VBO);
	glBufferData(GL_ARRAY_BUFFER, size, data, GL_STATIC_DRAW);

	unbind();
}

void VertexBuffer::enableAttribute(GLuint index, GLint size, GLenum type, GLsizei stride, const void* pointer) {
	glVertexAttribPointer(index, size, type, GL_FALSE, stride, pointer);
	glEnableVertexAttribArray(index);
}

void VertexBuffer::copyBuffer(GLuint srcBuffer, GLsizeiptr size) {
	glBindBuffer(GL_COPY_READ_BUFFER, srcBuffer);
	glBindBuffer(GL_COPY_WRITE_BUFFER, VBO);
	glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, size);
	glBindBuffer(GL_COPY_READ_BUFFER, 0);
	glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
}

void VertexBuffer::bufferSubData(GLsizeiptr size, const void* data) {
	glBufferSubData(GL_ARRAY_BUFFER, 0, size, data);
}

GLuint VertexBuffer::getVBO() {
	return VBO;
}

GLuint VertexBuffer::getVAO() {
	return VAO;
}

void VertexBuffer::deleteBuffer() {
	glDeleteVertexArrays(1, &VAO);
	glDeleteBuffers(1, &VBO);
}

// --------------------------Element Buffer---------------------------
void ElementBuffer::createBuffer(GLsizeiptr size, const void* data) {

	if (EBO) {
		deleteBuffer();
	}

	glGenBuffers(1, &EBO);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, size, data, GL_STATIC_DRAW);
}

void ElementBuffer::copyBuffer(GLuint srcBuffer, GLsizeiptr size) {
	glBindBuffer(GL_COPY_READ_BUFFER, srcBuffer);
	glBindBuffer(GL_COPY_WRITE_BUFFER, EBO);
	glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, size);
	glBindBuffer(GL_COPY_READ_BUFFER, 0);
	glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
}

GLuint ElementBuffer::getEBO() {
	return EBO;
}

void ElementBuffer::deleteBuffer() {
	glDeleteBuffers(1, &EBO);
}

// --------------------------Texture Buffer---------------------------
void TextureBuffer::createBuffer(GLenum internalFormat, int nx, int ny,  GLenum format, GLenum type, const void* data) {

	if (TBO) {
		deleteBuffer();
	}

	glGenTextures(1, &TBO);
	glBindTexture(GL_TEXTURE_2D, TBO);

	//	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);	// change if your data is not tightly packed (e.g. RGB format) and color looks corrupted
	glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, nx, ny, 0, format, type, data);

	glBindTexture(GL_TEXTURE_2D, 0);
}

void TextureBuffer::updateBuffer(GLenum internalFormat, int nx, int ny, GLenum format, GLenum type, const void* data) {
	bind();
	glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, nx, ny, 0, format, type, data);
	unbind();
}

void TextureBuffer::deleteBuffer() {
	glDeleteTextures(1, &TBO);
}

void TextureBuffer::bind() {
	glBindTexture(GL_TEXTURE_2D, TBO);
}

void TextureBuffer::unbind() {
	glBindTexture(GL_TEXTURE_2D, 0);
}

unsigned int TextureBuffer::getTextureID() {
	return TBO;
}
#include "buffer_manager.h"

#include <cstdio>
#include <imgui_impl_opengl3.h>

#include "stb_image.h"
#include "clipboard.h"
#include "printer.h"

// ===================================================================
// --------------------------Frame Buffer---------------------------
// ===================================================================
void FrameBuffer::createBuffer(int width, int height, int samples) {

	this->width = width;
	this->height = height;
	this->samples = samples;

	if (FBO) {
		deleteBuffer();
	}

	bool useMSAA = samples > 1;

	if (useMSAA) {
		createMSAABuffer(width, height, samples);
	}
	else {
		createNoMSAABuffer(width, height);
	}

}

void FrameBuffer::createNoMSAABuffer(int width, int height) {

	glGenFramebuffers(1, &FBO);
	glBindFramebuffer(GL_FRAMEBUFFER, FBO);

	// Color texture, directly usable by ImGui::Image
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glFramebufferTexture2D(
		GL_FRAMEBUFFER,
		GL_COLOR_ATTACHMENT0,
		GL_TEXTURE_2D,
		texture,
		0
	);

	// Normal, non-MSAA depth/stencil renderbuffer
	glGenRenderbuffers(1, &depthRBO);
	glBindRenderbuffer(GL_RENDERBUFFER, depthRBO);

	glRenderbufferStorage(
		GL_RENDERBUFFER,
		GL_DEPTH24_STENCIL8,
		width,
		height
	);

	glFramebufferRenderbuffer(
		GL_FRAMEBUFFER,
		GL_DEPTH_STENCIL_ATTACHMENT,
		GL_RENDERBUFFER,
		depthRBO
	);

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		printf("Non-MSAA framebuffer is not complete\n");
	}

	glBindRenderbuffer(GL_RENDERBUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

}

void FrameBuffer::createMSAABuffer(int width, int height, int samples) {

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

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		printf("Resolve framebuffer is not complete\n");
	}

	glBindTexture(GL_TEXTURE_2D, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

}

void FrameBuffer::create2DBuffer(int width, int height, GLenum internalFormat, GLenum format, GLenum type) {

	this->width = width;
	this->height = height;

	if (FBO) {
		deleteBuffer();
	}
	if (width <= 0 || height <= 0) {
		printf("Invalid FBO size: %d x %d\n", width, height);
		return;
	}

	glGenFramebuffers(1, &FBO);
	glBindFramebuffer(GL_FRAMEBUFFER, FBO);
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);

	glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, type, nullptr);

	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		printf("Simple framebuffer is not complete\n");
	}

	glBindTexture(GL_TEXTURE_2D, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void FrameBuffer::beginOffScreenImGuiRender(GLint& oldFBO, GLint (&oldViewport)[4], ImVec2& oldDisplaySize, ImVec2& oldFramebufferScale) {
	
	// get current framebuffer and store it so we can rebind it later
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFBO);
	glGetIntegerv(GL_VIEWPORT, oldViewport);

	// bind new frame buffer
	bind();
	glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	// start a temporary imgui frame
	ImGui_ImplOpenGL3_NewFrame();

	ImGuiIO& io = ImGui::GetIO();
	oldDisplaySize = io.DisplaySize;
	oldFramebufferScale = io.DisplayFramebufferScale;

	io.DisplaySize = ImVec2((float)width, (float)height);
	io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);

	ImGui::NewFrame();

	// build imgui draw commands. change if needed
	ImGui::SetNextWindowPos(ImVec2(0, 0));
	ImGui::SetNextWindowSize(ImVec2((float)width, (float)height));

}

void FrameBuffer::endOffScreenImGuiRender(const GLint& oldFBO, const GLint(&oldViewport)[4], const ImVec2& oldDisplaySize, const ImVec2 oldFramebufferScale) {
	// render the imgui frame
	ImGui::Render();

	bind();
	glDrawBuffer(GL_COLOR_ATTACHMENT0);

	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

	std::vector<unsigned char> pixels = readPixelsRGBA();
	bool copied = copyRGBAToClipboard(pixels.data(), width, height);

	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = oldDisplaySize;
	io.DisplayFramebufferScale = oldFramebufferScale;

	glBindFramebuffer(GL_FRAMEBUFFER, oldFBO);
	glViewport(oldViewport[0], oldViewport[1], oldViewport[2], oldViewport[3]);

}

std::vector<unsigned char> FrameBuffer::readPixelsRGBA() {
	std::vector<unsigned char> pixels(width * height * 4);

	glBindFramebuffer(GL_FRAMEBUFFER, FBO);
	glReadBuffer(GL_COLOR_ATTACHMENT0);

	glPixelStorei(GL_PACK_ALIGNMENT, 1);

	glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	return pixels;
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
	glViewport(0, 0, width, height);
}

void FrameBuffer::unbind() {
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void FrameBuffer::resolve() {

	if (samples <= 1) return;

	glBindFramebuffer(GL_READ_FRAMEBUFFER, FBO);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolveFBO);

	glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

unsigned int FrameBuffer::getTextureID() {
	return texture;
}

// ===================================================================
// --------------------------Vertex Buffer---------------------------
// ===================================================================
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
	glBindBuffer(GL_ARRAY_BUFFER, VBO);
	glBufferSubData(GL_ARRAY_BUFFER, 0, size, data);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
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

// ===================================================================
// --------------------------Element Buffer---------------------------
// ===================================================================
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

// ===================================================================
// --------------------------Texture Buffer---------------------------
// ===================================================================
void TextureBuffer::createBuffer(GLenum internalFormat, int nx, int ny,  GLenum format, GLenum type, const void* data) {

	if (TBO) {
		deleteBuffer();
	}

	glGenTextures(1, &TBO);
	glBindTexture(GL_TEXTURE_2D, TBO);

	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);	// change if your data is not tightly packed (e.g. RGB format) and color looks corrupted
	glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, nx, ny, 0, format, type, data);

	glBindTexture(GL_TEXTURE_2D, 0);
}

void TextureBuffer::createBuffer(const char* path) {

	int width, height, channels;
	unsigned char* data = stbi_load(path, &width, &height, &channels, 4);

	if (TBO) {
		deleteBuffer();
	}

	glGenTextures(1, &TBO);
	bind();
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	unbind();

	stbi_image_free(data);
}

void TextureBuffer::updateBuffer(int nx, int ny, GLenum format, GLenum type, const void* data) {
	bind();
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, nx, ny, format, type, data);
	unbind();
}

void TextureBuffer::setTextureShading(GLint filter) {

	glBindTexture(GL_TEXTURE_2D, TBO);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);

	glBindTexture(GL_TEXTURE_2D, 0);

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
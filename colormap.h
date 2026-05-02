#pragma once
#include <glm/fwd.hpp>
#include <vector>
#include "buffer_manager.h"
#include <string>
class Colormap {
public:

	Colormap();

	int currentItem = 0;
	float (*currentLUT)[3];
	const char* items[4] = { "Turbo", "Jet", "Parula", "HSV" };
	//const char* items[4] = {"Turbo", "Jet", "Parula", "HSV"};
	// binds current texture buffer
	void bind();

	// unbinds current texture buffer
	void unbind();

	// changes the colormap by updating currentLUT and currentItem
	void setColormap(int itemIndex);

	// get texture ID
	unsigned int getTextureID();

	// get the color vector given a value
	glm::vec3 getColor(double val, double vmin, double vmax);

	// get name of current colormap as a std::string
	std::string getColormap();

private:
	std::vector<TextureBuffer> colormapBuffers;
	TextureBuffer turboBuffer;
	TextureBuffer jetBuffer;
	TextureBuffer parulaBuffer;
	TextureBuffer hsvBuffer;
};
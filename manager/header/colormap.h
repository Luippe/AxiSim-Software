#pragma once
#include <glm/fwd.hpp>
#include <vector>
#include "buffer_manager.h"
#include <string>

class Colormap {
public:

	Colormap();

	using ColormapLUT = unsigned char[256][3];

	int currentItem = 0;
	const unsigned char (*currentLUT)[3];
	const char* items[6] = { "Turbo", "Parula", "HSV", "Gray", "Sky", "Abyss"};
	
	// binds current texture buffer
	void bind();

	// unbinds current texture buffer
	void unbind();

	// changes the colormap by updating currentLUT and currentItem
	void setColormap(int itemIndex);

	// get texture ID
	unsigned int getTextureID();

	// get name of current colormap as a std::string
	std::string getColormap();

	// get the rgb values of a colormap
	const ColormapLUT& getColormapValue(std::string colormap);
	const ColormapLUT& getColormapValue();
	// check if colormap is blank
	bool isBlankColormap(const ColormapLUT& cmap);

private:
	std::vector<TextureBuffer> colormapBuffers;
	TextureBuffer turboBuffer;
	TextureBuffer parulaBuffer;
	TextureBuffer hsvBuffer;
	TextureBuffer grayBuffer;
	TextureBuffer skyBuffer;
	TextureBuffer abyssBuffer;

};
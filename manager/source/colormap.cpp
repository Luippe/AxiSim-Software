#include "pch.h"

#include <stdexcept>

#include "colormap.h"

#include "colormap_manager.h"
#include "math_func.h"
#include "printer.h"

Colormap::Colormap() {

	turboBuffer.createBuffer(GL_RGB8, 1, 256, GL_RGB, GL_UNSIGNED_BYTE, &Turbo[0][0]);
	parulaBuffer.createBuffer(GL_RGB8, 1, 256, GL_RGB, GL_UNSIGNED_BYTE, &Parula[0][0]);
	hsvBuffer.createBuffer(GL_RGB8, 1, 256, GL_RGB, GL_UNSIGNED_BYTE, &HSV[0][0]);
	grayBuffer.createBuffer(GL_RGB8, 1, 256, GL_RGB, GL_UNSIGNED_BYTE, &Gray[0][0]);
	skyBuffer.createBuffer(GL_RGB8, 1, 256, GL_RGB, GL_UNSIGNED_BYTE, &Sky[0][0]);
	abyssBuffer.createBuffer(GL_RGB8, 1, 256, GL_RGB, GL_UNSIGNED_BYTE, &Abyss[0][0]);

	colormapBuffers.push_back(turboBuffer);
	colormapBuffers.push_back(parulaBuffer);
	colormapBuffers.push_back(hsvBuffer);
	colormapBuffers.push_back(grayBuffer);
	colormapBuffers.push_back(skyBuffer);
	colormapBuffers.push_back(abyssBuffer);

	currentLUT = Turbo;

}

const Colormap::ColormapLUT& Colormap::getColormapValue(std::string colormap) {
	
	std::string cmap = toLower(colormap);

	if (cmap == "turbo") {
		return Turbo;
	}
	else if (cmap == "parula") {
		return Parula;
	}
	else if (cmap == "hsv") {
		return HSV;
	}
	else if (cmap == "gray") {
		return Gray;
	}
	else if (cmap == "sky") {
		return Sky;
	}
	else if (cmap == "abyss") {
		return Abyss;
	}

	throw Blank;
}

const Colormap::ColormapLUT& Colormap::getColormapValue() {
	return getColormapValue(items[currentItem]);
}


void Colormap::setColormap(int itemIndex) {

	currentItem = itemIndex;

	switch (currentItem) {
	case 0:
		currentLUT = Turbo;
		break;
	case 1:
		currentLUT = Parula;
		break;
	case 2:
		currentLUT = HSV;
		break;
	case 3:
		currentLUT = Gray;
		break;
	case 4:
		currentLUT = Sky;
		break;
	case 5:
		currentLUT = Abyss;
		break;
	default:
		currentLUT = Turbo;
		currentItem = 0;
		break;
	}
}

void Colormap::bind() {
	colormapBuffers[currentItem].bind();
}

void Colormap::unbind() {
	colormapBuffers[currentItem].unbind();
}

unsigned int Colormap::getTextureID() {
	return colormapBuffers[currentItem].getTextureID();
}

std::string Colormap::getColormap() {
	return items[currentItem];
}

bool Colormap::isBlankColormap(const ColormapLUT& cmap) {
	return &cmap == &Blank;
}
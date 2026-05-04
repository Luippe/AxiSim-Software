#include "pch.h"
#include "colormap.h"
#include "colormap_manager.h"

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

glm::vec3 Colormap::getColor(double val, double vmin, double vmax) {
	double val_norm = (val - vmin) / (vmax - vmin);
	int idx = (int)(val_norm * 255.0);
	return glm::vec3(
		currentLUT[idx][0] / 255.0f,
		currentLUT[idx][1] / 255.0f,
		currentLUT[idx][2] / 255.0f
		);
}

std::string Colormap::getColormap() {
	return items[currentItem];
}
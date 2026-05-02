#include "pch.h"
#include "colormap.h"
#include "colormap_manager.h"

Colormap::Colormap() {

	turboBuffer.createBuffer(GL_RGB32F, 1, 256, GL_RGB, &Turbo[0][0]);
	jetBuffer.createBuffer(GL_RGB32F, 1, 256, GL_RGB, &Jet[0][0]);
	parulaBuffer.createBuffer(GL_RGB32F, 1, 256, GL_RGB, &Parula[0][0]);
	hsvBuffer.createBuffer(GL_RGB32F, 1, 256, GL_RGB, &HSV[0][0]);

	colormapBuffers.push_back(turboBuffer);
	colormapBuffers.push_back(jetBuffer);
	colormapBuffers.push_back(parulaBuffer);
	colormapBuffers.push_back(hsvBuffer);

	currentLUT = Turbo;
}

void Colormap::setColormap(int itemIndex) {

	currentItem = itemIndex;

	switch (currentItem) {
	case 0:
		currentLUT = Turbo;
		break;
	case 1:
		currentLUT = Jet;
		break;
	case 2:
		currentLUT = Parula;
		break;
	case 3:
		currentLUT = HSV;
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
		currentLUT[idx][0],
		currentLUT[idx][1],
		currentLUT[idx][2]
		);
}

std::string Colormap::getColormap() {
	return items[currentItem];
}
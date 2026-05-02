#include "pch.h"
#include "printer.h"

void printVec3(glm::vec3 vec) {
	printf("%f, %f, %f\n", vec.x, vec.y, vec.z);
}

void printFloat(float val) {
	printf("%f\n", val);
}

void printInt(int val) {
	printf("%d\n", val);
}

void printVec2(glm::vec2 vec) {
	printf("%f, %f\n", vec.x, vec.y);
}

void check() {
	printf("THE PROGRAM IS RUNNING HERE!\n");
}

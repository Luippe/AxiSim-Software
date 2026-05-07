#include "pch.h"
#include "printer.h"

void printVec3(glm::vec3 vec) {
	printf("%f, %f, %f\n", vec.x, vec.y, vec.z);
}

void printVec2(glm::vec2 vec) {
	printf("%f, %f\n", vec.x, vec.y);
}

void check() {
	printf("THE PROGRAM IS RUNNING HERE!\n");
}

void checkInt(int n) {
	printf("%d\n", n);
}
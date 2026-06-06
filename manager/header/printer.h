#pragma once
#include <glm/fwd.hpp>

void printVec3(glm::vec3 vec);

void printVec2(glm::vec2 vec);

void check();

void checkInt(int n);

template<typename... Args>
void printStr(Args... args) {
	(printf("%s ", args), ...);
	printf("\n");
}

template<typename... Args>
void printFloat(Args... args) {
	(printf("%f ", args), ...);
	printf("\n");
}

template<typename... Args>
void printInt(Args... args) {
	(printf("%d ", args), ...);
	printf("\n");
}

template<typename... Args>
void printBool(Args... args) {
	(printf("%s ", args ? "true" : "false"), ...);
	printf("\n");
}

template<typename... Args>
void printSizeT(Args... args) {
	(printf("%zu ", args), ...);
	printf("\n");
}

template<typename... Args>
void printSize(Args... args) {
	(printf("%zu ", args.size()), ...);
	printf("\n");
}

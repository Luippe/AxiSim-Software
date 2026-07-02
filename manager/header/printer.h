#pragma once
#include <glm/fwd.hpp>
#include <string>
#include <cstdio>

void printVec3(glm::vec3 vec);

void printVec2(glm::vec2 vec);

void check();


inline void printOne(const char* value) {
    std::printf("%s", value);
}

inline void printOne(const std::string& value) {
    std::printf("%s", value.c_str());
}

inline void printOne(int value) {
    std::printf("%d", value);
}

inline void printOne(float value) {
    std::printf("%f", value);
}

inline void printOne(double value) {
    std::printf("%f", value);
}

inline void printOne(bool value) {
    std::printf("%s", value ? "true" : "false");
}

inline void printOne(size_t value) {
    std::printf("%zu", value);
}

template<typename... Args>
void print(const Args&... args) {
    ((printOne(args), std::printf(" ")), ...);
    std::printf("\n");
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

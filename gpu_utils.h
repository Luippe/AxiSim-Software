#pragma once
#include <vector>
#include "cuda_runtime.h"

#define CUDA_CHECK(x) do { \
  cudaError_t err = (x); \
  if (err != cudaSuccess) { \
    printf("CUDA error %s:%d: %s\n", __FILE__, __LINE__, cudaGetErrorString(err)); \
    std::abort(); \
  } \
} while(0)

template<typename T>
T* deviceAlloc(size_t count) {
	T* ptr = nullptr;
	cudaMalloc(&ptr, count * sizeof(T));
	return ptr;
}

template<typename T>
T* deviceAllocHost(size_t count) {
	T* ptr = nullptr;
	cudaMallocHost(&ptr, count * sizeof(T));
	return ptr;
}

template <typename T>
T* copyHostToDevice(const T* host, size_t count) {
	T* ptr = deviceAlloc<T>(count);
	cudaMemcpy(ptr, host, count * sizeof(T), cudaMemcpyHostToDevice);
	return ptr;
}

template <typename T>
std::vector<T> copyDeviceToHostVector(const T* d_ptr, size_t count) {
	std::vector<T> h_vec(count);
	cudaMemcpy(h_vec.data(), d_ptr, count * sizeof(T), cudaMemcpyDeviceToHost);
	return h_vec;
}

template <typename T>
void freeDev(T*& ptr) {
	if (ptr) {
		CUDA_CHECK(cudaFree(ptr));
		ptr = nullptr;
	}
}

template <typename T>
void freeHost(T*& ptr) {
	if (ptr) {
		CUDA_CHECK(cudaFreeHost(ptr));
		ptr = nullptr;
	}
}

template <typename... Ptrs>
void freeAllDev(Ptrs*&... ptrs) {
	(freeDev(ptrs), ...);
}

template <typename... Ptrs>
void freeAllHost(Ptrs*&... ptrs) {
	(freeHost(ptrs), ...);
}
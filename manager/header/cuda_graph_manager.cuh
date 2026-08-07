#pragma once

#include <cuda_runtime.h>

#include "gpu_utils.h"

struct CudaGraph {

	// The stream this graph was captured on, recorded by the capture site rather
	// than handed in at construction: a CudaGraph has no use for a stream until
	// there is a graph, and every consumer below (upload/launch/destroy) wants
	// exactly the stream the nodes were recorded on. Held BY VALUE -- cudaStream_t
	// is a pointer typedef, so a reference bought nothing but a dangling-lifetime
	// invariant, and it deleted the default constructor.
	cudaStream_t stream = nullptr;
	cudaGraph_t graph = nullptr;
	cudaGraphExec_t exec = nullptr;

	CudaGraph() = default;

	// exec/graph are owned handles freed in the destructor; a copy would alias
	// then double-destroy them. The old reference member suppressed copy
	// ASSIGNMENT only -- copy construction was always available and unsafe.
	CudaGraph(const CudaGraph&) = delete;
	CudaGraph& operator=(const CudaGraph&) = delete;

	void beginCapture() {
		CUDA_CHECK(cudaStreamBeginCapture(stream, cudaStreamCaptureModeThreadLocal));
	}

	void endCapture() {
		CUDA_CHECK(cudaStreamEndCapture(stream, &graph));
	}

	void instantiate() {
		CUDA_CHECK(cudaGraphInstantiate(&exec, graph, nullptr, nullptr, 0));
	}

	void upload() {
		CUDA_CHECK(cudaGraphUpload(exec, stream));
	}

	void launch() {
		CUDA_CHECK(cudaGraphLaunch(exec, stream));
	}

	void destroy() {
		// Synchronize the stream that owns the existing executable before
		// destroying graph resources which one of its launches may still use.
		//
		// Guarded: a CudaGraph that was never captured on still has a null stream,
		// and cudaStreamSynchronize(nullptr) means "sync the legacy default stream",
		// not "do nothing". Only the sync is skipped -- the handles are still
		// released below, so this can never leak.
		if (stream) {
			CUDA_CHECK(cudaStreamSynchronize(stream));
		}

		if (exec) {
			CUDA_CHECK(cudaGraphExecDestroy(exec));
			exec = nullptr;
		}

		if (graph) {
			CUDA_CHECK(cudaGraphDestroy(graph));
			graph = nullptr;
		}
	}

	~CudaGraph() {
		destroy();
	}
};
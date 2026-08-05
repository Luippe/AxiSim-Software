#pragma once

// Precompiled header. Wired up in CMakeLists.txt via target_precompile_headers,
// so every C++ translation unit in AxiSim gets this force-included -- it does not
// need to be #included by hand (the explicit includes that remain are harmless).
//
// Deliberately limited to third-party and standard-library headers. Nothing from
// src/, manager/, solver/, graphics/ or structs/ belongs here: adding a project
// header would rebuild every C++ file in the target whenever that header changes,
// including the files that never included it. Measured as ~50% off C++ compile
// time; pulling project headers in as well was only marginally faster and not
// worth the coupling.
//
// glad must come before anything that might pull in a GL header.

#define GLM_ENABLE_EXPERIMENTAL

#include <glad/glad.h>
#include <glm/glm.hpp>

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

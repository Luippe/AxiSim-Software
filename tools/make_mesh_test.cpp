// Generate mesh_test.axigeom, a compact stress case for AxiSim's unstructured
// constrained-Delaunay mesher.
//
// The sketch contains:
//   * one concave outer domain (tests exterior flood-fill clipping),
//   * one rectangular hole (tests a sparse obstacle loop), and
//   * one circular hole (tests a densely sampled curved obstacle loop).
//
// Coordinates are stored in base SI units (metres). In AxiSim, set the display
// length unit to mm to see the dimensions described below.
//
// Build from a Visual Studio developer shell:
//   cl /std:c++17 /EHsc /I structs tools\make_mesh_test.cpp /Fe:make_mesh_test.exe
//   make_mesh_test.exe mesh_test.axigeom

#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include "sketch_struct.h"

namespace {

template <typename T>
void writeVector(std::ofstream& out, const std::vector<T>& values) {
    static_assert(std::is_trivially_copyable_v<T>,
        "AxiSim geometry vectors must be bulk-writable");

    const size_t count = values.size();
    out.write(reinterpret_cast<const char*>(&count), sizeof(count));
    out.write(reinterpret_cast<const char*>(values.data()), count * sizeof(T));
}

template <typename T>
bool readVector(std::ifstream& in, std::vector<T>& values) {
    static_assert(std::is_trivially_copyable_v<T>,
        "AxiSim geometry vectors must be bulk-readable");

    size_t count = 0;
    in.read(reinterpret_cast<char*>(&count), sizeof(count));
    values.resize(count);
    in.read(reinterpret_cast<char*>(values.data()), count * sizeof(T));
    return static_cast<bool>(in);
}

void writeInt(std::ofstream& out, int value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

bool readInt(std::ifstream& in, int& value) {
    in.read(reinterpret_cast<char*>(&value), sizeof(value));
    return static_cast<bool>(in);
}

template <typename T>
bool vectorsMatch(const std::vector<T>& a, const std::vector<T>& b) {
    return a.size() == b.size() &&
        (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(T)) == 0);
}

// AxiSim currently serializes whole trivially-copyable structs, including their
// padding. Clear that padding so this generated test artifact is deterministic
// and never carries indeterminate process bytes.
void zeroSketchPadding(SketchModel& sketch) {
    for (SketchPoint& value : sketch.points) {
        const int id = value.id;
        const Vec2 pos = value.pos;
        const bool selected = value.selected;
        std::memset(&value, 0, sizeof(value));
        value.id = id;
        value.pos = pos;
        value.selected = selected;
    }

    for (SketchLine& value : sketch.lines) {
        const int id = value.id;
        const int p0 = value.p0;
        const int p1 = value.p1;
        const bool construction = value.construction;
        const bool selected = value.selected;
        std::memset(&value, 0, sizeof(value));
        value.id = id;
        value.p0 = p0;
        value.p1 = p1;
        value.construction = construction;
        value.selected = selected;
    }

    for (SketchCircle& value : sketch.circles) {
        const int id = value.id;
        const Vec2 center = value.center;
        const double radius = value.radius;
        const bool construction = value.construction;
        const bool selected = value.selected;
        std::memset(&value, 0, sizeof(value));
        value.id = id;
        value.center = center;
        value.radius = radius;
        value.construction = construction;
        value.selected = selected;
    }

    for (SketchArc& value : sketch.arcs) {
        const int id = value.id;
        const Vec2 center = value.center;
        const double radius = value.radius;
        const double startAngle = value.startAngle;
        const double endAngle = value.endAngle;
        const bool construction = value.construction;
        const bool selected = value.selected;
        std::memset(&value, 0, sizeof(value));
        value.id = id;
        value.center = center;
        value.radius = radius;
        value.startAngle = startAngle;
        value.endAngle = endAngle;
        value.construction = construction;
        value.selected = selected;
    }

    for (SketchRectangle& value : sketch.rectangles) {
        const int id = value.id;
        const Vec2 min = value.min;
        const Vec2 max = value.max;
        const bool construction = value.construction;
        const bool selected = value.selected;
        std::memset(&value, 0, sizeof(value));
        value.id = id;
        value.min = min;
        value.max = max;
        value.construction = construction;
        value.selected = selected;
    }

    for (SketchDimension& value : sketch.dimensions) {
        const int id = value.id;
        const SketchDimensionType type = value.type;
        const int entityID = value.entityID;
        const Vec2 labelPos = value.labelPos;
        const bool selected = value.selected;
        std::memset(&value, 0, sizeof(value));
        value.id = id;
        value.type = type;
        value.entityID = entityID;
        value.labelPos = labelPos;
        value.selected = selected;
    }
}

void writeGeometry(const std::string& path, const SketchModel& sketch) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("cannot open output file");
    }

    // Keep this field order synchronized with saveFromPathGeometry().
    writeVector(out, sketch.points);
    writeVector(out, sketch.lines);
    writeVector(out, sketch.circles);
    writeVector(out, sketch.arcs);
    writeVector(out, sketch.rectangles);
    writeVector(out, sketch.dimensions);

    writeInt(out, sketch.nextPointID);
    writeInt(out, sketch.nextLineID);
    writeInt(out, sketch.nextCircleID);
    writeInt(out, sketch.nextArcID);
    writeInt(out, sketch.nextRectangleID);
    writeInt(out, sketch.nextDimensionID);

    if (!out) {
        throw std::runtime_error("failed while writing output file");
    }
}

bool verifyGeometry(const std::string& path, const SketchModel& expected) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }

    SketchModel actual;
    if (!readVector(in, actual.points) ||
        !readVector(in, actual.lines) ||
        !readVector(in, actual.circles) ||
        !readVector(in, actual.arcs) ||
        !readVector(in, actual.rectangles) ||
        !readVector(in, actual.dimensions) ||
        !readInt(in, actual.nextPointID) ||
        !readInt(in, actual.nextLineID) ||
        !readInt(in, actual.nextCircleID) ||
        !readInt(in, actual.nextArcID) ||
        !readInt(in, actual.nextRectangleID) ||
        !readInt(in, actual.nextDimensionID)) {
        return false;
    }

    // Geometry files contain exactly this payload; trailing bytes indicate a bad
    // writer or a project file accidentally given the .axigeom extension.
    const bool atEnd = in.peek() == std::ifstream::traits_type::eof();

    return atEnd &&
        vectorsMatch(actual.points, expected.points) &&
        vectorsMatch(actual.lines, expected.lines) &&
        vectorsMatch(actual.circles, expected.circles) &&
        vectorsMatch(actual.arcs, expected.arcs) &&
        vectorsMatch(actual.rectangles, expected.rectangles) &&
        vectorsMatch(actual.dimensions, expected.dimensions) &&
        actual.nextPointID == expected.nextPointID &&
        actual.nextLineID == expected.nextLineID &&
        actual.nextCircleID == expected.nextCircleID &&
        actual.nextArcID == expected.nextArcID &&
        actual.nextRectangleID == expected.nextRectangleID &&
        actual.nextDimensionID == expected.nextDimensionID;
}

} // namespace

int main(int argc, char** argv) {
    const std::string path = argc > 1 ? argv[1] : "mesh_test.axigeom";

    SketchModel sketch;

    // Concave 120 mm x 60 mm outer boundary. The 20 mm-wide, 16 mm-deep
    // downward notch makes the fluid domain non-convex.
    const Vec2 outer[] = {
        { 0.000, 0.000 },
        { 0.120, 0.000 },
        { 0.120, 0.060 },
        { 0.085, 0.060 },
        { 0.085, 0.044 },
        { 0.065, 0.044 },
        { 0.065, 0.060 },
        { 0.000, 0.060 },
    };

    constexpr int outerCount = static_cast<int>(sizeof(outer) / sizeof(outer[0]));
    for (int i = 0; i < outerCount; ++i) {
        sketch.addLine(outer[i], outer[(i + 1) % outerCount]);
    }

    // A 20 mm x 18 mm rectangular hole.
    sketch.addRectangle({ 0.018, 0.016 }, { 0.038, 0.034 });

    // An 18 mm diameter circular hole. AxiSim samples this into at least 32
    // constrained segments before passing it to AxiMesh.
    sketch.addCircle({ 0.070, 0.021 }, 0.009);

    zeroSketchPadding(sketch);

    try {
        writeGeometry(path, sketch);
    }
    catch (const std::exception& error) {
        std::fprintf(stderr, "mesh_test generator: %s: %s\n", path.c_str(), error.what());
        return 1;
    }

    if (!verifyGeometry(path, sketch)) {
        std::fprintf(stderr, "mesh_test generator: read-back verification failed\n");
        return 2;
    }

    std::printf("wrote and verified %s\n", path.c_str());
    std::printf("  outer loop: %d segments (concave)\n", outerCount);
    std::printf("  holes: 1 rectangle, 1 circle\n");
    std::printf("  sketch records: %zu points, %zu lines, %zu circles, %zu rectangles\n",
        sketch.points.size(), sketch.lines.size(), sketch.circles.size(),
        sketch.rectangles.size());
    std::printf("  on-disk struct sizes: point %zu, line %zu, circle %zu, rectangle %zu\n",
        sizeof(SketchPoint), sizeof(SketchLine), sizeof(SketchCircle),
        sizeof(SketchRectangle));

    return 0;
}

#pragma once

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "core_struct.h"

enum class SketchDimensionType {
    LineLength,
    CircleRadius,
    RectangleWidth,
    RectangleHeight
};

struct SketchDimension {
    int id = -1;
    SketchDimensionType type = SketchDimensionType::LineLength;
    int entityID = -1;
    Vec2 labelPos{};
    bool selected = false;
};

enum class SketchTool {
    Select,
    Line,
    Rectangle,
    Circle,
    Dimension,
    Trim,
    Erase
};

enum class SketchEntityType {
    Point,
    Line,
    Circle,
    Rectangle,
    Arc
};

struct SketchBound {
    Vec2 min{};
    Vec2 max{};
};

struct SketchPoint {
    int id = -1;
    Vec2 pos{};
    bool selected = false;
};

struct SketchLine {
    int id = -1;
    int p0 = -1;
    int p1 = -1;
    bool construction = false;
    bool selected = false;
};

struct SketchCircle {
    int id = -1;
    Vec2 center{};
    double radius = 0.0;
    bool construction = false;
    bool selected = false;
};

struct SketchArc {
    int id = -1;
    Vec2 center{};
    double radius = 0.0;
    double startAngle = 0.0;
    double endAngle = 0.0;
    bool construction = false;
    bool selected = false;
};

struct SketchRectangle {
    int id = -1;
    Vec2 min{};
    Vec2 max{};
    bool construction = false;
    bool selected = false;
};

inline SketchBound makeBound(Vec2 a, Vec2 b) {
    return {
        Vec2{ std::min(a.z, b.z), std::min(a.r, b.r) },
        Vec2{ std::max(a.z, b.z), std::max(a.r, b.r) }
    };
}

inline bool boundsInside(SketchBound box, SketchBound region) {
    return box.min.z >= region.min.z &&
        box.max.z <= region.max.z &&
        box.min.r >= region.min.r &&
        box.max.r <= region.max.r;
}

inline bool boundsOverlap(SketchBound a, SketchBound b) {
    return a.min.z <= b.max.z &&
        a.max.z >= b.min.z &&
        a.min.r <= b.max.r &&
        a.max.r >= b.min.r;
}

inline SketchBound circleBound(const SketchCircle& circle) {
    return {
        Vec2{ circle.center.z - circle.radius, circle.center.r - circle.radius },
        Vec2{ circle.center.z + circle.radius, circle.center.r + circle.radius }
    };
}

inline SketchBound rectangleBound(const SketchRectangle& rect) {
    return { rect.min, rect.max };
}

inline SketchBound arcBound(const SketchArc& arc) {
    return {
        Vec2{ arc.center.z - arc.radius, arc.center.r - arc.radius },
        Vec2{ arc.center.z + arc.radius, arc.center.r + arc.radius }
    };
}

struct SketchModel {
    SketchTool activeTool = SketchTool::Select;

    std::vector<SketchPoint> points;
    std::vector<SketchLine> lines;
    std::vector<SketchCircle> circles;
    std::vector<SketchArc> arcs;
    std::vector<SketchRectangle> rectangles;
    std::vector<SketchDimension> dimensions;

    int nextPointID = 0;
    int nextLineID = 0;
    int nextCircleID = 0;
    int nextArcID = 0;
    int nextRectangleID = 0;
    int nextDimensionID = 0;


    int addPoint(Vec2 pos) {
        int id = nextPointID++;
        points.push_back({ id, pos });
        return id;
    }

    int addLine(Vec2 a, Vec2 b) {
        int p0 = addPoint(a);
        int p1 = addPoint(b);

        int id = nextLineID++;
        lines.push_back({ id, p0, p1 });
        return id;
    }

    int addCircle(Vec2 center, double radius) {
        int id = nextCircleID++;
        circles.push_back({ id, center, radius });
        return id;
    }

    int addArc(Vec2 center, double radius, double startAngle, double endAngle) {
        int id = nextArcID++;
        arcs.push_back({ id, center, radius, startAngle, endAngle });
        return id;
    }

    int addRectangle(Vec2 a, Vec2 b) {
        Vec2 min{ std::min(a.z, b.z), std::min(a.r, b.r) };
        Vec2 max{ std::max(a.z, b.z), std::max(a.r, b.r) };

        int id = nextRectangleID++;
        rectangles.push_back({ id, min, max });
        return id;
    }

    int addDimension(SketchDimensionType type, int entityID, Vec2 labelPos) {
        for (SketchDimension& dimension : dimensions) {
            if (dimension.type == type && dimension.entityID == entityID) {
                dimension.labelPos = labelPos; // optional: move existing label
                return dimension.id;
            }
        }

        int id = nextDimensionID++;
        dimensions.push_back({ id, type, entityID, labelPos });
        return id;
    }

    bool removeDimension(int id) {
        auto it = std::remove_if(
            dimensions.begin(),
            dimensions.end(),
            [&](const SketchDimension& dimension) {
                return dimension.id == id;
            }
        );

        if (it == dimensions.end()) {
            return false;
        }

        dimensions.erase(it, dimensions.end());
        return true;
    }

    SketchPoint* findPoint(int id) {
        for (SketchPoint& point : points) {
            if (point.id == id) {
                return &point;
            }
        }

        return nullptr;
    }

    const SketchPoint* findPoint(int id) const {
        for (const SketchPoint& point : points) {
            if (point.id == id) {
                return &point;
            }
        }

        return nullptr;
    }

    SketchLine* findLine(int id) {
        for (SketchLine& line : lines) {
            if (line.id == id) {
                return &line;
            }
        }

        return nullptr;
    }

    const SketchLine* findLine(int id) const {
        for (const SketchLine& line : lines) {
            if (line.id == id) {
                return &line;
            }
        }

        return nullptr;
    }

    SketchCircle* findCircle(int id) {
        for (SketchCircle& circle : circles) {
            if (circle.id == id) {
                return &circle;
            }
        }

        return nullptr;
    }

    const SketchCircle* findCircle(int id) const {
        for (const SketchCircle& circle : circles) {
            if (circle.id == id) {
                return &circle;
            }
        }

        return nullptr;
    }

    SketchArc* findArc(int id) {
        for (SketchArc& arc : arcs) {
            if (arc.id == id) {
                return &arc;
            }
        }

        return nullptr;
    }

    const SketchArc* findArc(int id) const {
        for (const SketchArc& arc : arcs) {
            if (arc.id == id) {
                return &arc;
            }
        }

        return nullptr;
    }

    SketchRectangle* findRectangle(int id) {
        for (SketchRectangle& rect : rectangles) {
            if (rect.id == id) {
                return &rect;
            }
        }

        return nullptr;
    }

    const SketchRectangle* findRectangle(int id) const {
        for (const SketchRectangle& rect : rectangles) {
            if (rect.id == id) {
                return &rect;
            }
        }

        return nullptr;
    }

    SketchDimension* findDimension(int id) {
        for (SketchDimension& dimension : dimensions) {
            if (dimension.id == id) {
                return &dimension;
            }
        }

        return nullptr;
    }

    const SketchDimension* findDimension(int id) const {
        for (const SketchDimension& dimension : dimensions) {
            if (dimension.id == id) {
                return &dimension;
            }
        }

        return nullptr;
    }

    double getLineLength(int lineID) const {
        const SketchLine* line = findLine(lineID);
        if (!line) {
            return 0.0;
        }

        const SketchPoint* p0 = findPoint(line->p0);
        const SketchPoint* p1 = findPoint(line->p1);
        if (!p0 || !p1) {
            return 0.0;
        }

        double dz = p1->pos.z - p0->pos.z;
        double dr = p1->pos.r - p0->pos.r;

        return std::sqrt(dz * dz + dr * dr);
    }

    double getRectangleWidth(int rectangleID) const {
        const SketchRectangle* rect = findRectangle(rectangleID);
        return rect ? rect->max.z - rect->min.z : 0.0;
    }

    double getRectangleHeight(int rectangleID) const {
        const SketchRectangle* rect = findRectangle(rectangleID);
        return rect ? rect->max.r - rect->min.r : 0.0;
    }

    double getDimensionValue(const SketchDimension& dimension) const {
        switch (dimension.type) {
        case SketchDimensionType::LineLength:
            return getLineLength(dimension.entityID);
        case SketchDimensionType::CircleRadius: {
            const SketchCircle* circle = findCircle(dimension.entityID);
            return circle ? circle->radius : 0.0;
        }
        case SketchDimensionType::RectangleWidth:
            return getRectangleWidth(dimension.entityID);
        case SketchDimensionType::RectangleHeight:
            return getRectangleHeight(dimension.entityID);
        }
        return 0.0;
    }

    double getDimensionValue(int dimensionID) const {
        const SketchDimension* dimension = findDimension(dimensionID);
        return dimension ? getDimensionValue(*dimension) : 0.0;
    }

    bool setCircleRadius(int circleID, double radius) {
        SketchCircle* circle = findCircle(circleID);
        if (!circle || radius <= 0.0) {
            return false;
        }

        circle->radius = radius;
        return true;
    }

    bool setLineLength(int lineID, double length) {
        SketchLine* line = findLine(lineID);
        if (!line || length <= 0.0) {
            return false;
        }

        SketchPoint* p0 = findPoint(line->p0);
        SketchPoint* p1 = findPoint(line->p1);
        if (!p0 || !p1) {
            return false;
        }

        double dz = p1->pos.z - p0->pos.z;
        double dr = p1->pos.r - p0->pos.r;
        double oldLength = std::sqrt(dz * dz + dr * dr);

        if (oldLength <= 1e-12) {
            return false;
        }

        double scale = length / oldLength;

        p1->pos.z = p0->pos.z + dz * scale;
        p1->pos.r = p0->pos.r + dr * scale;

        return true;
    }

    bool setRectangleWidth(int rectangleID, double width) {
        SketchRectangle* rect = findRectangle(rectangleID);
        if (!rect || width <= 0.0) {
            return false;
        }

        rect->max.z = rect->min.z + width;
        return true;
    }

    bool setRectangleHeight(int rectangleID, double height) {
        SketchRectangle* rect = findRectangle(rectangleID);
        if (!rect || height <= 0.0) {
            return false;
        }

        rect->max.r = rect->min.r + height;
        return true;
    }

    bool setDimensionValue(int dimensionID, double value) {
        SketchDimension* dimension = findDimension(dimensionID);
        if (!dimension || value <= 0.0) {
            return false;
        }

        switch (dimension->type) {
        case SketchDimensionType::LineLength:
            return setLineLength(dimension->entityID, value);
        case SketchDimensionType::CircleRadius:
            return setCircleRadius(dimension->entityID, value);
        case SketchDimensionType::RectangleWidth:
            return setRectangleWidth(dimension->entityID, value);
        case SketchDimensionType::RectangleHeight:
            return setRectangleHeight(dimension->entityID, value);
        }

        return false;
    }

    SketchBound lineBounds(const SketchLine& line) const {
        const SketchPoint* p0 = findPoint(line.p0);
        const SketchPoint* p1 = findPoint(line.p1);

        if (!p0 || !p1) {
            return {};
        }

        return makeBound(p0->pos, p1->pos);
    }
};

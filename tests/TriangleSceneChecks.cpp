#include "TriangleScene.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <exception>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace
{
using madcat::sandbox::TrianglePose;
using madcat::sandbox::TriangleScene;
using Point = std::array<double, 3>;
using Clip = std::array<double, 4>;
void check(bool value, const char* message) { if (!value) { throw std::runtime_error(message); } }

Point rotateY(Point p, double degrees)
{
    const auto angle = degrees * std::numbers::pi / 180;
    return {std::cos(angle) * p[0] + std::sin(angle) * p[2], p[1], -std::sin(angle) * p[0] + std::cos(angle) * p[2]};
}

// Independent point-by-point reference: no Scene, GLM matrix or packing helper.
Clip reference(Point p, TrianglePose pose, double aspect, bool second = false)
{
    p = rotateY({p[0] * 1.1, p[1] * 0.9, p[2]}, second ? -35 : 25);
    p[0] += second ? 0.2 : -0.1;
    p[1] += second ? 0.05 : -0.05;
    p[2] += second ? -0.1 : 0;
    const bool modelMoved = pose == TrianglePose::ModelParent;
    const auto angle = (modelMoved ? 40 : 10) * std::numbers::pi / 180;
    p = {std::cos(angle) * p[0] - std::sin(angle) * p[1], std::sin(angle) * p[0] + std::cos(angle) * p[1], p[2]};
    p[0] += modelMoved ? 0.85 : 0.25;
    p[1] += modelMoved ? 0.25 : 0.1;
    p[2] += modelMoved ? -0.3 : 0;
    const bool cameraMoved = pose == TrianglePose::CameraParent;
    p[0] -= cameraMoved ? 0.4 : -0.15;
    p[1] -= cameraMoved ? 0.2 : 0.1;
    p[2] -= cameraMoved ? 0.25 : 0;
    p = rotateY(p, cameraMoved ? 8 : -5);
    p[2] -= 3;
    const double near = pose == TrianglePose::NearClipped ? 4 : 0.1;
    const double far = pose == TrianglePose::FarClipped ? 2 : 20;
    return {std::sqrt(3.0) * p[0] / aspect, -std::sqrt(3.0) * p[1],
        far / (near - far) * p[2] + near * far / (near - far), -p[2]};
}

Clip transform(const madcat::rhi::DrawTransform& matrix, Point point)
{
    Clip result{};
    for (size_t row = 0; row < 4; ++row)
    {
        result[row] = matrix.clipFromLocal[12 + row];
        for (size_t column = 0; column < 3; ++column) { result[row] += matrix.clipFromLocal[column * 4 + row] * point[column]; }
    }
    return result;
}

void projectionAndPacking()
{
    TriangleScene scene;
    for (const auto pose : {TrianglePose::Default, TrianglePose::ModelParent, TrianglePose::CameraParent,
        TrianglePose::NearClipped, TrianglePose::FarClipped, TrianglePose::DepthForward, TrianglePose::DepthReverse})
    {
        check(scene.selectPose(pose), "Pose selection failed");
        for (const auto aspect : {1.0f, 16.0f / 9.0f})
        {
            const auto matrix = scene.drawTransforms(aspect);
            check(matrix.has_value(), "Scene extraction failed");
            const bool depth = pose == TrianglePose::DepthForward || pose == TrianglePose::DepthReverse;
            check(matrix->count == (depth ? 2 : 1), "Wrong scene draw count");
            for (size_t draw = 0; draw < matrix->count; ++draw)
            {
                for (const Point point : {Point{0, 0.6, 0}, Point{0.6, -0.6, 0}, Point{-0.6, -0.6, 0}, Point{0.2, 0.1, 0.3}})
                {
                    const auto actual = transform(matrix->transforms[draw], point);
                    const auto expected = reference(point, pose, aspect, depth && (pose == TrianglePose::DepthReverse ? draw == 0 : draw == 1));
                    for (size_t row = 0; row < 4; ++row)
                    {
                        check(std::isfinite(actual[row]) && std::abs(actual[row] - expected[row]) < 0.0001,
                            "Packed matrix disagrees with analytic projection");
                    }
                }
            }
        }
    }
}

void clipping()
{
    TriangleScene scene;
    for (const auto pose : {TrianglePose::Default, TrianglePose::NearClipped, TrianglePose::FarClipped})
    {
        check(scene.selectPose(pose), "Pose selection failed");
        const auto matrix = scene.drawTransforms(1.5f);
        check(matrix.has_value(), "Scene extraction failed");
        for (const Point point : {Point{0, 0.6, 0}, Point{0.6, -0.6, 0}, Point{-0.6, -0.6, 0}})
        {
            const auto clip = transform(matrix->transforms[0], point);
            check(clip[3] > 0, "Triangle is behind camera");
            check(pose == TrianglePose::Default ? clip[2] >= 0 && clip[2] <= clip[3] :
                pose == TrianglePose::NearClipped ? clip[2] < 0 : clip[2] > clip[3], "Wrong zero-to-one clipping");
        }
    }
}

void snapshotsAndPoseChanges()
{
    TriangleScene scene;
    const auto initial = scene.drawTransforms(1.5f);
    check(initial.has_value(), "Initial snapshot failed");
    const auto saved = initial->transforms[0].clipFromLocal;
    check(scene.selectPose(TrianglePose::ModelParent), "Model parent edit failed");
    const auto moved = scene.drawTransforms(1.5f);
    check(moved && moved->transforms[0].clipFromLocal != saved && initial->transforms[0].clipFromLocal == saved, "Snapshot changed or parent edit lost");
    check(!scene.selectPose(static_cast<TrianglePose>(-1)) && !scene.selectPose(static_cast<TrianglePose>(7)), "Invalid pose accepted");
    check(scene.drawTransforms(1.5f)->transforms[0].clipFromLocal == moved->transforms[0].clipFromLocal, "Invalid pose mutated scene");
    check(scene.selectPose(TrianglePose::Default) && scene.drawTransforms(1.5f)->transforms[0].clipFromLocal == saved, "Reset did not restore pose");
}

void invalidAspectAndRecovery()
{
    TriangleScene scene;
    for (const auto aspect : {0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
    {
        check(!scene.drawTransforms(aspect), "Invalid aspect accepted");
    }
    check(scene.drawTransforms(2).has_value(), "Extraction failed to recover");
}

void reversedDrawOrder()
{
    TriangleScene scene;
    check(scene.selectPose(TrianglePose::DepthForward), "Depth pose failed");
    const auto forward = scene.drawTransforms(1.5f);
    check(scene.selectPose(TrianglePose::DepthReverse), "Reverse pose failed");
    const auto reverse = scene.drawTransforms(1.5f);
    check(forward && reverse && forward->count == 2 && reverse->count == 2 &&
        forward->transforms[0].clipFromLocal == reverse->transforms[1].clipFromLocal &&
        forward->transforms[1].clipFromLocal == reverse->transforms[0].clipFromLocal, "Order change mutated model transforms");
}
}

int main()
{
    const struct { const char* name; void (*run)(); } cases[] = {
        {"analytic projection and column packing for seven poses", projectionAndPacking},
        {"near and far clipping", clipping}, {"value snapshots, pose changes and reset", snapshotsAndPoseChanges},
        {"invalid aspect and recovery", invalidAspectAndRecovery}, {"reversed draw order", reversedDrawOrder}
    };
    for (const auto& test : cases)
    {
        try { test.run(); std::printf("Passed: %s\n", test.name); }
        catch (const std::exception& error) { std::fprintf(stderr, "Scene draw check failed (%s): %s\n", test.name, error.what()); return 1; }
    }
    std::printf("Scene draw checks passed: 5 cases.\n");
    return 0;
}

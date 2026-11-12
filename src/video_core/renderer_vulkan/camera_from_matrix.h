// SPDX-FileCopyrightText: Copyright 2026 CheezyEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cmath>
#include <optional>

#include "common/common_types.h"

namespace Vulkan::CameraFromMatrix {

/// The camera FSR frame generation asks for, taken apart from the game's view-projection.
struct Camera {
    f32 near_plane = 0.1f;
    f32 far_plane = 10000.0f;
    f32 fov = 1.0f;
    std::array<f32, 3> position{};
    std::array<f32, 3> up{0, 1, 0};
    std::array<f32, 3> right{1, 0, 0};
    std::array<f32, 3> forward{0, 0, 1};
};

inline Camera ExtractCamera(const std::array<f32, 16>& m) {
    using Vec = std::array<f64, 4>;
    const auto row = [&m](size_t r) {
        return Vec{m[r * 4], m[r * 4 + 1], m[r * 4 + 2], m[r * 4 + 3]};
    };
    const auto dot3 = [](const Vec& a, const Vec& b) {
        return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    };
    Vec r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
    Camera camera;
    const f64 length = std::sqrt(dot3(r3, r3));
    if (!(length > 1e-9) || !std::isfinite(length)) {
        return camera;
    }
    // Scale so clip w is the distance along the view axis; projective results don't change.
    for (Vec* v : {&r0, &r1, &r2, &r3}) {
        for (f64& x : *v) {
            x /= length;
        }
    }
    const auto unit = [&](Vec v) -> std::optional<std::array<f32, 3>> {
        const f64 along = dot3(v, r3);
        for (size_t i = 0; i < 3; ++i) {
            v[i] -= along * r3[i];
        }
        const f64 len = std::sqrt(dot3(v, v));
        if (!(len > 1e-9)) {
            return std::nullopt;
        }
        return std::array<f32, 3>{static_cast<f32>(v[0] / len), static_cast<f32>(v[1] / len),
                                  static_cast<f32>(v[2] / len)};
    };
    camera.forward = {static_cast<f32>(r3[0]), static_cast<f32>(r3[1]), static_cast<f32>(r3[2])};
    if (const auto right = unit(r0)) {
        camera.right = *right;
    }
    if (const auto up = unit(r1)) {
        camera.up = *up;
    }
    // Vertical focal length: row 1 without its forward (off-centre) part.
    Vec r1_lateral = r1;
    const f64 r1_along = dot3(r1, r3);
    for (size_t i = 0; i < 3; ++i) {
        r1_lateral[i] -= r1_along * r3[i];
    }
    const f64 focal = std::sqrt(dot3(r1_lateral, r1_lateral));
    if (focal > 1e-6 && std::isfinite(focal)) {
        camera.fov = static_cast<f32>(2.0 * std::atan(1.0 / focal));
    }
    // Clip z = a * w + c, so NDC z = a + c / w with [-1, 1] depth.
    const f64 a = dot3(r2, r3);
    const f64 c = r2[3] - a * r3[3];
    const f64 near_plane = c / (-1.0 - a);
    const f64 far_plane = std::abs(1.0 - a) > 1e-9 ? c / (1.0 - a) : 1e9;
    if (std::isfinite(near_plane) && near_plane > 0.0 && near_plane < 1e6) {
        camera.near_plane = static_cast<f32>(near_plane);
        camera.far_plane = static_cast<f32>(
            std::isfinite(far_plane) && far_plane > near_plane ? std::min(far_plane, 1e7)
                                                               : near_plane * 1e5);
    }
    // The eye is where clip x, y and w are all zero.
    const std::array<std::array<f64, 3>, 3> eq{{
        {r0[0], r0[1], r0[2]},
        {r1[0], r1[1], r1[2]},
        {r3[0], r3[1], r3[2]},
    }};
    const std::array<f64, 3> rhs{-r0[3], -r1[3], -r3[3]};
    const auto det3 = [](const std::array<std::array<f64, 3>, 3>& q) {
        return q[0][0] * (q[1][1] * q[2][2] - q[1][2] * q[2][1]) -
               q[0][1] * (q[1][0] * q[2][2] - q[1][2] * q[2][0]) +
               q[0][2] * (q[1][0] * q[2][1] - q[1][1] * q[2][0]);
    };
    const f64 det = det3(eq);
    if (std::abs(det) > 1e-12) {
        for (size_t axis = 0; axis < 3; ++axis) {
            auto q = eq;
            for (size_t r = 0; r < 3; ++r) {
                q[r][axis] = rhs[r];
            }
            camera.position[axis] = static_cast<f32>(det3(q) / det);
        }
    }
    return camera;
}

} // namespace Vulkan::CameraFromMatrix

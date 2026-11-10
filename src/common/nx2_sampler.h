// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <string>

namespace Nx2Sampler {

// EDEN_NX2_SAMPLE=<thread names>: a sampling profiler for named host threads (Windows only).
// Names are comma separated; a name ending in '*' matches by prefix (CPUCore_*). From
// EDEN_NX2_SAMPLE_START seconds (default 60) for EDEN_NX2_SAMPLE_SECONDS (default 30), each
// thread is paused about 1000 times a second and its call stack recorded; the counts per
// function (where the thread is, and everything on its stack) go to EDEN_NX2_SAMPLE_OUT
// (default nx2_samples.txt).
void StartFromEnvironment();

/// Samples the named threads for `seconds` now (blocking), writes the report to `out` (and
/// the folded stacks to `out`.folded) and returns the report. With `keep`, only the moments it
/// returns true for are sampled (the debug server's `late_ms`: while a frame is overdue). With
/// `select`, only the samples whose time (steady_clock, ns) it returns true for are reported
/// (`slow_ms`: those inside frames that turned out long).
std::string Sample(const char* names, double seconds, const std::string& out,
                   const std::function<bool()>& keep = {},
                   const std::function<bool(unsigned long long)>& select = {});

} // namespace Nx2Sampler

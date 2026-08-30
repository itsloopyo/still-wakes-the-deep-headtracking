// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// The INI contract for the vertical position limits.
//
// The processor clamps y as [-limit_y_down, +limit_y], and limit_y_down carries
// its own default. An INI written before the LimitYDown key existed carries only
// LimitY, so LimitY has to reach both bounds or the player gets asymmetric travel
// with nothing in the log saying why.

#include <cstdio>
#include <cstdlib>
#include <string>

#include <windows.h>

#include "config.h"

namespace {

int g_failures = 0;

void CheckNear(float actual, float expected, const char* what) {
    if (actual >= expected - 1e-6f && actual <= expected + 1e-6f) return;
    std::printf("FAIL: %s (expected %.6f, got %.6f)\n", what,
                static_cast<double>(expected), static_cast<double>(actual));
    ++g_failures;
}

// IniReader reads through GetPrivateProfileString, which caches the file it last
// read, so every case gets a directory of its own under TEMP.
std::string WriteIni(const char* tag, const char* body) {
    char temp[MAX_PATH] = {};
    GetTempPathA(MAX_PATH, temp);
    const std::string dir = std::string(temp) + "swtd_ht_config_" + tag;
    CreateDirectoryA(dir.c_str(), nullptr);

    const std::string path = dir + "\\HeadTracking.ini";
    FILE* f = nullptr;
    fopen_s(&f, path.c_str(), "w");
    if (f == nullptr) {
        std::printf("FAIL: could not write %s\n", path.c_str());
        ++g_failures;
        return dir;
    }
    std::fputs(body, f);
    std::fclose(f);
    return dir;
}

void LimitYReachesBothBoundsWhenLimitYDownIsAbsent() {
    swtd_ht::Config raised;
    swtd_ht::config_load(WriteIni("wide", "[Position]\nLimitY=0.40\n"), raised);
    CheckNear(raised.limit_y, 0.40f, "LimitY=0.40 raises the upward bound");
    CheckNear(raised.limit_y_down, 0.40f,
              "LimitY=0.40 raises the downward bound too, rather than leaving 0.20");

    swtd_ht::Config tightened;
    swtd_ht::config_load(WriteIni("tight", "[Position]\nLimitY=0.05\n"), tightened);
    CheckNear(tightened.limit_y, 0.05f, "LimitY=0.05 lowers the upward bound");
    CheckNear(tightened.limit_y_down, 0.05f, "LimitY=0.05 lowers the downward bound too");
}

void AnExplicitLimitYDownStillWins() {
    swtd_ht::Config cfg;
    swtd_ht::config_load(WriteIni("both", "[Position]\nLimitY=0.40\nLimitYDown=0.05\n"), cfg);
    CheckNear(cfg.limit_y, 0.40f, "LimitY is read");
    CheckNear(cfg.limit_y_down, 0.05f, "an explicit LimitYDown overrides the mirrored value");
}

}  // namespace

int main() {
    LimitYReachesBothBoundsWhenLimitYDownIsAbsent();
    AnExplicitLimitYDownStillWins();

    if (g_failures != 0) {
        std::printf("%d check(s) failed\n", g_failures);
        return EXIT_FAILURE;
    }
    std::printf("all checks passed\n");
    return EXIT_SUCCESS;
}

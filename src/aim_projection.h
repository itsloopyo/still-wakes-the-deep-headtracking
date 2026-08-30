// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

namespace swtd_ht::AimProjection
{
    // What counts as a believable field of view, in degrees. The bracket is
    // wide enough for every realistic horizontal FOV, so anything outside it is
    // struct drift after a game patch or an uninitialised frame rather than a
    // setting. Both the camera hook, which reads and offsets the engine's live
    // value, and the projector, which consumes it, test against these.
    constexpr float kFovMinDegrees = 10.0f;
    constexpr float kFovMaxDegrees = 170.0f;

    // Push the aim-vs-view relative rotation each render-caller frame so the
    // projector can work out where the clean-aim ray lands in the head-tracked
    // view. qrel = trackedView^-1 * cleanView (unit quaternion x,y,z,w).
    // active=false invalidates the offset entirely (tracking off, no tracker
    // data, or not in gameplay).
    void UpdateAim(double qx, double qy, double qz, double qw, bool active);

    // Push the live FOV (degrees) the engine is rendering with, read from the
    // FMinimalViewInfo the render caller is filling in. This is UE's
    // aspect-independent FOV scalar, treated as the horizontal FOV at the 16:9
    // reference aspect; the projector re-derives Hor+ scaling for the live
    // aspect. Values outside a sane range are ignored so one garbage read
    // cannot throw the reticle across the screen.
    void SetFovDegrees(float fovDegrees);

    // Refresh only the active flag without disturbing the cached qrel, for the
    // non-render GPV callers that the caller gate rejects. The cached qrel from
    // the last render-caller frame stays valid until the next one.
    void SetActive(bool active);

    // Where the clean-aim ray lands, as a pixel offset from the centre of the
    // game window's client area (+x right, +y down). False when there is no
    // valid offset - tracking off, no data, not in gameplay, or the aim is
    // behind the tracked view at an extreme head turn - in which case dx/dy are
    // left untouched and the caller must not move anything.
    bool GetScreenOffset(float& dx, float& dy);
}

// Head shadow, the game side: the hero keeps his head in the first-person pose (so his shadow has one) while the eye
// views cut everything within a few centimeters of the eye. The reasoning and the pure math: steamvr/head_shadow.h.
//   HideHead: the skeleton hook's thread, each hero pose that would shrink the head (vr/skeletons.cpp PoseBody).
//   EyeNear: the game's render thread, each eye projection in first person (vr/vrcam.cpp VrProjInner).
// Reads no game memory and calls nothing in the game.
// Log "Frame head shadow:": each change of state (at most one a second, the changes in between counted), and every
// 10 s while the head is kept: poses kept or hidden, eye passes at the cut, the near before and after, the head pitch
// and the cut's range, how far the head bones reach (a WARNING when the farthest is within 5 cm of the smallest cut).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include "../steamvr/head_shadow.h"
#include "logging.h"

namespace {
namespace hs = framevr::headshadow;

constexpr uint64_t SummaryMs = 10000;   // the summary line's period

std::atomic<uint64_t> g_cutAt{0};    // ms the eye's near clip last stood at the cut (0: never)
std::atomic<uint64_t> g_keptAt{0};   // ms a pose last kept the head (0: never)
std::atomic<float> g_heldNear{0};    // the near (world units) of the last cut, held for the tail

// The summary's counts: the skeleton thread's poses and the render thread's projections (g_lock, short holds).
SRWLOCK g_lock = SRWLOCK_INIT;
struct Window {
    int poses = 0, kept = 0, hidden = 0;
    hs::Why lastHidden = hs::Why::Off;
    hs::Reach reach;   // the farthest seen this window
    int passes = 0, cutPasses = 0, heldPasses = 0;
    float nearFrom = 0, nearTo = 0, unitsPerMetre = 0, pitchDeg = 0, cutMin = 0, cutMax = 0;
};
Window g_window;
uint64_t g_nextSummary = 0;
// The state line: told on a change, at most one a second, the changes in between counted.
hs::Why g_told = hs::Why::NotHidden;
uint64_t g_tellAt = 0;
int g_flaps = 0;
}  // namespace

namespace headshadow {
bool Holding() { return hs::Live(g_keptAt.load(std::memory_order_acquire), GetTickCount64()); }

bool HideHead(bool form, const float* bones, const char* inNeck, int count, const float* eye, float unitsPerCm) {
    const uint64_t now = GetTickCount64();
    const hs::Why w = hs::Decide(true, true, form, hs::Live(g_cutAt.load(std::memory_order_acquire), now));
    if (w == hs::Why::Kept) g_keptAt.store(now, std::memory_order_release);   // before the pose is written: the eye's hold starts now
    char line[200];
    bool tell = false;
    AcquireSRWLockExclusive(&g_lock);
    if (w != g_told) {
        if (now < g_tellAt) ++g_flaps;
        else {
            std::snprintf(line, sizeof line, "Frame head shadow: %s (cut %.0f cm level, %.0f up, %.0f down; %d more changes in the last second)",
                          hs::WhyName(w), hs::LevelCm, hs::UpCm, hs::DownCm, g_flaps);
            g_told = w; g_tellAt = now + 1000; g_flaps = 0; tell = true;
        }
    }
    ++g_window.poses;
    if (w == hs::Why::Kept) ++g_window.kept; else { ++g_window.hidden; g_window.lastHidden = w; }
    const bool measure = now + 1000 >= g_nextSummary;   // the reach only in the summary's last second: a loop over the bones
    ReleaseSRWLockExclusive(&g_lock);
    if (measure) {
        const hs::Reach r = hs::HeadReach(bones, inNeck, count, eye, unitsPerCm);
        AcquireSRWLockExclusive(&g_lock);
        if (r.bones && (!g_window.reach.bones || r.farCm > g_window.reach.farCm)) g_window.reach = r;
        ReleaseSRWLockExclusive(&g_lock);
    }
    if (tell) framevr::logging::Write(line);   // after the lock: Write takes its own
    return w != hs::Why::Kept;
}

float EyeNear(float nearZ, bool on, float unitsPerMetre, float pitchDeg) {
    const uint64_t now = GetTickCount64();
    const float cut = hs::PitchCut(pitchDeg);
    const float from = nearZ;
    bool atCut = false, held = false;
    if (on && unitsPerMetre > 0 && std::isfinite(unitsPerMetre)) {
        nearZ = hs::RaisedNear(nearZ, cut, unitsPerMetre);   // at least the cut ([camera] near may already be past it)
        atCut = true;
        g_heldNear.store(nearZ, std::memory_order_relaxed);
        g_cutAt.store(now, std::memory_order_release);
    }
    // The hold: a pose kept the head within CutLiveMs (the setting just went off, or the scale is not known this pass):
    // keep the last cut until the shrunk head is back.
    if (hs::Live(g_keptAt.load(std::memory_order_acquire), now)) {
        const float last = g_heldNear.load(std::memory_order_relaxed);
        if (last > nearZ) { nearZ = last; held = true; }
    }
    Window s{};
    bool summary = false;
    AcquireSRWLockExclusive(&g_lock);
    if (!g_nextSummary) g_nextSummary = now + SummaryMs;
    else if (now >= g_nextSummary) {   // a window left open by third person or a menu (no passes) is dropped, not told as 10 s
        s = g_window; g_window = Window{};
        summary = s.kept > 0 && now - g_nextSummary < SummaryMs;
        g_nextSummary = now + SummaryMs;
    }
    Window& w = g_window;
    ++w.passes;
    if (atCut) ++w.cutPasses;
    if (held) ++w.heldPasses;
    w.nearFrom = from; w.nearTo = nearZ; w.unitsPerMetre = unitsPerMetre; w.pitchDeg = pitchDeg;
    w.cutMin = w.passes == 1 ? cut : std::min(w.cutMin, cut);
    w.cutMax = w.passes == 1 ? cut : std::max(w.cutMax, cut);
    ReleaseSRWLockExclusive(&g_lock);
    if (!summary) return nearZ;
    char reach[160];
    if (s.reach.bones)
        std::snprintf(reach, sizeof reach, "head bones %d reach %.0f..%.0f cm from the eye%s", s.reach.bones, s.reach.nearCm, s.reach.farCm,
                      s.reach.farCm + 5.f > s.cutMin ? " (WARNING: the farthest is within 5 cm of the cut: the head may show)" : "");
    else std::snprintf(reach, sizeof reach, "head bones' reach not known (no eye point)");
    char pitch[40];
    if (std::isfinite(s.pitchDeg)) std::snprintf(pitch, sizeof pitch, "%+.0f deg", s.pitchDeg);
    else std::snprintf(pitch, sizeof pitch, "not known (level cut)");
    char text[600];
    std::snprintf(text, sizeof text,
                  "Frame head shadow: 10 s: poses %d (head kept %d, hidden %d%s%s); eye passes %d, at the cut %d, held %d; near %.3f -> %.3f world units "
                  "at %.2f units/m; pitch %s, cut %.1f..%.1f cm; %s",
                  s.poses, s.kept, s.hidden, s.hidden ? ": " : "", s.hidden ? hs::WhyName(s.lastHidden) : "", s.passes, s.cutPasses, s.heldPasses,
                  s.nearFrom, s.nearTo, s.unitsPerMetre, pitch, s.cutMin, s.cutMax, reach);
    framevr::logging::Write(text);
    return nearZ;
}
}  // namespace headshadow

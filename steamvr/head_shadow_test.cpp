// Head shadow (steamvr/head_shadow.h): when the head is kept, the eye's near clip, the cut by head pitch, the hold, the
// head bones' reach.
#include <cmath>
#include <cstdio>
#include <limits>
#include "head_shadow.h"

namespace hs = framevr::headshadow;
static int g_fail = 0;
static void Check(bool ok, const char* what) { if (!ok) { std::printf("FAIL: %s\n", what); ++g_fail; } }
static bool Near(float a, float b, float tol = 1e-4f) { return std::fabs(a - b) <= tol; }

int main() {
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    {   // Decide: the head is kept only when every condition holds; anything else shrinks it as before
        Check(hs::Decide(true, true, false, true) == hs::Why::Kept, "hidden, on, no form, cut live: kept");
        Check(hs::Decide(false, true, false, true) == hs::Why::NotHidden, "the head is shown (third person, F1, a flat menu): untouched");
        Check(hs::Decide(true, false, false, true) == hs::Why::Off, "[arms] head_shadow=0: shrunk as before");
        Check(hs::Decide(true, true, true, true) == hs::Why::Form, "a shapeshift form: shrunk as before");
        Check(hs::Decide(true, true, false, false) == hs::Why::NoCut, "the eye's cut not live: shrunk (fail-safe)");
        for (int w = 0; w <= int(hs::Why::NoCut); ++w) Check(hs::WhyName(hs::Why(w))[0] != '?', "every reason has a name");
    }
    {   // Live: within CutLiveMs of a stamp; never before the first; a stamp a little ahead (another thread) counts
        Check(!hs::Live(0, 1000), "never stamped: not live");
        Check(hs::Live(1000, 1000) && hs::Live(1000, 1000 + hs::CutLiveMs), "the same ms, and exactly CutLiveMs later: live");
        Check(!hs::Live(1000, 1001 + hs::CutLiveMs), "past CutLiveMs: not live");
        Check(hs::Live(1016, 1000), "stamped by the other thread after this one read the clock: live, not a wrapped gap");
        Check(hs::CutLiveMs >= 100 && hs::CutLiveMs <= 1000, "the hold spans several frames and stays short");
    }
    {   // The tuned values: level, up and down, and the span
        Check(hs::LevelCm == 11 && hs::UpCm == 13 && hs::DownCm == 3, "the headset-tuned cuts: 11 cm level, 13 up, 3 down");
        Check(hs::PitchSpanDeg > 0 && hs::PitchSpanDeg < 90, "the span is a real head pitch, short of straight up");
        Check(hs::PitchCut(0) == hs::LevelCm && hs::PitchCut(90) == hs::UpCm && hs::PitchCut(-90) == hs::DownCm, "the defaults: level, straight up, straight down");
    }
    {   // RaisedNear: never nearer than the configured near, the cut at the player's scale, bad input keeps the near
        Check(Near(hs::RaisedNear(.2f, 25, 4.f), 1.f), "25 cm at 4 units/m: 1 unit");
        Check(Near(hs::RaisedNear(.2f, 11, 4.78f), .5258f, 1e-3f), "11 cm at 4.78 units/m (a headset log): 0.526 units");
        Check(Near(hs::RaisedNear(1.5f, 25, 4.f), 1.5f), "[camera] near already past the cut: kept");
        Check(Near(hs::RaisedNear(.2f, 0, 4.f), .2f), "no cut: the near as configured");
        Check(Near(hs::RaisedNear(.2f, 25, 0), .2f), "no scale: the near as configured");
        Check(Near(hs::RaisedNear(.2f, 25, -3), .2f), "a negative scale: the near as configured");
        Check(Near(hs::RaisedNear(.2f, 25, inf), .2f), "an infinite scale: the near as configured");
        Check(Near(hs::RaisedNear(.2f, 25, nan), .2f), "a NaN scale: the near as configured");
        Check(Near(hs::RaisedNear(.2f, nan, 4.f), .2f), "a NaN cut: the near as configured");
    }
    {   // HeadReach: only inNeck bones, distance from the eye in cm
        float bones[4 * 16] = {};
        auto at = [&](int i, float x, float y, float z) { bones[i * 16 + 12] = x; bones[i * 16 + 13] = y; bones[i * 16 + 14] = z; bones[i * 16 + 15] = 1; };
        at(0, 0, 0, 0);          // the pelvis: not in the neck
        at(1, 0, 1.6f, 0);       // the neck: 10 cm below the eye
        at(2, 0, 1.75f, .03f);   // the head: 5 cm above, 3 back
        at(3, 0, 1.9f, 0);       // a helm's bone: 20 cm above
        const char inNeck[4] = {0, 1, 1, 1};
        const float eye[3] = {0, 1.7f, 0};
        const hs::Reach r = hs::HeadReach(bones, inNeck, 4, eye, .01f);   // 1 model unit = 1 m
        Check(r.bones == 3, "three bones in the neck");
        Check(Near(r.nearCm, std::sqrt(25.f + 9.f), 1e-2f) && Near(r.farCm, 20, 1e-2f), "nearest the head bone, farthest the helm's");
        Check(hs::HeadReach(bones, inNeck, 4, nullptr, .01f).bones == 0, "no eye point: nothing measured");
        Check(hs::HeadReach(bones, inNeck, 4, eye, 0).bones == 0, "no scale: nothing measured");
        Check(hs::HeadReach(nullptr, inNeck, 4, eye, .01f).bones == 0 && hs::HeadReach(bones, nullptr, 4, eye, .01f).bones == 0, "no bones: nothing");
        const char none[4] = {};
        Check(hs::HeadReach(bones, none, 4, eye, .01f).bones == 0, "no neck bones: nothing");
    }
    {   // PitchCut: level is the level cut exactly, easing to the up / down cut by PitchSpanDeg, flat past it
        const float span = hs::PitchSpanDeg;
        Check(hs::PitchCut(0, 12, 30, 6) == 12 && hs::PitchCut(0, 25, 50, 0) == 25 && hs::PitchCut(-0.f, 12, 30, 6) == 12, "level (and -0): the level cut");
        for (float p = -90; p <= 90; p += 0.5f) Check(hs::PitchCut(p, 12, 12, 12) == 12, "equal cuts: the same at every pitch, bit for bit");
        Check(hs::PitchCut(span, 12, 30, 6) == 30 && hs::PitchCut(-span, 12, 30, 6) == 6, "at the span: the up / down cut exactly");
        Check(hs::PitchCut(89, 12, 30, 6) == 30 && hs::PitchCut(-89, 12, 30, 6) == 6 && hs::PitchCut(1000, 12, 30, 6) == 30, "past it: clamped at the end value");
        Check(Near(hs::PitchCut(span / 2, 12, 30, 6), 21) && Near(hs::PitchCut(-span / 2, 12, 30, 6), 9), "half way (smoothstep 0.5): the midpoint");
        Check(Near(hs::PitchCut(span / 4, 10, 20, 10), 10 + 10 * .15625f) && Near(hs::PitchCut(span * .75f, 10, 20, 10), 10 + 10 * .84375f),
              "a quarter and three quarters: smoothstep 0.156 and 0.844");
        Check(Near(hs::PitchWeight(span / 2), .5f) && hs::PitchWeight(0) == 0 && hs::PitchWeight(span) == 1 && hs::PitchWeight(-span * 2) == 1, "the weight: 0, 0.5, 1, clamped");
        Check(hs::PitchCut(1, 12, 30, 6) - 12 < .05f && hs::PitchCut(1, 12, 30, 6) > 12, "flat at level: a degree up barely moves it");
        Check(hs::PitchCut(nan) == hs::LevelCm && hs::PitchCut(inf) == hs::LevelCm && hs::PitchCut(-inf) == hs::LevelCm, "a pitch not known: the level cut");
        Check(hs::PitchCut(30, 12, 30, 6, nan) == 12 && hs::PitchCut(30, 12, 30, 6, 0) == 12 && hs::PitchCut(30, 12, 30, 6, -5) == 12, "a bad span: the level cut");
        Check(hs::PitchCut(40, 0, 30, 6) == 0 && hs::PitchCut(40, -3, 30, 6) == 0 && hs::PitchCut(40, nan, 30, 6) == 0, "no level cut: none at any pitch");
        Check(hs::PitchCut(span, 12, nan, 6) == 0 && hs::PitchCut(span, 12, -4, 6) == 0 && hs::PitchCut(-span, 12, 30, inf) == 0, "a bad end counts as none");
        bool upRises = true, downFalls = true;
        float prevUp = hs::PitchCut(0), prevDown = prevUp;
        for (float p = .25f; p <= 90; p += .25f) {
            const float u = hs::PitchCut(p), d = hs::PitchCut(-p);
            upRises = upRises && u >= prevUp; downFalls = downFalls && d <= prevDown;
            prevUp = u; prevDown = d;
            Check(u >= hs::LevelCm && u <= hs::UpCm && d <= hs::LevelCm && d >= hs::DownCm, "always between the level and the end");
        }
        Check(upRises && downFalls, "monotonic from level to each end");
    }
    if (g_fail) { std::printf("head shadow: %d failed\n", g_fail); return 1; }
    std::puts("head shadow: ok");
    return 0;
}

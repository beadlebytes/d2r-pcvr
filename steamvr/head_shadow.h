#pragma once
// Head shadow: in first person the hero keeps his whole head (and helm) in the pose, so his shadow has one, and the eye
// views draw nothing within a few centimeters of the eye instead, so the inside of the head is never seen.
//
// Before, the head and neck shrank into a point at the neck (vr/skeletons.cpp PoseBody, skel::Input::hideHead) so a
// camera at the eyes saw out instead of the inside of the skull. The shadow pass draws the same bones, so the shadow was
// headless too. Now [arms] hide_head keeps the head in the pose and raises the eye projection's near clip (vrcam
// VrProjInner, [camera] near) to the cut below. A near plane at distance d drops every point within d of the eye (a
// point's depth along the view is never more than its distance), and the shadow map is drawn from the light, not through
// the eye's frustum, so the shadow keeps the head. Every depth reader takes the same near (the depth fog's NearPlane, the
// headset's projection and depth, DLSS). [arms] head_shadow=0 shrinks the head as before.
//
// The cut follows head pitch: level, it is LevelCm; it eases (smoothstep) toward UpCm looking up and DownCm looking down
// and reaches them at PitchSpanDeg. Looking up, the head's back and a helm sit nearer the eye's view; looking down, a
// small cut keeps the top of the chest. Values tuned in the headset (a Paladin, helm on and off).
//
// Fail-safe order: a pose keeps the head only while the eye's cut is live (raised within CutLiveMs), and the eye keeps
// the cut for CutLiveMs after the last pose that kept the head, so a pose with the head never meets an eye view without
// the cut, whichever thread runs first. A shapeshift form keeps the old shrink (its snout reaches past any cut).
//
// This header is pure (no game, no Windows): steamvr/head_shadow_test.cpp. The game side is vr/head_shadow.cpp.
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace framevr::headshadow {

constexpr float LevelCm = 11.f;        // the cut looking level, cm at the player's scale
constexpr float UpCm = 13.f;           // looking up (the end value, reached at PitchSpanDeg)
constexpr float DownCm = 3.f;          // looking down
constexpr float PitchSpanDeg = 60.f;   // about as far as a head turns up or down from level
constexpr uint64_t CutLiveMs = 250;    // the cut counts as live this long after it was last raised, and is held this long

// at: the ms something last happened (0: never); now: this call's ms. True within CutLiveMs of it. A stamp a little
// ahead of now (another thread read the clock later) counts as live.
inline bool Live(uint64_t at, uint64_t now) { return at != 0 && (now <= at || now - at <= CutLiveMs); }

// Why this pose kept or hid the head (the log names it).
enum class Why : int { Kept, NotHidden, Off, Form, NoCut };
inline const char* WhyName(Why w) {
    switch (w) {
    case Why::Kept: return "head kept for its shadow";
    case Why::NotHidden: return "the head is shown here (not first person)";
    case Why::Off: return "head hidden: [arms] head_shadow is 0";
    case Why::Form: return "head hidden: a shapeshift form's rig";
    case Why::NoCut: return "head hidden: the eye's cut is not live yet";
    }
    return "?";
}

// hide: this pose would shrink the head (first person, [arms] hide_head or no full body). on: [arms] head_shadow.
// form: a shapeshift form's rig. cutLive: the eye's near clip stood at the cut within CutLiveMs.
inline Why Decide(bool hide, bool on, bool form, bool cutLive) {
    if (!hide) return Why::NotHidden;
    if (!on) return Why::Off;
    if (form) return Why::Form;
    if (!cutLive) return Why::NoCut;
    return Why::Kept;
}

// How far along toward the end value, 0 (level) to 1 (at or past spanDeg): smoothstep of |pitch| / span.
// A pitch that is not known (NaN, infinite) or a span that is not positive: 0, the level cut.
inline float PitchWeight(float pitchDeg, float spanDeg = PitchSpanDeg) {
    if (!std::isfinite(pitchDeg) || !(spanDeg > 0) || !std::isfinite(spanDeg)) return 0;
    const float t = std::fabs(pitchDeg) / spanDeg;
    if (t >= 1) return 1;
    return t * t * (3 - 2 * t);
}
// The cut in cm at this head pitch (pitchDeg: up + / down -, degrees from the horizon; NaN: not known, the level cut).
// Level is levelCm exactly, easing toward upCm or downCm. A NaN, infinite or negative cut counts as 0 (none).
inline float PitchCut(float pitchDeg, float levelCm = LevelCm, float upCm = UpCm, float downCm = DownCm, float spanDeg = PitchSpanDeg) {
    auto clean = [](float cm) { return std::isfinite(cm) && cm > 0 ? cm : 0.f; };
    const float level = clean(levelCm);
    if (!(level > 0)) return 0;
    const float w = PitchWeight(pitchDeg, spanDeg);
    if (!(w > 0)) return level;
    const float end = clean(pitchDeg > 0 ? upCm : downCm);
    return w >= 1 ? end : level + (end - level) * w;
}

// The eye's near clip in world units: never nearer than the configured one, at least cutCm at the player's scale.
// unitsPerMetre: world units per metre of the player (vrcam ScaleHeight / UserEyeHeightM). 0 or a bad scale: unchanged.
inline float RaisedNear(float nearZ, float cutCm, float unitsPerMetre) {
    if (!(cutCm > 0) || !(unitsPerMetre > 0) || !std::isfinite(unitsPerMetre)) return nearZ;
    const float cut = cutCm * .01f * unitsPerMetre;
    return std::isfinite(cut) && cut > nearZ ? cut : nearZ;
}

// How far the head's bones reach from the eye, for the log: the nearest and farthest bone flagged in inNeck (the neck
// and everything under it), in cm. Joints, not the mesh: the skin and a helm reach a few cm past the farthest joint.
struct Reach { int bones = 0; float nearCm = 0, farCm = 0; };
// bones: count matrices of 16 floats (row vectors: the translation at [12..14]); eye: model space; unitsPerCm > 0.
inline Reach HeadReach(const float* bones, const char* inNeck, int count, const float* eye, float unitsPerCm) {
    Reach r;
    if (!bones || !inNeck || !eye || count <= 0 || !(unitsPerCm > 0)) return r;
    for (int i = 0; i < count; ++i) {
        if (!inNeck[i]) continue;
        const float* m = bones + size_t(i) * 16;
        const float dx = m[12] - eye[0], dy = m[13] - eye[1], dz = m[14] - eye[2];
        const float cm = std::sqrt(dx * dx + dy * dy + dz * dz) / unitsPerCm;
        if (!std::isfinite(cm)) continue;
        if (!r.bones || cm < r.nearCm) r.nearCm = cm;
        if (!r.bones || cm > r.farCm) r.farCm = cm;
        ++r.bones;
    }
    return r;
}

}  // namespace framevr::headshadow

// The game side (vr/head_shadow.cpp). Not used by the pure test.
namespace headshadow {
// PoseBody (the skeleton hook, the hero's pose), just before the head shrink, only when it would shrink the head and
// [arms] head_shadow is on. form: a shapeshift form's rig. bones .. unitsPerCm: the pose, for the log's reach only.
// Returns false when the head stays whole this pose (the shrink is skipped), true to shrink it as before.
bool HideHead(bool form, const float* bones, const char* inNeck, int count, const float* eye, float unitsPerCm);
// VrProjInner (each eye projection, first person only), when on or Holding(). on: the head may be kept ([arms]
// head_shadow and the head would be hidden). Returns the near clip to draw with: raised to the cut at this pitch while
// on, and held at the last cut while a pose kept the head within CutLiveMs.
float EyeNear(float nearZ, bool on, float unitsPerMetre, float pitchDeg);
// A pose kept the head within CutLiveMs: the eye must keep the cut even with the setting just turned off.
bool Holding();
}  // namespace headshadow

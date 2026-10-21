#pragma once
#include <cmath>

namespace framevr::sticktap {
// Right stick double tap up: two up flicks close together, the stick back
// near center between them. The stick's x is the turn, so a flick leaning
// sideways (a snap or smooth turn) never counts, and an up flick stays well
// inside the turn's own .7 threshold. Each tap needs a fresh return to center.
constexpr float Up=.7f,Center=.3f,Sideways=.4f;
constexpr double Window=.4; // first up to second up, in seconds
class DoubleTap {
    enum class Phase {Rest,First,Between,Wait} phase=Phase::Wait; // Wait: until the stick is centered
    double firstAt=0;
public:
    void Reset(){phase=Phase::Wait;}
    bool Step(double time,float x,float y,bool allowed) {
        if(!allowed||!std::isfinite(time)||!std::isfinite(x)||!std::isfinite(y)){Reset();return false;}
        if(std::fabs(x)>=Sideways){phase=Phase::Wait;return false;}
        const bool centered=y<Center,up=y>Up;
        if((phase==Phase::First||phase==Phase::Between)&&time-firstAt>Window)phase=centered?Phase::Rest:Phase::Wait;
        switch(phase) {
            case Phase::Wait:if(centered)phase=Phase::Rest;break;
            case Phase::Rest:if(up){phase=Phase::First;firstAt=time;}break;
            case Phase::First:if(centered)phase=Phase::Between;break;
            case Phase::Between:if(up){phase=Phase::Wait;return true;}break;
        }
        return false;
    }
};
}

#pragma once
#include <cstdint>

// 2026-10-19: a throwing potion is thrown by the game's own Throw skill. With
// no controller slot holding Throw, one slot lends it for a single throw: its
// skill is bound to Throw, pressed once, and the player's own skill is bound
// back. Pure state; the transport issues the native binding requests and
// reads the bindings back from the fresh skill snapshot.
namespace framevr::potionthrow {
inline constexpr int Throw=2;
enum class Do {Nothing,Lend,Press,Return,Restored,GiveUp};
class Loan {
public:
    enum class Step {Idle,Lending,Pressing,Returning};
    // Milliseconds: binding round trip, press until the command is built, and
    // the gap before rebinding so a held press cannot repeat the old skill.
    static constexpr uint64_t LendWait=600,PressWait=600,ReturnAfter=200,Retry=300;
    Step step=Step::Idle;
    int slot=-1,skill=-1,owner=-1; // the slot and the player's own skill in it
    uint64_t session=0,at=0,asked=0;
    unsigned tries=0;
    bool Busy() const {return step!=Step::Idle;}
    // The slot holds skill/owner now. Returns Do::Lend (bind Throw) or Nothing.
    Do Start(int nextSlot,int heldSkill,int heldOwner,uint64_t nextSession,uint64_t now) {
        if(Busy()||nextSlot<0||!nextSession||heldSkill==Throw)return Do::Nothing;
        step=Step::Lending;slot=nextSlot;skill=heldSkill;owner=heldOwner;session=nextSession;at=asked=now;tries=1;
        return Do::Lend;
    }
    // fresh: the snapshot below belongs to this session and is current.
    // held: what the slot holds now. abort: menu, cancel, weapon swap, death,
    // inactive controls. gone: the character session ended. thrown: the
    // game built the Throw command.
    Do Update(uint64_t now,bool fresh,int heldSkill,int heldOwner,bool abort,bool gone,bool thrown) {
        if(!Busy())return Do::Nothing;
        if(gone){*this=Loan{};return Do::GiveUp;} // nothing left to rebind in
        const bool own=fresh&&heldSkill==skill&&heldOwner==owner;
        switch(step) {
            case Step::Lending:
                if(fresh&&heldSkill==Throw&&!abort){step=Step::Pressing;at=now;return Do::Press;}
                if(abort||now-at>=LendWait) {
                    if(own){*this=Loan{};return Do::Restored;} // never lent
                    return Back(now);
                }
                return Do::Nothing;
            case Step::Pressing:
                if(abort||(thrown&&now-at>=ReturnAfter)||now-at>=PressWait)return Back(now);
                return Do::Nothing;
            case Step::Returning:
                if(own){*this=Loan{};return Do::Restored;}
                if(now-asked>=Retry){asked=now;++tries;return Do::Return;}
                return Do::Nothing;
            default:return Do::Nothing;
        }
    }
private:
    Do Back(uint64_t now){step=Step::Returning;asked=now;tries=1;return Do::Return;}
};
}

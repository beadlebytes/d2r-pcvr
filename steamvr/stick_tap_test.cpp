#include "stick_tap.h"
#include <cstdio>
#include <cstdlib>
using namespace framevr;
static void Check(bool b,const char* label){if(!b){std::fprintf(stderr,"FAIL: %s\n",label);std::exit(1);}}
struct Sim {
    sticktap::DoubleTap tap;double time=1;bool allowed=true;int fired=0;
    // Hold (x,y) for n frames of 10 ms; counts toggles.
    Sim& Hold(float x,float y,int n=1){while(n--){time+=.01;fired+=tap.Step(time,x,y,allowed);}return *this;}
    Sim& Flick(float x=0,float y=1){return Hold(x,y,5).Hold(0,0,5);}
};
int main(){
    {Sim s;s.Hold(0,0,5).Flick();Check(!s.fired,"one up flick does not toggle");
     s.Flick();Check(s.fired==1,"two quick up flicks toggle once");
     s.Hold(0,0,50);Check(s.fired==1,"resting after the double tap does not repeat");}
    {Sim s;s.Hold(0,0,5).Hold(0,1,5).Hold(0,0,5).Hold(0,1,200);Check(s.fired==1,"holding the second flick toggles once");
     s.Hold(0,1,5);Check(s.fired==1,"a held stick never repeats");}
    {Sim s;s.Hold(0,1,5);s.Hold(0,0,5).Flick();Check(!s.fired,"a stick already up when allowed is not a first tap");
     s.Flick();Check(s.fired==1,"after centering, a fresh pair toggles");}
    {Sim s;s.Hold(0,0,5).Flick().Hold(0,0,40).Flick();Check(!s.fired,"flicks further apart than the window do not toggle");
     s.Flick();Check(s.fired==1,"the late flick starts a new pair");}
    {Sim s;s.Hold(0,0,5).Hold(0,1,50).Hold(0,0,2).Flick();Check(!s.fired,"a long push up is not a tap");}
    {Sim s;s.Hold(0,0,5).Hold(0,1,5).Hold(0,.5f,5).Hold(0,1,5);Check(!s.fired,"the stick must return near center between taps");}
    {Sim s;s.Hold(0,0,5).Flick(.75f,.75f).Flick(.75f,.75f);Check(!s.fired,"diagonal snap-turn flicks do not toggle");
     s.Hold(0,0,5).Flick(.3f,.9f).Flick(-.3f,.9f);Check(s.fired==1,"a slightly leaning up flick still counts");}
    {Sim s;s.Hold(0,0,5).Flick().Flick(.9f,0);s.Flick();Check(!s.fired,"a turn between the flicks cancels the pair");}
    {Sim s;s.Hold(0,0,5).Hold(0,1,5).Hold(0,0,5).Hold(.5f,.9f,5);Check(!s.fired,"a second flick leaning into a turn does not toggle");}
    {Sim s;s.Hold(0,0,5).Flick();s.allowed=false;s.Hold(0,0,3);s.allowed=true;s.Flick();Check(!s.fired,"a gate between flicks cancels the pair");
     s.Flick().Flick();Check(s.fired==1,"the gate lifting allows a fresh pair");}
    {Sim s;s.allowed=false;s.Hold(0,0,5).Flick().Flick();Check(!s.fired,"never toggles while gated");}
    {Sim s;s.Hold(0,0,5).Flick();s.Hold(0,0,1);s.time=NAN;s.Hold(0,1,1);s.time=2;s.Hold(0,0,1).Hold(0,1,1);
     Check(!s.fired,"a bad sample cancels the pair");}
    {Sim s;s.Hold(0,0,5).Flick(0,-1).Flick(0,-1);Check(!s.fired,"down flicks do not toggle");}
    std::puts("stick tap tests passed");
}

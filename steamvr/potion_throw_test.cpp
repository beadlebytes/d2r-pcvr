#include "potion_throw.h"
#include <cstdio>
#include <cstdlib>
using namespace framevr::potionthrow;
static void Check(bool ok,const char* what){if(!ok){std::fprintf(stderr,"FAIL: %s\n",what);std::exit(1);}}
int main() {
    {
        Loan loan;
        Check(loan.Start(0,0,-1,7,1000)==Do::Lend&&loan.Busy(),"a slot without Throw lends it");
        Check(loan.Start(0,0,-1,7,1001)==Do::Nothing,"one loan at a time");
        Check(loan.Update(1100,true,0,-1,false,false,false)==Do::Nothing,"waits for the binding");
        Check(loan.Update(1200,true,Throw,-1,false,false,false)==Do::Press,"presses once Throw is bound");
        Check(loan.Update(1300,true,Throw,-1,false,false,true)==Do::Nothing,"keeps Throw while the press may still be held");
        Check(loan.Update(1400,true,Throw,-1,false,false,true)==Do::Return,"binds the player's skill back after the throw");
        Check(loan.Update(1500,true,Throw,-1,false,false,false)==Do::Nothing,"waits for the rebind");
        Check(loan.Update(1700,true,Throw,-1,false,false,false)==Do::Return,"retries a rebind that did not land");
        Check(loan.Update(1800,true,0,-1,false,false,false)==Do::Restored&&!loan.Busy(),"done once the slot holds its own skill again");
    }
    {
        Loan loan;loan.Start(0,-1,-1,7,1000);
        Check(loan.Update(1600,true,-1,-1,false,false,false)==Do::Restored&&!loan.Busy(),"a refused loan changes nothing and ends");
    }
    for(int stage=0;stage<2;++stage) {
        Loan loan;loan.Start(0,251,-1,7,1000);
        if(stage)Check(loan.Update(1100,true,Throw,-1,false,false,false)==Do::Press,"lent");
        Check(loan.Update(1150,true,Throw,-1,true,false,false)==Do::Return,"menu, cancel, swap or death rebinds at once");
        Check(loan.Update(1200,false,-1,-1,false,false,false)==Do::Nothing&&loan.Busy(),"an unreadable snapshot is never taken as restored");
        Check(loan.Update(2000,true,251,-1,false,false,false)==Do::Restored,"the player's skill comes back, owner included");
    }
    {
        Loan loan;loan.Start(0,0,-1,7,1000);loan.Update(1100,true,Throw,-1,false,false,false);
        Check(loan.Update(1700,true,Throw,-1,false,false,false)==Do::Return,"a press the game never used still rebinds");
        for(uint64_t t=2000;t<20000;t+=300)Check(loan.Update(t,true,Throw,-1,true,false,false)!=Do::Restored,"keeps retrying while Throw is still bound");
        Check(loan.Busy(),"never gives up on a live character");
        Check(loan.Update(20000,false,-1,-1,false,true,false)==Do::GiveUp&&!loan.Busy(),"ends when the character is gone");
    }
    {
        Loan loan;loan.Start(0,0,-1,7,1000);
        Check(loan.Update(1100,true,0,5,false,false,false)==Do::Nothing,"another owner is not the player's skill");
        Check(loan.Start(1,Throw,-1,7,1000)==Do::Nothing&&Loan{}.Start(-1,0,-1,7,0)==Do::Nothing,"no loan for a slot already holding Throw or no slot");
    }
    std::puts("Potion throw: Throw loan, single press, rebind, retries and abort tests passed");
}

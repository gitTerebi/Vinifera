#include "always.h"

#include "sharedcontrol.h"

#include "debughandler.h"
#include "hooker.h"
#include "house.h"
#include "session.h"
#include "syringe.h"
#include "techno.h"
#include "tibsun_globals.h"

#include <algorithm>


static bool SharedControlHouses[32];


bool Vinifera_Is_Shared_Control_House(const HouseClass* house)
{
    if (house == nullptr) {
        return false;
    }

    const int id = house->HeapID;
    if (id < 0 || id >= static_cast<int>(std::size(SharedControlHouses))) {
        return false;
    }

    return SharedControlHouses[id];
}


bool Vinifera_Can_Locally_Control(const TechnoClass* techno)
{
    if (techno == nullptr) {
        return false;
    }

    return Vinifera_Is_Shared_Control_House(techno->House);
}


DEFINE_HOOK(0x004CB97E, _HouseClass_Is_Player_Control_Multiplayer, 5)
{
    GET(HouseClass*, this_ptr, ECX);

    if (this_ptr == PlayerPtr || Vinifera_Is_Shared_Control_House(this_ptr)) {
        R->EAX(1);
    } else {
        R->EAX(0);
    }

    return 0x004CB983;
}


void Vinifera_Init_Shared_Control()
{
    std::fill(std::begin(SharedControlHouses), std::end(SharedControlHouses), false);

    if (PlayerPtr == nullptr) {
        return;
    }

    for (int i = 0; i < Houses.Count(); i++) {
        HouseClass* house = Houses[i];

        if (house == PlayerPtr || house->IsHuman) {
            continue;
        }

        if (!PlayerPtr->Is_Ally(house) || !house->Is_Ally(PlayerPtr)) {
            continue;
        }

        const int id = house->HeapID;
        if (id < 0 || id >= static_cast<int>(std::size(SharedControlHouses))) {
            continue;
        }

        SharedControlHouses[id] = true;

        DEBUG_INFO("SharedControl: house {} \"{}\" is now commandable by the player.\n",
            id, house->IniName.c_str());
    }
}


bool Vinifera_Event_Sender_Controls(int sender_id, const TechnoClass* techno)
{
    if (techno == nullptr || techno->House == nullptr) {
        return false;
    }

    HouseClass* owner = techno->House;

    if (owner->IsHuman) {
        return false;
    }

    if (sender_id < 0 || sender_id >= Houses.Count()) {
        return false;
    }

    HouseClass* sender = Houses[sender_id];
    if (sender == nullptr || !sender->IsHuman) {
        return false;
    }

    return sender->Is_Ally(owner) && owner->Is_Ally(sender);
}

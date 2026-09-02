#include "always.h"

#include "sharedcontrol.h"

#include "house.h"
#include "session.h"
#include "techno.h"
#include "tibsun_globals.h"


bool Vinifera_Is_Shared_Control_House(const HouseClass* house)
{
    if (house == nullptr || PlayerPtr == nullptr) {
        return false;
    }

    if (house == PlayerPtr) {
        return false;
    }

    if (house->IsHuman) {
        return false;
    }

    return PlayerPtr->Is_Ally(house) && house->Is_Ally(PlayerPtr);
}


bool Vinifera_Can_Locally_Control(const TechnoClass* techno)
{
    if (techno == nullptr) {
        return false;
    }

    return Vinifera_Is_Shared_Control_House(techno->House);
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

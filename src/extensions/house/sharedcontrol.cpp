#include "always.h"

#include "sharedcontrol.h"

#include "airally.h"
#include "debughandler.h"
#include "extension.h"
#include "hooker.h"
#include "house.h"
#include "housetype.h"
#include "session.h"
#include "spawner.h"
#include "syringe.h"
#include "techno.h"
#include "technotype.h"
#include "technotypeext.h"
#include "tibsun_globals.h"

#include <algorithm>


static bool SharedControlHouses[32];
static constexpr const char* COOPSP_PLAYER_HOUSE = "Fake31";


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


static void Init_CoopSP_Player_House()
{
    if (!Spawner::Is_CoopSP() || PlayerPtr == nullptr) {
        return;
    }

    HousesType coop = HouseTypeClass::From_Name(COOPSP_PLAYER_HOUSE);
    if ((coop == HOUSE_NONE || coop >= 32) && HouseTypes.Count() > 31) {
        coop = static_cast<HousesType>(31);
    }
    if (coop == HOUSE_NONE || coop >= 32) {
        DEBUG_WARNING("CoopSP: no house type \"{}\" below index 32; player keeps ActLike {}.\n",
            COOPSP_PLAYER_HOUSE, static_cast<int>(PlayerPtr->ActLike));
        return;
    }

    const HousesType country = PlayerPtr->Class->House;
    if (country == coop) {
        return;
    }

    const long src = 1L << country;
    const long dst = 1L << coop;

    for (int i = 0; i < TechnoTypes.Count(); i++) {
        TechnoTypeClass* ttype = TechnoTypes[i];
        if (ttype == nullptr) {
            continue;
        }

        if (ttype->Ownable & src) {
            ttype->Ownable |= dst;
        }

        TechnoTypeClassExtension* ext = Extension::Fetch(ttype);
        if (ext->RequiredHouses != -1 && (ext->RequiredHouses & src)) {
            ext->RequiredHouses |= dst;
        }
        if (ext->ForbiddenHouses != -1 && (ext->ForbiddenHouses & src)) {
            ext->ForbiddenHouses |= dst;
        }
    }

    PlayerPtr->ActLike = coop;

    DEBUG_INFO("CoopSP: player house {} builds as \"{}\" (type {}) with the ownership of type {}.\n",
        static_cast<int>(PlayerPtr->HeapID), HouseTypes[coop]->Name(), static_cast<int>(coop), static_cast<int>(country));
}


void Vinifera_Init_Shared_Control()
{
    std::fill(std::begin(SharedControlHouses), std::end(SharedControlHouses), false);
    Vinifera_Reset_AI_Rally();

    if (PlayerPtr == nullptr) {
        return;
    }

    Init_CoopSP_Player_House();

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

        house->IsAITriggersOn = true;

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

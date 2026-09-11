#include "always.h"

#include "airally.h"

#include "aircraft.h"
#include "aircrafttype.h"
#include "beacon.h"
#include "debughandler.h"
#include "foot.h"
#include "hooker.h"
#include "house.h"
#include "infantry.h"
#include "mouse.h"
#include "session.h"
#include "sharedcontrol.h"
#include "syringe.h"
#include "team.h"
#include "tibsun_globals.h"
#include "unit.h"
#include "unittype.h"

#include <algorithm>


#define AI_RALLY_MAX_HOUSES 32
#define AI_RALLY_REFRESH_RATE 60
#define AI_RALLY_HOLD_RADIUS 6


static void Release_Rallied_Units();

static bool RallyActive;
static Cell RallyPoint;
static int RallyOwner;
static int RallyBeaconID;
static int RallyNextSweepFrame;
static bool RallyHouses[AI_RALLY_MAX_HOUSES];


void Vinifera_Reset_AI_Rally()
{
    RallyActive = false;
    RallyPoint = Cell(0, 0);
    RallyOwner = -1;
    RallyBeaconID = -1;
    RallyNextSweepFrame = 0;
    std::fill(std::begin(RallyHouses), std::end(RallyHouses), false);
}


bool Vinifera_Has_AI_Rally()
{
    return RallyActive;
}


bool Vinifera_Is_At_AI_Rally(const Cell& cell)
{
    if (!RallyActive) {
        return false;
    }

    return std::abs(cell.X - RallyPoint.X) <= 2 && std::abs(cell.Y - RallyPoint.Y) <= 2;
}


bool Vinifera_Is_AI_Rally_House(const HouseClass* house)
{
    if (!RallyActive || house == nullptr) {
        return false;
    }

    const int id = house->HeapID;
    if (id < 0 || id >= AI_RALLY_MAX_HOUSES) {
        return false;
    }

    return RallyHouses[id];
}


static bool Is_Rallyable(const FootClass* foot)
{
    if (foot == nullptr || !foot->IsActive || foot->IsInLimbo || foot->IsTethered) {
        return false;
    }

    if (foot->In_Radio_Contact()) {
        return false;
    }

    if (foot->RTTI == RTTI_UNIT) {
        const UnitTypeClass* type = static_cast<const UnitClass*>(foot)->Class;
        if (type->IsToHarvest || type->IsToVeinHarvest || type->DeploysInto != nullptr) {
            return false;
        }
    }

    if (!foot->Is_Weapon_Equipped()) {
        return false;
    }

    return true;
}


static bool Is_Busy_Mission(MissionType mission)
{
    switch (mission) {
    case MISSION_HARVEST:
    case MISSION_RETURN:
    case MISSION_UNLOAD:
    case MISSION_ENTER:
    case MISSION_CAPTURE:
    case MISSION_SABOTAGE:
    case MISSION_CONSTRUCTION:
    case MISSION_DECONSTRUCTION:
    case MISSION_REPAIR:
    case MISSION_RESCUE:
        return true;
    default:
        return false;
    }
}


bool Vinifera_Is_AI_Rally_Unit(const FootClass* foot)
{
    return Is_Rallyable(foot) && Vinifera_Is_AI_Rally_House(foot->House);
}


static bool Is_Rallyable_Air(const AircraftClass* air)
{
    if (air == nullptr || !air->IsActive || air->IsInLimbo || air->IsALoaner) {
        return false;
    }

    if (!air->Is_Weapon_Equipped()) {
        return false;
    }

    if (air->Ammo == 0) {
        return false;
    }

    if (air->Ammo > 0 && air->Ammo < air->Class->MaxAmmo && air->In_Radio_Contact()) {
        return false;
    }

    return true;
}


bool Vinifera_AI_Rally_Air_On_Station(const AircraftClass* air)
{
    if (!RallyActive || air == nullptr) {
        return false;
    }

    if (!Vinifera_Is_AI_Rally_House(air->House)) {
        return false;
    }

    if (air->In_Radio_Contact() || !Is_Rallyable_Air(air)) {
        return false;
    }

    return true;
}


static Cell Rally_Spot(const Cell& center, int index)
{
    static const int offsets[][2] = {
        { 0, 0 },
        { 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, -1 },
        { 1, 1 }, { -1, 1 }, { -1, -1 }, { 1, -1 },
        { 2, 0 }, { 0, 2 }, { -2, 0 }, { 0, -2 },
        { 2, 1 }, { 1, 2 }, { -1, 2 }, { -2, 1 },
        { -2, -1 }, { -1, -2 }, { 1, -2 }, { 2, -1 },
        { 2, 2 }, { -2, 2 }, { -2, -2 }, { 2, -2 },
        { 3, 0 }, { 0, 3 }, { -3, 0 }, { 0, -3 },
        { 3, 1 }, { 1, 3 }, { -1, 3 }, { -3, 1 },
        { -3, -1 }, { -1, -3 }, { 1, -3 }, { 3, -1 },
        { 3, 2 }, { 2, 3 }, { -2, 3 }, { -3, 2 },
        { -3, -2 }, { -2, -3 }, { 2, -3 }, { 3, -2 },
        { 4, 0 }, { 0, 4 }, { -4, 0 }, { 0, -4 },
        { 4, 2 }, { 2, 4 }, { -2, 4 }, { -4, 2 },
        { -4, -2 }, { -2, -4 }, { 2, -4 }, { 4, -2 },
        { 4, 4 }, { -4, 4 }, { -4, -4 }, { 4, -4 },
        { 5, 0 }, { 0, 5 }, { -5, 0 }, { 0, -5 }
    };

    const int count = static_cast<int>(std::size(offsets));
    const int slot = index % count;
    const int rings = index / count;

    Cell cell(center.X + offsets[slot][0] * (rings + 1), center.Y + offsets[slot][1] * (rings + 1));

    if (!Map.In_Radar(cell)) {
        return center;
    }

    return cell;
}


static void Set_Recruitable(TechnoClass* techno, bool recruitable)
{
    techno->field_205 = recruitable;
    techno->field_206 = recruitable;
}


static void Send_To_Rally(FootClass* foot, const Cell& rally, int index)
{
    if (foot->Team != nullptr) {
        foot->Team->Remove(foot);
    }

    Cell spot = Rally_Spot(rally, index);
    AbstractClass* target = &Map[spot];

    foot->Assign_Mission(MISSION_GUARD_AREA);
    foot->SuspendedNavCom = nullptr;
    foot->SuspendedTarCom = nullptr;
    foot->Clear_Navigation_List();
    foot->Assign_Target(nullptr);
    foot->Assign_Destination(target);
    foot->ArchiveTarget = target;
}


static void Send_Air_To_Rally(AircraftClass* air, const Cell& rally, int index)
{
    if (air->Team != nullptr) {
        air->Team->Remove(air);
    }

    Cell spot = Rally_Spot(rally, index);
    AbstractClass* target = &Map[spot];

    air->SuspendedNavCom = nullptr;
    air->SuspendedTarCom = nullptr;
    air->Clear_Navigation_List();
    air->Assign_Mission(MISSION_GUARD_AREA);
    air->Assign_Target(nullptr);
    air->Assign_Destination(target);
    air->ArchiveTarget = target;
}


static bool Is_Air_Settled_On_Rally(const AircraftClass* air, const Cell& rally)
{
    if (air->Team != nullptr || air->Mission != MISSION_GUARD_AREA || air->ArchiveTarget == nullptr) {
        return false;
    }

    const Cell archive = air->ArchiveTarget->Center_Coord().As_Cell();
    return std::abs(archive.X - rally.X) <= 8 && std::abs(archive.Y - rally.Y) <= 8;
}


static bool Is_Settled_On_Rally(const FootClass* foot, const Cell& rally)
{
    if (foot->Team != nullptr || foot->Mission != MISSION_GUARD_AREA || foot->ArchiveTarget == nullptr) {
        return false;
    }

    const Cell archive = foot->ArchiveTarget->Center_Coord().As_Cell();
    return std::abs(archive.X - rally.X) <= 8 && std::abs(archive.Y - rally.Y) <= 8;
}


static bool Is_Safe_To_Release(const FootClass* foot)
{
    if (foot == nullptr || !foot->IsActive || foot->IsInLimbo) {
        return false;
    }

    if (foot->IsTethered || foot->In_Radio_Contact()) {
        return false;
    }

    return Is_Settled_On_Rally(foot, RallyPoint);
}


static int Rally_House_Index(const FootClass* foot)
{
    const HouseClass* house = foot->House;
    if (house == nullptr) {
        return -1;
    }

    const int id = house->HeapID;
    if (id < 0 || id >= AI_RALLY_MAX_HOUSES || !RallyHouses[id]) {
        return -1;
    }

    return id;
}


static void Gather_Rallied_Units(bool force)
{
    int index[AI_RALLY_MAX_HOUSES] = {};

    for (int i = 0; i < Units.Count(); i++) {
        UnitClass* unit = Units[i];

        const int id = Rally_House_Index(unit);
        if (id < 0 || !Is_Rallyable(unit)) {
            continue;
        }

        int& slot = index[id];
        Set_Recruitable(unit, false);

        if (!force && (unit->TarCom != nullptr || Is_Busy_Mission(unit->Mission) || Is_Settled_On_Rally(unit, RallyPoint))) {
            slot++;
            continue;
        }

        Send_To_Rally(unit, RallyPoint, slot++);
    }

    for (int i = 0; i < Infantry.Count(); i++) {
        InfantryClass* inf = Infantry[i];

        const int id = Rally_House_Index(inf);
        if (id < 0 || !Is_Rallyable(inf)) {
            continue;
        }

        int& slot = index[id];
        Set_Recruitable(inf, false);

        if (!force && (inf->TarCom != nullptr || Is_Busy_Mission(inf->Mission) || Is_Settled_On_Rally(inf, RallyPoint))) {
            slot++;
            continue;
        }

        Send_To_Rally(inf, RallyPoint, slot++);
    }

    for (int i = 0; i < Aircrafts.Count(); i++) {
        AircraftClass* air = Aircrafts[i];

        const int id = Rally_House_Index(air);
        if (id < 0 || !Is_Rallyable_Air(air)) {
            continue;
        }

        int& slot = index[id];
        Set_Recruitable(air, false);

        if (!force && (air->TarCom != nullptr || Is_Busy_Mission(air->Mission) || Is_Air_Settled_On_Rally(air, RallyPoint))) {
            slot++;
            continue;
        }

        if (air->In_Radio_Contact()) {
            air->Transmit_Message(RADIO_OVER_OUT);
        }

        Send_Air_To_Rally(air, RallyPoint, slot++);
    }
}


static void Delete_Rally_Beacon()
{
    if (RallyOwner >= 0 && RallyBeaconID != -1) {
        BeaconManager.Delete_Beacon(static_cast<HousesType>(RallyOwner), RallyBeaconID);
    }

    RallyOwner = -1;
    RallyBeaconID = -1;
}


void Vinifera_Set_AI_Rally(int sender_id, const Cell& cell, int beacon_id)
{
    if (sender_id < 0 || sender_id >= Houses.Count()) {
        return;
    }

    HouseClass* sender = Houses[sender_id];
    if (sender == nullptr) {
        return;
    }

    Delete_Rally_Beacon();

    if (RallyActive) {
        Release_Rallied_Units();
    }

    RallyActive = true;
    RallyPoint = cell;
    RallyOwner = sender_id;
    RallyBeaconID = beacon_id;
    RallyNextSweepFrame = Frame + AI_RALLY_REFRESH_RATE;
    std::fill(std::begin(RallyHouses), std::end(RallyHouses), false);

    for (int i = 0; i < Houses.Count() && i < AI_RALLY_MAX_HOUSES; i++) {
        HouseClass* house = Houses[i];

        if (house == nullptr || house->IsHuman || house == sender) {
            continue;
        }

        if (!sender->Is_Ally(house) || !house->Is_Ally(sender)) {
            continue;
        }

        RallyHouses[i] = true;

        DEBUG_INFO("AIRally: house {} \"{}\" rallying to ({}, {}).\n",
            i, house->IniName.c_str(), cell.X, cell.Y);
    }

    Gather_Rallied_Units(true);
}


static void Release_Rallied_Units()
{
    for (int i = 0; i < Units.Count(); i++) {
        UnitClass* unit = Units[i];

        if (Rally_House_Index(unit) < 0) {
            continue;
        }

        Set_Recruitable(unit, true);

        if (Is_Safe_To_Release(unit)) {
            unit->Enter_Idle_Mode();
        }
    }

    for (int i = 0; i < Infantry.Count(); i++) {
        InfantryClass* inf = Infantry[i];

        if (Rally_House_Index(inf) < 0) {
            continue;
        }

        Set_Recruitable(inf, true);

        if (Is_Safe_To_Release(inf)) {
            inf->Enter_Idle_Mode();
        }
    }

    for (int i = 0; i < Aircrafts.Count(); i++) {
        AircraftClass* air = Aircrafts[i];

        if (Rally_House_Index(air) < 0) {
            continue;
        }

        Set_Recruitable(air, true);

        if (Is_Safe_To_Release(air)) {
            air->Enter_Idle_Mode();
        }
    }
}


void Vinifera_Clear_AI_Rally()
{
    if (!RallyActive) {
        return;
    }

    Delete_Rally_Beacon();
    Release_Rallied_Units();

    RallyActive = false;
    std::fill(std::begin(RallyHouses), std::end(RallyHouses), false);

    DEBUG_INFO("AIRally: rally cleared.\n");
}


DEFINE_HOOK(0x004BC5E0, _HouseClass_AI_Rally, 6)
{
    if (!RallyActive) {
        return 0;
    }

    GET(HouseClass*, this_ptr, ECX);

    const int id = this_ptr->HeapID;
    if (id < 0 || id >= AI_RALLY_MAX_HOUSES || !RallyHouses[id]) {
        return 0;
    }

    if (Frame >= RallyNextSweepFrame) {
        RallyNextSweepFrame = Frame + AI_RALLY_REFRESH_RATE;
        Gather_Rallied_Units(false);
    }

    return 0;
}

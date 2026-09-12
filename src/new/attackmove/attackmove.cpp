#include "always.h"

#include "attackmove.h"

#include "eventext.h"
#include "extension.h"
#include "foot.h"
#include "footext.h"
#include "house.h"
#include "infantry.h"
#include "infantrytype.h"
#include "iomap.h"
#include "mouse.h"
#include "session.h"
#include "sharedcontrol.h"
#include "target.h"
#include "technotype.h"
#include "tibsun_globals.h"
#include "tibsun_inline.h"
#include "vinifera_defines.h"


#define ATTACKMOVE_SCAN_MIN 2
#define ATTACKMOVE_SCAN_MAX 5


static int AttackMoveActiveCount = 0;


bool Vinifera_AttackMove_Type_Allowed(const FootClass* foot)
{
    if (foot == nullptr || foot->RTTI == RTTI_AIRCRAFT) {
        return false;
    }

    if (foot->Techno_Type_Class()->IsSubterranean) {
        return false;
    }

    if (foot->RTTI == RTTI_INFANTRY
        && reinterpret_cast<const InfantryClass*>(foot)->Class->IsJumpJet) {
        return false;
    }

    return true;
}


static bool Can_Attack_Move(const ObjectClass* object)
{
    if (object == nullptr || !object->Is_Foot()) {
        return false;
    }

    const FootClass* foot = reinterpret_cast<const FootClass*>(object);

    if (!foot->IsActive || foot->IsInLimbo) {
        return false;
    }

    return foot->Can_Player_Move() && foot->Can_Player_Fire();
}


bool Vinifera_AttackMove_Has_Selection()
{
    for (int i = 0; i < CurrentObjects.Count(); ++i) {
        ObjectClass* object = CurrentObjects[i];

        if (!Can_Attack_Move(object)) {
            continue;
        }

        if (!static_cast<TechnoClass*>(object)->House->Is_Player_Control() &&
            !Vinifera_Can_Locally_Control(static_cast<TechnoClass*>(object))) {
            continue;
        }

        return true;
    }

    return false;
}


ActionType Vinifera_AttackMove_Action()
{
    if (PlayerPtr->IsDefeated || !Vinifera_AttackMove_Has_Selection()) {
        return ACTION_NOMOVE;
    }

    return static_cast<ActionType>(EXT_ACTION_ATTACK_MOVE);
}


bool Vinifera_AttackMove_Is_Action(ActionType action)
{
    return action == static_cast<ActionType>(EXT_ACTION_ATTACK_MOVE);
}


void Vinifera_AttackMove_Order(const Cell& cell)
{
    if (PlayerPtr->IsDefeated || !Map.In_Radar(cell)) {
        return;
    }

    for (int i = 0; i < CurrentObjects.Count(); ++i) {
        ObjectClass* object = CurrentObjects[i];

        if (!Can_Attack_Move(object)) {
            continue;
        }

        TechnoClass* techno = static_cast<TechnoClass*>(object);

        if (!techno->House->Is_Player_Control() && !Vinifera_Can_Locally_Control(techno)) {
            continue;
        }

        // Resolved client side, as the engine does in code/foot.cpp:1765.
        const Cell moveto = reinterpret_cast<FootClass*>(techno)->Move_Order(cell, false);

        if (moveto == CELL_NONE) {
            continue;
        }

        OutList.Add(EventClassExt(PlayerPtr->HeapID, TargetClass(techno), moveto).As_Event());
    }
}


void Vinifera_AttackMove_Assign(int sender_id, const xTargetClass& whom, const Cell& cell)
{
    FootClass* foot = whom.As_Foot();

    if (foot == nullptr || !foot->IsActive || foot->IsInLimbo || foot->IsTethered) {
        return;
    }

    if (Session.Type != GAME_NORMAL) {
        if (sender_id != foot->House->HeapID && !Vinifera_Event_Sender_Controls(sender_id, foot)) {
            return;
        }
    } else {
        if (!foot->House->IsPlayerControl && !Vinifera_Can_Locally_Control(foot)) {
            return;
        }
    }

    if (foot->PrimaryWeapon == nullptr || !Vinifera_AttackMove_Type_Allowed(foot)) {
        return;
    }

    FootClassExtension* ext = Extension::Fetch(foot);

    if (!ext->IsAttackMove) {
        AttackMoveActiveCount++;
    }

    ext->IsAttackMove = true;
    ext->AttackMoveCell = cell;
    ext->AttackMoveStatus = ATTACKMOVE_TRAVEL;
    ext->AttackMoveScanDelay = 0;

    foot->Assign_Target(nullptr);
    foot->ArchiveTarget = nullptr;
    foot->Clear_Navigation_List();
    foot->Assign_Destination(&Map[cell]);
    foot->Assign_Mission(MISSION_MOVE);
    foot->Commence();
}


void Vinifera_AttackMove_Clear(TechnoClass* techno)
{
    if (AttackMoveActiveCount == 0 || techno == nullptr || !techno->Is_Foot()) {
        return;
    }

    FootClassExtension* ext = Extension::Fetch(reinterpret_cast<FootClass*>(techno));

    if (!ext->IsAttackMove) {
        return;
    }

    ext->IsAttackMove = false;
    ext->AttackMoveCell = Cell(0, 0);
    ext->AttackMoveStatus = ATTACKMOVE_TRAVEL;
    ext->AttackMoveScanDelay = 0;

    AttackMoveActiveCount--;
}


void Vinifera_AttackMove_Uncount()
{
    if (AttackMoveActiveCount > 0) {
        AttackMoveActiveCount--;
    }
}


void Vinifera_AttackMove_Recount()
{
    AttackMoveActiveCount = 0;

    for (int i = 0; i < Technos.Count(); ++i) {
        TechnoClass* techno = Technos[i];

        if (!techno->Is_Foot()) {
            continue;
        }

        if (Extension::Fetch(reinterpret_cast<FootClass*>(techno))->IsAttackMove) {
            AttackMoveActiveCount++;
        }
    }
}


bool Vinifera_AttackMove_Any_Active()
{
    return AttackMoveActiveCount > 0;
}


int Vinifera_AttackMove_Status(const FootClass* foot)
{
    if (AttackMoveActiveCount == 0 || foot == nullptr) {
        return -1;
    }

    FootClassExtension* ext = Extension::Fetch(const_cast<FootClass*>(foot));

    if (!ext->IsAttackMove) {
        return -1;
    }

    return ext->AttackMoveStatus;
}


bool Vinifera_AttackMove_Destination(const FootClass* foot, Cell& cell)
{
    if (AttackMoveActiveCount == 0 || foot == nullptr) {
        return false;
    }

    FootClassExtension* ext = Extension::Fetch(const_cast<FootClass*>(foot));

    if (!ext->IsAttackMove) {
        return false;
    }

    cell = ext->AttackMoveCell;
    return true;
}


static void Halt(FootClass* foot)
{
    // Mirrors the engine stop sequence in code/event.cpp:1078 case IDLE.
    foot->NavQueue.Clear();
    foot->Clear_Navigation_List();
    foot->CurrentPath = PATH_NONE;
    foot->NextWaypoint = 0;
    foot->Transmit_Message(RADIO_OVER_OUT);
    foot->Assign_Destination(nullptr);
}


static AbstractClass* Scan_For_Target(FootClass* foot)
{
    AbstractClass* target = foot->Greatest_Threat(THREAT_RANGE, foot->PositionCoord, false);

    if (target == nullptr || !Is_Target_Object(target)) {
        return nullptr;
    }

    return target;
}


static void Resume_Travel(FootClass* foot, FootClassExtension* ext)
{
    ext->AttackMoveStatus = ATTACKMOVE_TRAVEL;
    ext->AttackMoveScanDelay = Random_Pick(ATTACKMOVE_SCAN_MIN, ATTACKMOVE_SCAN_MAX);

    foot->Assign_Target(nullptr);
    foot->Assign_Destination(&Map[ext->AttackMoveCell]);
    foot->Assign_Mission(MISSION_MOVE);
    foot->Commence();
}


void Vinifera_AttackMove_AI(FootClass* foot)
{
    if (AttackMoveActiveCount == 0) {
        return;
    }

    FootClassExtension* ext = Extension::Fetch(foot);

    if (!ext->IsAttackMove) {
        return;
    }

    if (!foot->IsActive || foot->IsInLimbo || foot->IsTethered || foot->Strength <= 0) {
        Vinifera_AttackMove_Clear(foot);
        return;
    }

    if (foot->PrimaryWeapon == nullptr || foot->House->IsDefeated) {
        Vinifera_AttackMove_Clear(foot);
        return;
    }

    switch (ext->AttackMoveStatus) {

    case ATTACKMOVE_TRAVEL:
    {
        if (foot->NavCom == nullptr && !foot->Locomotion->Is_Moving()) {
            Vinifera_AttackMove_Clear(foot);
            foot->Enter_Idle_Mode();
            return;
        }

        if (ext->AttackMoveScanDelay > 0) {
            ext->AttackMoveScanDelay--;
            return;
        }

        ext->AttackMoveScanDelay = Random_Pick(ATTACKMOVE_SCAN_MIN, ATTACKMOVE_SCAN_MAX);

        AbstractClass* target = Scan_For_Target(foot);

        if (target != nullptr) {
            ext->AttackMoveStatus = ATTACKMOVE_APPROACH;
            Halt(foot);
            foot->Assign_Target(target);
            foot->Assign_Mission(MISSION_ATTACK);
            foot->Commence();
        }

        break;
    }

    case ATTACKMOVE_APPROACH:
    {
        if (foot->TarCom == nullptr || foot->Mission != MISSION_ATTACK) {
            Resume_Travel(foot, ext);
            break;
        }

        if (foot->In_Range_Of(foot->TarCom, foot->What_Weapon_Should_I_Use(foot->TarCom))) {

            // No further track starts once the destination is clear and Path[0] is FACING_NONE (drive.cpp:529).
            foot->Assign_Destination(nullptr);
            foot->Path[0] = FACING_NONE;

            ext->AttackMoveStatus = ATTACKMOVE_ENGAGE;
            foot->Assign_Mission(MISSION_GUARD);
            foot->Commence();
            break;
        }

        if (foot->NavCom != nullptr || foot->Locomotion->Is_Moving()) {
            break;
        }

        foot->Assign_Target(nullptr);
        Resume_Travel(foot, ext);
        ext->AttackMoveScanDelay = 2 * TICKS_PER_SECOND;

        break;
    }

    case ATTACKMOVE_ENGAGE:
    {
        if (ext->AttackMoveScanDelay > 0) {
            ext->AttackMoveScanDelay--;
            break;
        }

        ext->AttackMoveScanDelay = Random_Pick(ATTACKMOVE_SCAN_MIN, ATTACKMOVE_SCAN_MAX);

        if (foot->TarCom != nullptr) {

            TechnoClass* victim = Target_As_Techno(foot->TarCom);

            if (victim != nullptr && foot->House->Is_Ally(victim->House)) {
                foot->Assign_Target(nullptr);
                break;
            }

            if (foot->Can_Fire(foot->TarCom, foot->What_Weapon_Should_I_Use(foot->TarCom)) == FIRE_CANT) {
                foot->Assign_Target(nullptr);
                break;
            }

            break;
        }

        AbstractClass* target = Scan_For_Target(foot);

        if (target != nullptr) {
            Halt(foot);
            foot->Assign_Target(target);
            break;
        }

        Resume_Travel(foot, ext);

        break;
    }

    default:
        break;
    }
}

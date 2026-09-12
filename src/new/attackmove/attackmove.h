#pragma once

#include "tibsun_defines.h"


class FootClass;
class TechnoClass;
class xTargetClass;


enum AttackMoveStatusType {
    ATTACKMOVE_TRAVEL,
    ATTACKMOVE_APPROACH,
    ATTACKMOVE_ENGAGE
};

#define ATTACKMOVE_LOOKAHEAD_LEPTONS (2 * CELL_LEPTON)

void Vinifera_AttackMove_Order(const Cell& cell);
void Vinifera_AttackMove_Assign(int sender_id, const xTargetClass& whom, const Cell& cell);
void Vinifera_AttackMove_AI(FootClass* foot);
void Vinifera_AttackMove_Clear(TechnoClass* techno);
bool Vinifera_AttackMove_Is_Active(const FootClass* foot);
bool Vinifera_AttackMove_Type_Allowed(const FootClass* foot);
bool Vinifera_AttackMove_Is_Travelling(const FootClass* foot);
bool Vinifera_AttackMove_Destination(const FootClass* foot, Cell& cell);
bool Vinifera_AttackMove_Has_Selection();
ActionType Vinifera_AttackMove_Action();
bool Vinifera_AttackMove_Is_Action(ActionType action);

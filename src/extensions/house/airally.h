#pragma once

#include "tibsun_defines.h"


class HouseClass;
class FootClass;
class AircraftClass;

void Vinifera_Reset_AI_Rally();
bool Vinifera_Is_AI_Rally_House(const HouseClass* house);
bool Vinifera_Is_AI_Rally_Unit(const FootClass* foot);
void Vinifera_Set_AI_Rally(int sender_id, const Cell& cell, int beacon_id);
void Vinifera_Clear_AI_Rally();
bool Vinifera_Has_AI_Rally();
bool Vinifera_Is_At_AI_Rally(const Cell& cell);
bool Vinifera_AI_Rally_Air_On_Station(const AircraftClass* air);

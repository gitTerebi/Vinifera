#pragma once


class HouseClass;
class TechnoClass;

void Vinifera_Init_Shared_Control();
bool Vinifera_Is_Shared_Control_House(const HouseClass* house);
bool Vinifera_Can_Locally_Control(const TechnoClass* techno);
bool Vinifera_Event_Sender_Controls(int sender_id, const TechnoClass* techno);

/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Contains the hooks for the extended ScrollClass.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "building.h"
#include "command.h"
#include "hooker.h"
#include "house.h"
#include "mouse.h"
#include "options.h"
#include "particlesys.h"
#include "rules.h"
#include "scenario.h"
#include "scrollext_hooks.h"
#include "session.h"
#include "syringe.h"
#include "techno.h"
#include "tibsun_defines.h"
#include "tibsun_globals.h"
#include "vinifera_globals.h"
#include "windialog.h"

#include <algorithm>


bool Passes_Cloak_Check(TechnoClass* techno)
{
    if (PlayerPtr->Is_Ally(techno)) {
        return true;
    }

    Coord coord = techno->Center_Coord();
    if (Map[coord].Sensed_By(PlayerPtr->HeapID)) {
        return true;
    }

    return false;
}


/**
 *  Fixes a bug where mouse input is not handled on cloaked objects of allied players.
 *  Sadly, the bug appears to be caused by a function that is inlined or copy-pasted
 *  in multiple places, so we need multiple patches.
 *
 *  Author: Rampastring
 */
DEFINE_HOOK(0x005E8840, _ScrollClass_Input_Allied_Cloaked_Object_Patch1, 0)
{
    GET(TechnoClass*, techno, ESI);

    if (Passes_Cloak_Check(techno)) {
        // Target object is cloaked but visible to us (allied or sensed),
        // handle mouse input on it normally.
        return 0x005E886B;
    }

    // Target object is cloaked and not visible to us.
    return 0x005E887F;
}


/**
 *  Fixes a bug where mouse input is not handled on cloaked buildings of allied players.
 *
 *  Author: Rampastring
 */
DEFINE_HOOK(0x005E88AA, _ScrollClass_Input_Allied_Cloaked_Object_Patch2, 0)
{
    GET(BuildingClass*, building, EDI);

    if (Passes_Cloak_Check(building)) {
        // Target object is visible to us (allied or sensed), handle mouse input on it normally.
        return 0x005E88D6;
    }

    // Target object is cloaked and not visible to us.
    return 0x005E88E6;
}


/**
 *  Fixes a bug where mouse input is not handled on cloaked objects of allied players.
 *  While this is in TacticalClass, the bug is identical to the bugs in ScrollClass
 *  and has an identical fix, which is why we include this hack here.
 *
 *  Author: Rampastring
 */
DEFINE_HOOK(0x006167E3, _Tactical_Get_Object_At_Cell_Allied_Cloaked_Object_Patch, 0)
{
    GET(TechnoClass*, techno, EAX);

    if (Passes_Cloak_Check(techno)) {
        // Target object is visible to us (allied or sensed), handle mouse input on it normally.
        return 0x0061680F;
    }

    // Target object is cloaked and not visible to us.
    return 0x00616811;
}


/**
 *  Blocks the map from receiving inputs while any Windows dialogs are open.
 *
 *  Author: ZivDero
 */
DEFINE_HOOK(0x005E934E, _ScrollClass_Message_Handler_Block_Inputs_In_Dialogs_Patch, 6)
{
    if (WSDialogCount != 0) {
        return 0x005E961E;
    }
    return 0;
}


static const CommandType SmoothScrollCommands[] = {
    COMMAND_SCROLLNORTH,
    COMMAND_SCROLLSOUTH,
    COMMAND_SCROLLEAST,
    COMMAND_SCROLLWEST
};


bool Vinifera_Is_Smooth_Scroll_Command(CommandClass* command)
{
    if (!Vinifera_SmoothKeyboardScroll || command == nullptr) {
        return false;
    }

    for (CommandType type : SmoothScrollCommands) {
        if (Commands.Count() > type && Commands[type] == command) {
            return true;
        }
    }

    return false;
}


static bool Is_Scroll_Command_Key_Down(CommandType type)
{
    if (Commands.Count() <= type) {
        return false;
    }

    KeyNumType key = HotkeyIndex.Fetch_ID_By_Data(Commands[type]);
    if (key == KN_NONE || static_cast<int>(key) == -1) {
        return false;
    }

    return Keyboard->Down(key & 0xFF);
}


static unsigned SmoothScrollLastTick = 0;
static int SmoothScrollRemainder = 0;

static const unsigned SmoothScrollMaxStepMS = 100;

static const int SmoothScrollReferenceFPS = 60;

static const int SmoothScrollDistances[9] = { 448, 384, 320, 256, 192, 128, 64, 32, 16 };


static void Smooth_Keyboard_Scroll()
{
    if (!Vinifera_SmoothKeyboardScroll) {
        return;
    }

    if (Keyboard == nullptr || Rule == nullptr || Commands.Count() <= COMMAND_SCROLLWEST) {
        return;
    }

    const unsigned now = GetTickCount();
    unsigned elapsed = now - SmoothScrollLastTick;
    SmoothScrollLastTick = now;

    if (elapsed > SmoothScrollMaxStepMS) {
        elapsed = SmoothScrollMaxStepMS;
    }

    if (WSDialogCount != 0 || Session.Messages.Is_Edit()) {
        return;
    }

    if (Scen != nullptr && Scen->InputLock) {
        return;
    }

    const int north = Is_Scroll_Command_Key_Down(COMMAND_SCROLLNORTH) ? 1 : 0;
    const int south = Is_Scroll_Command_Key_Down(COMMAND_SCROLLSOUTH) ? 1 : 0;
    const int east = Is_Scroll_Command_Key_Down(COMMAND_SCROLLEAST) ? 1 : 0;
    const int west = Is_Scroll_Command_Key_Down(COMMAND_SCROLLWEST) ? 1 : 0;

    const int horizontal = east - west;
    const int vertical = south - north;

    if (horizontal == 0 && vertical == 0) {
        SmoothScrollRemainder = 0;
        return;
    }

    FacingType facing;
    if (vertical < 0) {
        facing = horizontal > 0 ? FACING_NE : (horizontal < 0 ? FACING_NW : FACING_N);
    } else if (vertical > 0) {
        facing = horizontal > 0 ? FACING_SE : (horizontal < 0 ? FACING_SW : FACING_S);
    } else {
        facing = horizontal > 0 ? FACING_E : FACING_W;
    }

    const int step = std::clamp(Options.ScrollRate, 0, 4) + 1;
    const double per_second = SmoothScrollDistances[step] * Rule->ScrollMultiplier * SmoothScrollReferenceFPS;
    const int total = static_cast<int>(per_second * elapsed) + SmoothScrollRemainder;

    int distance = total / 1000;
    SmoothScrollRemainder = total % 1000;

    if (distance <= 0) {
        return;
    }

    Map.Scroll_Map(facing, distance, true);
}


DEFINE_HOOK(0x005E85E8, _ScrollClass_AI_Smooth_Keyboard_Scroll_Patch, 8)
{
    Smooth_Keyboard_Scroll();

    return 0;
}


/**
 *  Main function for patching the hooks.
 */
void ScrollClassExtension_Hooks()
{

}

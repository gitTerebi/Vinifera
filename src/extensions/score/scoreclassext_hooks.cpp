/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Contains the hooks for the extended ScoreClass.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "hooker.h"
#include "miscutil.h"
#include "scenarioext.h"
#include "syringe.h"
#include "tibsun_globals.h"


/**
 *  #issue-242
 *
 *  Allows customizing the colors of the singleplayer score screen.
 *
 *  Author: Rampastring
 */
DEFINE_HOOK(0x005E532A, _ScoreClass_Draw_Dual_Bars_Player_RGB_Patch, 0)
{
    R->Stack<unsigned char>(0x18, ScenExtension->ScorePlayerColor.R);
    R->Stack<unsigned char>(0x18 + 1, ScenExtension->ScorePlayerColor.G);
    R->Stack<unsigned char>(0x18 + 2, ScenExtension->ScorePlayerColor.B);
    return R->Origin() + 0xE;
}
DEFINE_HOOK_AGAIN(0x005E536B, _ScoreClass_Draw_Dual_Bars_Player_RGB_Patch, 0);
DEFINE_HOOK_AGAIN(0x005E53AC, _ScoreClass_Draw_Dual_Bars_Player_RGB_Patch, 0);

DEFINE_HOOK(0x005E53F8, _ScoreClass_Draw_Dual_Bars_Enemy_RGB_Patch, 0)
{
    R->Stack<unsigned char>(0x18, ScenExtension->ScoreEnemyColor.R);
    R->Stack<unsigned char>(0x18 + 1, ScenExtension->ScoreEnemyColor.G);
    R->Stack<unsigned char>(0x18 + 2, ScenExtension->ScoreEnemyColor.B);
    return R->Origin() + 0xD;
}
DEFINE_HOOK_AGAIN(0x005E543B, _ScoreClass_Draw_Dual_Bars_Enemy_RGB_Patch, 0);
DEFINE_HOOK_AGAIN(0x005E547E, _ScoreClass_Draw_Dual_Bars_Enemy_RGB_Patch, 0);


/**
 *  Pressing ESC on the singleplayer score screen fast-forwards to the end of it.
 *
 *  Every wait in ScoreClass::Presentation goes through Call_Back_Delay, and the
 *  print waits spin until their object leaves ScoreObjs. Once ESC is pressed,
 *  Call_Back_Delay deletes every queued object and returns without waiting, so
 *  the rest of the presentation runs through in a few frames. The hall of fame
 *  name entry and the closing "click to continue" wait are left as well.
 */
static bool ScoreFastForward = false;
static bool ScoreEscWasDown = false;
static bool &ScoreStillUpdating = *reinterpret_cast<bool *>(0x0080A370);

DEFINE_HOOK(0x005E2EA0, _ScoreClass_Presentation_Reset_Fast_Forward_Patch, 6)
{
    ScoreFastForward = false;

    /**
     *  An ESC still held from skipping the win movie must not count as a press.
     */
    ScoreEscWasDown = Key_Down(VK_ESCAPE);
    return 0;
}

DEFINE_HOOK(0x005E63A0, _ScoreClass_Call_Back_Delay_Fast_Forward_Patch, 5)
{
    char *this_ptr = R->ECX<char *>();

    const bool esc_down = GameInFocus && GetForegroundWindow() == MainWindow && Key_Down(VK_ESCAPE);
    if (esc_down && !ScoreEscWasDown) {
        ScoreFastForward = true;
    }
    ScoreEscWasDown = esc_down;

    if (!ScoreFastForward) {
        return 0;
    }

    /**
     *  ScoreObjs is the DynamicVectorClass at this+0x1C: Vector at +0x20, ActiveCount at +0x2C.
     */
    using DeletingDtorFn = void(__thiscall *)(void *, int);
    void **objs = *reinterpret_cast<void ***>(this_ptr + 0x20);
    int &count = *reinterpret_cast<int *>(this_ptr + 0x2C);
    for (int i = 0; i < count; ++i) {
        if (objs[i] != nullptr) {
            DeletingDtorFn dtor = **reinterpret_cast<DeletingDtorFn **>(objs[i]);
            dtor(objs[i], 1);
        }
    }
    count = 0;
    ScoreStillUpdating = false;

    return 0x005E659D;
}

DEFINE_HOOK(0x005E6201, _ScoreClass_Input_Name_Fast_Forward_Patch, 5)
{
    if (ScoreFastForward) {
        return 0x005E620C;
    }
    return 0;
}

DEFINE_HOOK(0x005E4F0B, _ScoreClass_Cycle_Wait_Click_Fast_Forward_Patch, 0)
{
    if (ScoreFastForward) {
        return 0x005E4F19;
    }
    return R->EBX<int>() == 0 ? 0x005E4E28 : 0x005E4F13;
}

/**
 *  Main function for patching the hooks.
 */
void ScoreClassExtension_Hooks()
{

}

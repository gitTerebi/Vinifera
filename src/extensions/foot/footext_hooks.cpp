/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Contains the hooks for the extended FootClass.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "footext_hooks.h"

#include "aircrafttracker.h"
#include "asserthandler.h"
#include "attackmove.h"
#include "clipline.h"
#include "coord.h"
#include "debughandler.h"
#include "extension.h"
#include "foot.h"
#include "hooker.h"
#include "house.h"
#include "infantry.h"
#include "iomap.h"
#include "ionstorm.h"
#include "levitatelocomotion.h"
#include "radarevent.h"
#include "rules.h"
#include "rulesext.h"
#include "script.h"
#include "scripttype.h"
#include "session.h"
#include "syringe.h"
#include "tag.h"
#include "tactical.h"
#include "team.h"
#include "teamext.h"
#include "teamtypeext.h"
#include "technoext.h"
#include "technotype.h"
#include "technotypeext.h"
#include "terrain.h"
#include "terraintype.h"
#include "tibsun_globals.h"
#include "tibsun_inline.h"
#include "uicontrol.h"
#include "unit.h"
#include "unitext.h"
#include "unittype.h"
#include "vinifera_globals.h"
#include "vox.h"
#include "warheadtype.h"
#include "weapontype.h"


/**
 *  A fake class for implementing new member functions which allow
 *  access to the "this" pointer of the intended class.
 *
 *  @note: This must not contain a constructor or destructor!
 *  @note: All functions must be prefixed with "_" to prevent accidental virtualization.
 */
DECLARE_EXTENDING_CLASS_AND_PAIR(FootClass)
{
public:
    void _Draw_Action_Line() const;
    void _Draw_NavComQueue_Lines() const;
    void _Death_Announcement(TechnoClass* source) const;
    Cell _Search_For_Tiberium(int rad, bool a2);
    bool _Unlimbo(const Coord& coord, Dir256 dir);
    bool _Limbo();
    int  _Do_MISSION_HUNT();
    int  _Do_MISSION_GUARD_AREA();

private:
    void _Draw_Line(Coord& start_coord, Coord& end_coord, bool is_dashed, bool is_thick, bool is_dropshadow, unsigned line_color, unsigned drop_color, int rate) const;
};


/**
 *  Draws an action line with the given parameters.
 *
 *  @author: CCHyper, ZivDero
 */
void FootClassExt::_Draw_Line(Coord& start_coord, Coord& end_coord, bool is_dashed, bool is_thick, bool is_dropshadow, unsigned line_color, unsigned drop_color, int rate) const
{
    int point_size = 3;
    Point2D point_offset(-1, -1);

    if (is_thick) {
        point_size = 4;
        point_offset = Point2D(-2, -2);
    }

    /**
     *  Convert the world coord to screen pixel.
     */
    Point2D start_point;
    Point2D end_point;
    TacticalMap->Coord_To_Pixel(start_coord, start_point);
    TacticalMap->Coord_To_Pixel(end_coord, end_point);

    /**
     *  Offset pixel position relative to tactical viewport.
     */
    start_point += Point2D(TacticalRect.X, TacticalRect.Y);
    end_point += Point2D(TacticalRect.X, TacticalRect.Y);

    /**
     *  Save the start and end points before we clip them to the viewport,
     *  so that when we draw start and end rectangles they don't show up
     *  on screen edges if they're off-screen.
     */
    Point2D start_point_unclipped = start_point;
    Point2D end_point_unclipped = end_point;

    /**
     *  Draw the queue line.
     */
    if (Clip_Line(start_point, end_point, TacticalRect)) {

        Point2D drop_start_point = start_point;
        Point2D drop_end_point = end_point;

        drop_start_point.Y += 1;
        drop_end_point.Y += 1;

        if (is_dashed) {

            /**
             *  4 pixels on, 4 off, 4 pixels on, 4 off.
             */
            static bool _pattern[] = { true, true, true, true, false, false, false, false, true, true, true, true, false, false, false, false };

            /**
             *  Adjust the offset of the line pattern.
             */
            int time = timeGetTime();
            int offset = (-time / rate) & (std::size(_pattern) - 1);

            /**
             *  Draw the drop shadow line.
             */
            if (is_dropshadow) {

                if (is_thick) {
                    drop_start_point.Y += 1;
                    drop_end_point.Y += 1;
                }

                CompositeSurface->Draw_Dashed_Line(TacticalRect, drop_start_point, drop_end_point, drop_color, _pattern, offset);

                if (is_thick) {
                    drop_start_point.Y += 1;
                    drop_end_point.Y += 1;
                    CompositeSurface->Draw_Dashed_Line(TacticalRect, drop_start_point, drop_end_point, drop_color, _pattern, offset);
                }

            }

            /**
             *  Draw the dashed queue line.
             */
            CompositeSurface->Draw_Dashed_Line(TacticalRect, start_point, end_point, line_color, _pattern, offset);

            if (is_thick) {
                start_point.Y += 1;
                end_point.Y += 1;
                CompositeSurface->Draw_Dashed_Line(TacticalRect, start_point, end_point, line_color, _pattern, offset);
            }

        }
        else {

            /**
             *  Draw the drop shadow line.
             */
            if (is_dropshadow) {

                if (is_thick) {
                    drop_start_point.Y += 1;
                    drop_end_point.Y += 1;
                }

                CompositeSurface->Draw_Line(drop_start_point, drop_end_point, drop_color);

                if (is_thick) {
                    drop_start_point.Y += 1;
                    drop_end_point.Y += 1;
                    CompositeSurface->Draw_Line(drop_start_point, drop_end_point, drop_color);
                }

            }

            /**
             *  Draw the queue line.
             */
            CompositeSurface->Draw_Line(start_point, end_point, line_color);

            if (is_thick) {
                start_point.Y += 1;
                end_point.Y += 1;
                CompositeSurface->Draw_Line(start_point, end_point, line_color);
            }

        }

    }

    /**
     *  Draw the queue line start and end squares.
     */
    if (is_dropshadow) {

        const int drop_point_size = is_thick ? (point_size + 3) : (point_size + 2);
        const Point2D drop_point_offset = is_thick ? (point_offset + Point2D(-2, -2)) : (point_offset + Point2D(-1, -1));

        if (is_thick) {
            point_size -= 1;
        }

        Rect drop_start_point_rect = Intersect(TacticalRect, Rect(start_point_unclipped + drop_point_offset, drop_point_size, drop_point_size));
        CompositeSurface->Fill_Rect(drop_start_point_rect, drop_color);

        Rect drop_end_point_rect = Intersect(TacticalRect, Rect(end_point_unclipped + drop_point_offset, drop_point_size, drop_point_size));
        CompositeSurface->Fill_Rect(drop_end_point_rect, drop_color);
    }

    Rect start_point_rect = Intersect(TacticalRect, Rect(start_point_unclipped + point_offset, point_size, point_size));
    CompositeSurface->Fill_Rect(start_point_rect, line_color);

    Rect end_point_rect = Intersect(TacticalRect, Rect(end_point_unclipped + point_offset, point_size, point_size));
    CompositeSurface->Fill_Rect(end_point_rect, line_color);
}


/**
 *  Draws a line for the current NavCom queue.
 * 
 *  @author: CCHyper, ZivDero
 */
void FootClassExt::_Draw_NavComQueue_Lines() const
{
    if (!NavCom || !NavQueue.Count()) {
        return;
    }

    /**
     *  Fetch the line properties.
     */
    const bool is_dashed = UIControls->IsNavComQueueLineDashed;
    const bool is_thick = UIControls->IsNavComQueueLineThick;
    const bool is_dropshadow = UIControls->IsNavComQueueLineDropShadow;

    const unsigned line_color = DSurface::Build_Hicolor_Pixel(
        UIControls->NavComQueueLineColor.R,
        UIControls->NavComQueueLineColor.G,
        UIControls->NavComQueueLineColor.B);

    const unsigned drop_color = DSurface::Build_Hicolor_Pixel(
        UIControls->NavComQueueLineDropShadowColor.R,
        UIControls->NavComQueueLineDropShadowColor.G,
        UIControls->NavComQueueLineDropShadowColor.B);

    /**
     *  Fetch the queue line start and end coord.
     */
    AbstractClass * start = NavCom;
    AbstractClass * end = NavQueue[0];

    Coord start_coord;
    Coord end_coord;

    for (int i = 0; i < NavQueue.Count(); i++) {

        start_coord = start->Center_Coord();

        if (Map.In_Radar(start_coord.As_Cell()) && Map[start_coord].IsUnderBridge) {
            start_coord.Z = BRIDGE_LEPTON_HEIGHT + Map.Get_Height_GL(start_coord);
        }

        end_coord = end->Center_Coord();

        if (Map.In_Radar(end_coord.As_Cell()) && Map[end_coord].IsUnderBridge) {
            end_coord.Z = BRIDGE_LEPTON_HEIGHT + Map.Get_Height_GL(end_coord);
        }

        _Draw_Line(start_coord, end_coord, is_dashed, is_thick, is_dropshadow, line_color, drop_color, 128);

        start = NavQueue[i];
        end = NavQueue[i + 1];
    }
}


/**
 *  Reimplementation of FootClass::Draw_Action_Line().
 * 
 *  @author: CCHyper, ZivDero
 */
void FootClassExt::_Draw_Action_Line() const
{
    if (!TarCom && !NavCom) {
        return;
    }

    if (!UIControls->IsAlwaysShowActionLines && ActionLineTimer.Expired() && !Keyboard->Down(Options.KeyQueueMove1) && !Keyboard->Down(Options.KeyQueueMove2)) {
        return;
    }

    /**
     *  Fetch the line properties.
     */
    const bool tarcom_is_dashed = UIControls->IsTargetLineDashed;
    const bool tarcom_is_thick = UIControls->IsTargetLineThick;
    const bool tarcom_is_dropshadow = UIControls->IsTargetLineDropShadow;

    const bool navcom_is_dashed = UIControls->IsMovementLineDashed;
    const bool navcom_is_thick = UIControls->IsMovementLineThick;
    const bool navcom_is_dropshadow = UIControls->IsMovementLineDropShadow;

    const unsigned tarcom_color = DSurface::Build_Hicolor_Pixel(
        UIControls->TargetLineColor.R,
        UIControls->TargetLineColor.G,
        UIControls->TargetLineColor.B);

    const unsigned tarcom_drop_color = DSurface::Build_Hicolor_Pixel(
        UIControls->TargetLineDropShadowColor.R,
        UIControls->TargetLineDropShadowColor.G,
        UIControls->TargetLineDropShadowColor.B);

    Cell attack_move_cell;
    const bool attack_moving = Vinifera_AttackMove_Destination(this, attack_move_cell);

    // OpenRA AttackMoveInfo.TargetLineColor is Color.OrangeRed.
    const unsigned navcom_color = attack_moving
        ? DSurface::Build_Hicolor_Pixel(255, 69, 0)
        : DSurface::Build_Hicolor_Pixel(
            UIControls->MovementLineColor.R,
            UIControls->MovementLineColor.G,
            UIControls->MovementLineColor.B);

    const unsigned navcom_drop_color = DSurface::Build_Hicolor_Pixel(
        UIControls->MovementLineDropShadowColor.R,
        UIControls->MovementLineDropShadowColor.G,
        UIControls->MovementLineDropShadowColor.B);

    /**
     *  Fetch the action line start and end coord.
     */
    Coord start_coord;
    Coord end_coord;

    if (TarCom) {

        start_coord = entry_28C();
        end_coord = func_638AF0();

        _Draw_Line(start_coord, end_coord, tarcom_is_dashed, tarcom_is_thick, tarcom_is_dropshadow, tarcom_color, tarcom_drop_color, 64);

    }

    const bool show_attack_move_dest = !NavCom && attack_moving;

    if (NavCom || show_attack_move_dest) {

        start_coord = PositionCoord;

        if (show_attack_move_dest) {
            end_coord = attack_move_cell.As_Coord();
            end_coord.Z = Map.Get_Height_GL(end_coord);
        } else {
            AbstractClass * navtarget = field_260.Count() ? field_260.Fetch_Tail() : NavCom;
            end_coord = navtarget->Center_Coord();
        }

        Cell target_cell = end_coord.As_Cell();

        if (Map.In_Radar(target_cell) && Map[end_coord].IsUnderBridge) {
            end_coord.Z = BRIDGE_LEPTON_HEIGHT + Map.Get_Height_GL(end_coord);
        }

        _Draw_Line(start_coord, end_coord, navcom_is_dashed, navcom_is_thick, navcom_is_dropshadow, navcom_color, navcom_drop_color, 128);

        if (UIControls->IsShowNavComQueueLines) {
            _Draw_NavComQueue_Lines();
        }

    }
}


/**
 *  #issue-203
 *  
 *  Evaluates the value of Tiberium on a single cell.
 *  
 *  Author: Rampastring
 */
void _Vinifera_FootClass_Search_For_Tiberium_Check_Tiberium_Value_Of_Cell(FootClass* this_ptr, Cell& cell_coords, Cell* besttiberiumcell, int* besttiberiumvalue, UnitClassExtension* unitext)
{
    if (this_ptr->Tiberium_Check(cell_coords)) {

        CellClass* cell = &Map[cell_coords];
        int tiberiumvalue = cell->Get_Tiberium_Value();

        /**
        *  #issue-203
        *
        *  Consider distance to refinery when selecting the next tiberium patch to harvest.
        *  Prefer the most resourceful tiberium patch, but if there's a tie, prefer one that's
        *  closer to our refinery. Original game only cares about the value.
        *
        *  @author: Rampastring
        */
        if (unitext && unitext->LastDockedBuilding && unitext->LastDockedBuilding->IsActive && !unitext->LastDockedBuilding->IsInLimbo) {
            tiberiumvalue *= 100;
            tiberiumvalue -= ::Distance(cell_coords, unitext->LastDockedBuilding->Get_Cell());
        }

        if (tiberiumvalue > *besttiberiumvalue)
        {
            *besttiberiumvalue = tiberiumvalue;
            *besttiberiumcell = cell_coords;
        }
    }
}


/**
 *  #issue-203
 * 
 *  Smarter replacement for the Search_For_Tiberium method.
 *  Makes harvesters consider the distance to their refinery when
 *  looking for the cell of tiberium to harvest.
 * 
 *  Author: Rampastring
 */
Cell FootClassExt::_Search_For_Tiberium(int rad, bool a2)
{
    Cell aiweightedcell = CELL_NONE;

    if (!Owner_HouseClass()->Is_Human_Player() &&
        RTTI == RTTI_UNIT &&
        ((UnitClass*)this)->Class->IsToHarvest &&
        a2 &&
        Session.Type != GAME_NORMAL)
    {
        /**
         *  Use weighted tiberium-seeking algorithm for AI in multiplayer.
         *  Record the returned cell so we can compare it to the regular algorithm's
         *  results later on.
         */

        aiweightedcell = Search_For_Tiberium_Weighted(rad);
    }

    Coord center_coord = Center_Coord();
    Cell cell_coords = center_coord.As_Cell();
    Cell unit_cell_coords = cell_coords;

    if (Map[unit_cell_coords].Land_Type() == LAND_TIBERIUM) {

        /**
         *  If we're already standing on tiberium, then we don't need to move anywhere.
         */

        return unit_cell_coords;
    }

    int besttiberiumvalue = -1;
    Cell besttiberiumcell = Cell(0, 0);

    UnitClassExtension* unitext = nullptr;
    if (RTTI == RTTI_UNIT) {
        unitext = Extension::Fetch(reinterpret_cast<UnitClass*>(this));
    }

    /**
     *  Perform a ring search outward from the center.
     */
    for (int radius = 1; radius < rad; radius++) {
        for (int x = -radius; x <= radius; x++) {

            cell_coords = Cell(unit_cell_coords.X + x, unit_cell_coords.Y - radius);
            _Vinifera_FootClass_Search_For_Tiberium_Check_Tiberium_Value_Of_Cell(this, cell_coords, &besttiberiumcell, &besttiberiumvalue, unitext);

            cell_coords = Cell(unit_cell_coords.X + x, unit_cell_coords.Y + radius);
            _Vinifera_FootClass_Search_For_Tiberium_Check_Tiberium_Value_Of_Cell(this, cell_coords, &besttiberiumcell, &besttiberiumvalue, unitext);

            cell_coords = Cell(unit_cell_coords.X - radius, unit_cell_coords.Y + x);
            _Vinifera_FootClass_Search_For_Tiberium_Check_Tiberium_Value_Of_Cell(this, cell_coords, &besttiberiumcell, &besttiberiumvalue, unitext);

            cell_coords = Cell(unit_cell_coords.X + radius, unit_cell_coords.Y + x);
            _Vinifera_FootClass_Search_For_Tiberium_Check_Tiberium_Value_Of_Cell(this, cell_coords, &besttiberiumcell, &besttiberiumvalue, unitext);
        }

        if (besttiberiumvalue != -1)
            break;
    }
    
    if (aiweightedcell == CELL_NONE)
        return besttiberiumcell;

    if (besttiberiumcell == CELL_NONE)
        return aiweightedcell;

    /**
     *  The special AI algorithm is good for finding richer distant tiberium fields, 
     *  such as blue tiberium and gems that the regular search might not be able to find.
     *  However, if both have found the same kind of tiberium, the regular algorithm likely
     *  gave a closer result.
     */
    if (Map[aiweightedcell].Overlay == Map[besttiberiumcell].Overlay)
        return besttiberiumcell;

    int distanceaiweighted = Distance(aiweightedcell.As_Coord());
    int distanceregular = Distance(besttiberiumcell.As_Coord());

    if (distanceaiweighted > distanceregular * 2 && distanceaiweighted > distanceregular + (CELL_LEPTON * 6))
        return besttiberiumcell;

    return aiweightedcell;
}


/**
 *  #issue-593
 * 
 *  Implements IsCanPassiveAcquire for TechnoTypes when the unit is in MISSION_MOVE.
 * 
 *  @author: CCHyper
 */
DEFINE_HOOK(0x004A102F, _FootClass_Mission_Move_Can_Passive_Acquire_Patch, 0)
{
    GET(FootClass *, this_ptr, ESI);

    auto technoclassext = Extension::Fetch(this_ptr);

    /**
     *  Can this unit passively acquire new targets?
     */
    if (!technoclassext->Can_Passive_Acquire()) {
        goto finish_mission_process;
    }

    /**
     *  Find a fresh target within my range.
     */
    this_ptr->Target_Something_Nearby(this_ptr->PositionCoord, THREAT_RANGE);

finish_mission_process:
    return 0x004A104B;
}


/**
 *  #issue-593
 * 
 *  Implements IsCanPassiveAcquire for TechnoTypes when the unit is in MISSION_GUARD.
 * 
 *  @author: CCHyper
 */
DEFINE_HOOK(0x004A1AAE, _FootClass_Mission_Guard_Can_Passive_Acquire_Patch, 0)
{
    GET(FootClass *, this_ptr, ESI);

    auto technoclassext = Extension::Fetch(this_ptr);

    /**
     *  Can this unit passively acquire new targets?
     */
    if (!technoclassext->Can_Passive_Acquire()) {
        goto continue_check;
    }

    /**
     *  Find a fresh target within my range.
     */
    if (!this_ptr->Target_Something_Nearby(this_ptr->PositionCoord, THREAT_RANGE)) {
        goto random_animate;
    }

continue_check:
    return 0x004A1AD6;

random_animate:
    return 0x004A1ACC;
}


/**
 *  #issue-593
 * 
 *  Implements IsCanPassiveAcquire for TechnoTypes when the unit is in MISSION_GUARD_AREA.
 * 
 *  @author: CCHyper
 */
DEFINE_HOOK(0x004A2BE7, _FootClass_Mission_Guard_Area_Can_Passive_Acquire_Patch, 0)
{
    GET(FootClass *, this_ptr, ESI);

    auto technoclassext = Extension::Fetch(this_ptr);

    /**
     *  Can this unit passively acquire new targets?
     */
    if (!technoclassext->Can_Passive_Acquire()) {
        goto tarcom_check;
    }

    /**
     *  Find a fresh target in my area using the backup target.
     */
    if (this_ptr->ArchiveTarget != nullptr) {
        this_ptr->Target_Something_Nearby(this_ptr->ArchiveTarget->Center_Coord(), THREAT_AREA);
    }

tarcom_check:
    return 0x004A2C04;
}


/**
 *  #issue-421
 * 
 *  Implements IdleRate for TechnoTypes.
 * 
 *  @author: CCHyper
 */
DEFINE_HOOK(0x004A59E1, _FootClass_AI_IdleRate_Patch, 0)
{
    GET(FootClass *, this_ptr, ESI);
    GET(ILocomotion *, loco, EDI);

    auto technotypeext = Extension::Fetch(this_ptr->TClass);

    /**
     *  Stolen bytes/code.
     * 
     *  If the object is currently moving, check to see if its time to update its walk frame.
     */
    if (this_ptr->Locomotion->Is_Moving_Now() && !(Frame % this_ptr->TClass->WalkRate)) {
        ++this_ptr->TotalFramesWalked;

    /**
     *  Otherwise, if the object is not currently moving, check to see if its time to update its idle frame.
     */
    } else if (technotypeext->IdleRate > 0) {
        if (!this_ptr->Locomotion->Is_Moving_Now() && !(Frame % technotypeext->IdleRate)) {
            ++this_ptr->TotalFramesWalked;
        }
    }

    return 0x004A5A12;
}


/**
 *  #issue-404
 * 
 *  A object with "CloakStop" set has no effect on the cloaking behavior.
 * 
 *  @author: CCHyper
 */
DEFINE_HOOK(0x004A6866, _FootClass_Is_Allowed_To_Recloak_Cloak_Stop_BugFix_Patch, 0)
{
    GET(FootClass *, this_ptr, ESI);
    GET(TechnoTypeClass *, technotype, EAX);

    /**
     *  Is this unit flagged to only re-cloak when not moving?
     */
    if (technotype->CloakStop) {

        /**
         *  If the object is currently moving, then return false.
         * 
         *  The original code here called Is_Moving_Now, which returned
         *  false when the locomotor was on a slope or rotating, which
         *  breaks the CloakStop mechanic.
         */
        if (this_ptr->Locomotion->Is_Moving()) {
            goto return_false;
        }
    }

    /**
     *  The unit can re-cloak.
     */
return_true:
    return 0x004A6897;

    /**
     *  The unit is not allowed to re-cloak.
     */
return_false:
    return 0x004A689B;
}


/**
 *  Announces the death of a unit.
 *
 *  @author: 07/01/1995 JLB - Created.
 *           ZivDero - Adjustments for Tiberian Sun
 */
void FootClassExt::_Death_Announcement(TechnoClass* source) const
{
    if (IsOwnedByPlayer) {

        const auto is_spawned = Extension::Fetch(TClass)->IsSpawned;
        if (!TClass->IsInsignificant && !is_spawned) {

            RadarEventClass::LastEventCell = entry_50().As_Cell();
            Speak(VOX_UNIT_LOST);
        }
    }
}


/**
 *  FootClass::Unlimbo replacement.
 *
 *  @author: ZivDero
 */
bool FootClassExt::_Unlimbo(const Coord& coord, Dir256 dir)
{
    /**
     *  Try to unlimbo the unit.
     */
    if (TechnoClass::Unlimbo(coord, dir)) {

        Locomotion->Unlimbo();

        bool off = false;
        // if (IonStorm_Is_Active()) {
        //     if (Locomotion->Is_Ion_Sensitive()) {
        //         off = true;
        //     }
        // }

        if (off) {
            Locomotion->Power_Off();
        } else {
            Locomotion->Power_On();
        }

        /**
         *  Instead of patching levitate locomotion to add to tracking, since levitate locomotion is
         *  always in flight let's add it right now.
         */
        if (TClass->Locomotor == __uuidof(LevitateLocomotionClass)) {
            AircraftTracker->Track(this);
        }

        /**
         *  Mobile units are always revealed to the house that owns them.
         */
        Revealed(House);

        /**
         *  Start in a still (non-moving) state.
         */
        Path[0] = FACING_NONE;

        Cell cell = Position.As_Cell();
        for (int face = FACING_FIRST; face < FACING_COUNT; face++) {
            Cell c = Adjacent_Cell(cell, FacingType(face));
            CellClass* cptr = &Map[c];
            cptr->AdjacentObjectCount++;
        }

        if (!In_Air()) {
            LastAdjacencyCell = cell;
        }

        ThreatAvoidanceCoefficient = TClass->ThreatAvoidanceCoefficient;
        return true;
    }
    return false;
}


/**
 *  FootClass::Limbo replacement.
 *
 *  @author: ZivDero
 */
bool FootClassExt::_Limbo()
{
    if (!IsInLimbo) {
        Cell cell = LastAdjacencyCell;
        for (FacingType face = FACING_FIRST; face < FACING_COUNT; face++) {
            Cell newcell = Adjacent_Cell(cell, face);
            CellClass* cptr = &Map[newcell];
            cptr->AdjacentObjectCount--;
        }
        Stop_Driver();
        if (Locomotion != nullptr) {
            Locomotion->Mark_All_Occupation_Bits(MARK_UP);
        }

        /**
         *  Remove the object from the aircraft tracker.
         */
        const auto ext = Extension::Fetch(this);
        if (ext->Get_Last_Flight_Cell() != CELL_NONE) {
            AircraftTracker->Untrack(this);
        }
    }
    return TechnoClass::Limbo();
}


/**
 *  Patches FootClass::Active_Click_With inside the 'ACTION_ATTACK_SUPPORT' case.
 *  Makes healing units (negative damage) prefer to guard other units instead of themselves.
 *  Healing units will guard other combatants, if any, and will assign themselves to the unit closest to them.
 *  
 *  If they can't find any, or they are not healing units, then they'll simply guard themselves instead.
 *
 *  @author: JoyfulShush
 */
DEFINE_HOOK(0x004A3518, _FootClass_Active_Click_With_Attack_Support_Patch, 0)
{
    GET(FootClass*, this_ptr, ESI);
    bool has_negative_damage = R->AL();

    ObjectClass* guard_object = this_ptr;
    MissionType mission = MISSION_GUARD;    
    if (has_negative_damage) {
        mission = MISSION_GUARD_AREA;
        for (int i = 0; i < CurrentObjects.Count(); ++i) {
            ObjectClass* object = CurrentObjects[i];
            if (!object || object == this_ptr || !object->Is_Techno() || object->Fetch_RTTI() == RTTI_BUILDING) {
                continue;
            }

            TechnoClass* techno = static_cast<TechnoClass*>(object);
            if (techno->Combat_Damage() <= 0) {
                continue;
            }

            if (techno->House->Is_Player_Control() && techno->House == this_ptr->House) {
                // if the object that is currently assigned is the unit doing the guard, simply assign it an eligible unit to guard
                if (guard_object == this_ptr) {
                    guard_object = object;
                } else {
                    if (this_ptr->Distance_To(object) < this_ptr->Distance_To(guard_object)) {
                        guard_object = object;
                    }
                }
            }
        }
    }

    this_ptr->Player_Assign_Mission(mission, guard_object, nullptr);

    return 0x004A2F95;
}


// Copy of Target_Something_Nearby that does not ignore ThreatTypes other than the range specifiers.
bool Respectable_Target_Something_Nearby(FootClassExt* foot, Coord & coord, ThreatType threat)
{
    /*
    **	Determine that if there is an existing target it is still legal
    **	and within range.
    */
    if (foot->TarCom != nullptr) {
        if ((threat & THREAT_RANGE)) {
            WeaponSlotType primary = foot->What_Weapon_Should_I_Use(foot->TarCom);
            if (!foot->In_Range(foot->TarCom->Center_Coord(), primary)) {
                foot->Assign_Target(nullptr);
            }
        }
    }

    /*
    **	If there is no target, then try to find one and assign it as
    **	the target for this unit.
    */
    if (foot->TarCom == nullptr) {
        foot->Assign_Target(foot->Greatest_Threat(threat, coord, false));
    }

    /*
    **	Return with answer to question: Does this unit now have a target?
    */
    return(foot->TarCom != nullptr);
}


/*
**	Implements smart hunt logic.
**
**  Author: tomsons26/ZivDero for original decompiled code, Rampastring for smart hunt logic.
*/
int FootClassExt::_Do_MISSION_HUNT()
{
    int delay = Current_Mission_Control().Normal_Delay();
    bool smarthunt = RuleExtension->AdvancedAISmartHunt;

    if (smarthunt && Team != nullptr)
    {
        auto teamtypeext = Extension::Fetch(Team->Class);
        smarthunt = teamtypeext->SmartHunt;
    }

    if (smarthunt) {
        // First, try to target something in range.
        // Prefer focusing on targets that are actually threatening - mobile objects and base defenses.

        ThreatType threat = THREAT_RANGE | THREAT_INFANTRY | THREAT_VEHICLES | THREAT_BASE_DEFENSE;
        if (Anti_Air() > 0) {
            threat = threat | THREAT_AIR;
        }

        if (!Respectable_Target_Something_Nearby(this, PositionCoord, threat)) {

            delay += 7 + Random_Pick(0, 5);

            // If it failed, then try to target something far away.
            if (!Respectable_Target_Something_Nearby(this, PositionCoord, THREAT_NORMAL)) {

                // If there's truly nothing to target, we're done.
                Random_Animate();
                return delay + TICKS_PER_SECOND + Random_Pick(0, 5);
            }
        }
    } else {
        // Without smart hunt, use original game code.

        if (!Target_Something_Nearby(PositionCoord, THREAT_NORMAL)) {

            Random_Animate();
            return delay + Random_Pick(0, 2);
        }
    }

    InfantryClass* infantry = RTTI == RTTI_INFANTRY ? (InfantryClass*)this : NULL;
    if (infantry != nullptr && infantry->Class->IsEngineer && !infantry->Class->IsBomber && !infantry->Has_Ability(ABILITY_C4)) {
        Assign_Destination(TarCom);
        Assign_Mission(MISSION_CAPTURE);
        if (Ready_To_Commence()) {
            Commence();
        }
    }
    else if (infantry != nullptr && (infantry->Class->IsBomber || infantry->Has_Ability(ABILITY_C4)) && dynamic_cast<BuildingClass*>(TarCom)) {
        Assign_Destination(TarCom);
        Assign_Mission(MISSION_SABOTAGE);
        if (Ready_To_Commence()) {
            Commence();
        }
    }
    else if (infantry != nullptr && infantry->Class->IsVehicleThief) {
        Assign_Destination(TarCom);
        Assign_Mission(MISSION_CAPTURE);
        if (Ready_To_Commence()) {
            Commence();
        }
    }
    else {
        bool approach_target = true;

        if (smarthunt)
        {
            Coord targetcoord = TarCom->Center_Coord();
            WeaponSlotType weaponslot = What_Weapon_Should_I_Use(TarCom);
            const WeaponTypeClass* ourweapon = TClass->Fetch_Weapon_Info(weaponslot).Weapon;

            if (ourweapon != nullptr)
            {
                bool inrange = In_Range(targetcoord, weaponslot);

                if (inrange)
                {
                    int targetspeed = 0;
                    int targetrange = 0;
                    WeaponSlotType theirweaponslot = WEAPON_SLOT_PRIMARY;
                    TechnoClass* tarcom_as_techno = ::As_Techno(TarCom);

                    if (tarcom_as_techno != nullptr) {
                        targetspeed = tarcom_as_techno->TClass->MaxSpeed;

                        theirweaponslot = tarcom_as_techno->What_Weapon_Should_I_Use(this);

                        if (theirweaponslot != WEAPON_SLOT_NONE) {
                            const WeaponTypeClass* theirweapon = tarcom_as_techno->TClass->Fetch_Weapon_Info(theirweaponslot).Weapon;
                            if (theirweapon != nullptr) {
                                targetrange = theirweapon->Range;
                            }
                        }
                    }

                    int ourrange = ourweapon->Range;

                    if (RTTI == RTTI_UNIT) 
                    {
                        const UnitTypeClass* unittype = reinterpret_cast<const UnitTypeClass*>(TClass);
                        bool cankite = ((!unittype->IsNoFireWhileMoving && unittype->IsTurretEquipped) || (!Arm.Expired() && ourweapon->ROF > TICKS_PER_SECOND && unittype->MaxSpeed > targetspeed))
                            && ourrange >= targetrange + CELL_LEPTON
                            && In_Range(targetcoord, weaponslot)
                            && tarcom_as_techno != nullptr;

                        // If the target is immobile and we already outrange it, there is no need to kite.
                        if (cankite && !::Is_Foot(TarCom)) {
                            if (targetrange <= 0) {
                                cankite = false;
                            }
                            else if (!reinterpret_cast<TechnoClass*>(TarCom)->In_Range(Center_Coord(), theirweaponslot)) {
                                cankite = false;
                            }
                        }

                        if (cankite)
                        {
                            // Allow the AI to kite if it's not too much for the player's skill level.
                            int kitechance = RuleExtension->AIKiteChance[House->Difficulty];

                            if (kitechance > 0 && (kitechance >= 100 || Percent_Chance(kitechance)))
                            {
                                // If the target is in range, we can fire while moving AND we outrange the target by at least a cell,
                                // try to maximize our distance to the target.

                                int distance = Distance(targetcoord);
                                int distancediff = std::abs(ourweapon->Range - distance);

                                if (tarcom_as_techno != nullptr)
                                {
                                    Dir256 dir = Direction256(targetcoord, PositionCoord);
                                    Cell maximumRangeCell = Coord_Move(targetcoord, DirType(dir), ourweapon->Range).As_Cell();

                                    Cell nearbyloc = Map.Nearby_Location(maximumRangeCell, TClass->Speed, Map.Get_Cell_Zone(PositionCell, TClass->MZone, IsOnBridge), TClass->MZone, false, Point2D(1, 1), false, false, false, true, maximumRangeCell);
                                    if (nearbyloc != CELL_NONE) {
                                        Assign_Destination(&Map[nearbyloc]);
                                        approach_target = false;
                                    }
                                }
                            }
                        }

                        // If we didn't end up kiting, just stop so we can fire - we are already in range.
                        if (approach_target) {
                            Assign_Destination(nullptr);
                            approach_target = false;
                        }
                    }
                    else if (RTTI == RTTI_INFANTRY)
                    {
                        // If we are significantly longer-ranged and faster than the enemy, we can kite them.
                        bool cankite = (!Arm.Expired() && TClass->MaxSpeed > targetspeed)
                            && ourrange >= targetrange + CELL_LEPTON
                            && In_Range(targetcoord, weaponslot)
                            && tarcom_as_techno != nullptr;

                        // If the target is immobile and we already outrange it, there is no need to kite.
                        if (cankite && !::Is_Foot(TarCom)) {
                            if (targetrange <= 0) {
                                cankite = false;
                            }
                            else if (!reinterpret_cast<TechnoClass*>(TarCom)->In_Range(Center_Coord(), theirweaponslot)) {
                                cankite = false;
                            }
                        }

                        if (cankite)
                        {
                            // Allow the AI to kite if it's not too much for the player's skill level.
                            int kitechance = RuleExtension->AIKiteChance[House->Difficulty];

                            if (kitechance > 0 && (kitechance >= 100 || Percent_Chance(kitechance)))
                            {
                                // If the target is in range, we can fire while moving AND we outrange the target by at least a cell,
                                // try to maximize our distance to the target.

                                int distance = Distance(targetcoord);
                                int distancediff = std::abs(ourweapon->Range - distance);

                                if (tarcom_as_techno != nullptr)
                                {
                                    Dir256 dir = Direction256(targetcoord, PositionCoord);
                                    Cell maximumRangeCell = Coord_Move(targetcoord, DirType(dir), ourweapon->Range).As_Cell();

                                    Cell nearbyloc = Map.Nearby_Location(maximumRangeCell, TClass->Speed, Map.Get_Cell_Zone(PositionCell, TClass->MZone, IsOnBridge), TClass->MZone, false, Point2D(1, 1), false, false, false, true, maximumRangeCell);
                                    if (nearbyloc != CELL_NONE) {
                                        Assign_Destination(&Map[nearbyloc]);
                                        approach_target = false;
                                    }
                                }
                            }
                        }

                        // If we didn't end up kiting, just stop so we can fire - we are already in range.
                        if (approach_target) {
                            Assign_Destination(nullptr);
                            approach_target = false;
                        }
                    }
                }
                else
                {
                    int dist = ::Distance(Center_Coord(), targetcoord);
                    if (dist > ourweapon->Range * 3 && dist > CELL_LEPTON * 20)
                        delay += TICKS_PER_SECOND * 6 + Random_Pick(0, 30);
                    else
                        delay += TICKS_PER_SECOND + Random_Pick(0, 2);
                }
            }
        }
        
        if (approach_target) {
            Approach_Target();
        }
    }

    return delay;
}

void FootClass_AI_Stuck_In_WarFactory_Check(FootClass* foot)
{
    if (foot->RTTI == RTTI_UNIT && foot->NavCom == nullptr && foot->Mission != MISSION_MOVE)
    {
        BuildingClass* bldg = Map[foot->Center_Coord()].Cell_Building();

        if (bldg != nullptr && bldg->Class->IsWeaponsFactory) {
            FootClassExtension* footext = Extension::Fetch(foot);

            if (footext->WFStuckFrame <= 0) {
                footext->WFStuckFrame = Frame;
            }
            else if (Frame > footext->WFStuckFrame + 1000) {
                DEBUG_INFO("FootClass::AI: scattering unit that has been stuck in a war factory for too long\n");
                footext->WFStuckFrame = Frame;
                foot->Scatter(Coord(-1, -1, -1), true, true);
            }
        }
    }
}

void FootClass_AI_Abandon_Invalid_Targets(FootClass* this_ptr)
{
    if (!this_ptr->House->Is_Human_Player())
    {
        if (this_ptr->TarCom != nullptr)
        {
            if (this_ptr->TarCom->RTTI == RTTI_CELL) {
                if (reinterpret_cast<CellClass*>(this_ptr->TarCom)->Overlay == OVERLAY_NONE) {

                    // Make an exception for attacking waypoints.
                    if (this_ptr->Team != nullptr && this_ptr->Team->Script->CurrentMission > -1 &&
                        this_ptr->Team->Script->CurrentMission < this_ptr->Team->Script->Class->MissionCount &&
                        this_ptr->Team->Script->Class->MissionList[this_ptr->Team->Script->CurrentMission].Mission == SMISSION_ATT_WAYPT)
                    {
                        return;
                    }

                    this_ptr->Assign_Target(nullptr);
                }
            }
            else if (this_ptr->TarCom->RTTI == RTTI_TERRAIN) 
            {
                // Also stop firing if we are firing at a tree, but don't have a weapon that could destroy wood.
                // OR if the tree is indestructible.

                if (reinterpret_cast<TerrainClass*>(this_ptr->TarCom)->Class->IsImmune) {
                    this_ptr->Assign_Target(nullptr);
                }
                else {
                    WeaponSlotType wslot = this_ptr->What_Weapon_Should_I_Use(this_ptr->TarCom);

                    if (wslot == WEAPON_SLOT_NONE || !this_ptr->TClass->Fetch_Weapon_Info(wslot).Weapon->WarheadPtr->IsWoodDestroyer) {
                        this_ptr->Assign_Target(nullptr);
                    }
                }
            }
        }
    }
}

/**
 *  There is a bug in the game where sometimes AI-owned units start firing at a wall,
 *  but don't stop firing once the wall has been destroyed.
 * 
 *  This patch makes the AI abandon the target if it's firing at a cell that has no overlay on it.
 *  It hooks the beginning of FootClass::AI, making it a good place for putting further per-frame
 *  patches if necessary.
 *
 *  Author: Rampastring
 */
DEFINE_HOOK(0x004A58A8, _FootClass_AI_Hook, 0)
{
    GET(FootClass*, this_ptr, ESI);

    FootClass_AI_Abandon_Invalid_Targets(this_ptr);

    Vinifera_AttackMove_AI(this_ptr);

    // FootClass_AI_Stuck_In_WarFactory_Check(this_ptr);

    // Restore stolen bytes / code
    this_ptr->field_34A = false;

    if (this_ptr->Techno_Type_Class()->IsTiberiumHeal) {
        return 0x004A58D4;
    }

    return 0x004A58C3;
}


/*
**	Adjusts area guard mode for Advanced AI.
**
**  Author: Rampastring, ZivDero, Shush
*/
int FootClassExt::_Do_MISSION_GUARD_AREA()
{
    TechnoTypeClassExtension* technotype_ext = Extension::Fetch(TClass);


    if (!House->Is_Human_Player() && NavQueue.Count() > 0 && NavCom == nullptr && NavQueue[0] == ArchiveTarget) {
        AbstractClass* first = NavQueue[0];
        if (first != nullptr) {
            Assign_Destination(first);
            NavQueue.Delete(0);
            if (IsNavQueueLoop) {
                NavQueue.Add(first);
            }
        }
    }

    /*
    **
    */
    if (ArchiveTarget != nullptr && ArchiveTarget->RTTI == RTTI_CELL) {

        BuildingClass* building = Map[ArchiveTarget->Center_Coord()].Cell_Building();
        if (building != nullptr && House->Is_Ally(building->House) && !House->Is_Human_Player() && (RTTI != RTTI_UNIT || !reinterpret_cast<UnitClass*>(this)->Class->IsToHarvest)) {

            Cell nearby = Map.Nearby_Location(PositionCell, SPEED_TRACK, Map.Get_Cell_Zone((Cell)ArchiveTarget->Center_Coord()));
            if (nearby != CELL_NONE) {
                Assign_Archive_Target(&Map[nearby]);
            }
        }
    }

    if (RTTI == RTTI_UNIT && reinterpret_cast<UnitClass*>(this)->Class->IsToHarvest) {
        Assign_Mission(MISSION_HARVEST);
        Commence();
        return(1 + Random_Pick(1, 10));
    }

    /*
    **	Ensure that the archive target is valid.
    */
    if (ArchiveTarget == nullptr && MissionQueue == MISSION_NONE) {
        Assign_Archive_Target(&Map[(Coord&)PositionCoord]);
    }

    /*
    **	If this is a bomber type infantry and the current target is a building, then go into
    **	sabotage mode if not already.
    */
    // TODO dynamic_cast here should be As_InfantryClass and As_BuildingClass but doesn't match..
    InfantryClass* infantry = dynamic_cast<InfantryClass*>(this);
    if (!House->Is_Human_Player() && infantry != NULL && (infantry->Class->IsBomber || infantry->Has_Ability(ABILITY_C4)) && Mission != MISSION_SABOTAGE && dynamic_cast<BuildingClass*>(TarCom) != NULL) {
        Assign_Mission(MISSION_SABOTAGE);
        return(1);
    }

    /*
    **	Make sure that the unit has not strayed too far from the home position.
    **	If it has, then race back to it. Do not impose a default pursuit limit
    **	while the unit is moving towards a target: area-guard acquisition uses
    **	straight-line distance, while terrain can make the actual path much longer
    **	and cause an abandon/re-acquire loop.
    */
    int maxrange = (int)(Threat_Range(1) * 0.75);

    if (ArchiveTarget != nullptr) {
        const bool is_escorting = ArchiveTarget->Fetch_RTTI() != RTTI_CELL;
        int escort_range = -1;
        int abandon_target_escort_range = -1;

        if (is_escorting) {
            if (technotype_ext->EscortRange > 0) {
                escort_range = technotype_ext->EscortRange;
            } else if (RuleExtension->EscortRange > 0) {
                escort_range = RuleExtension->EscortRange;
            }

            if (technotype_ext->AbandonTargetEscortRange > 0) {
                abandon_target_escort_range = technotype_ext->AbandonTargetEscortRange;
            } else if (RuleExtension->AbandonTargetEscortRange > 0) {
                abandon_target_escort_range = RuleExtension->AbandonTargetEscortRange;
            }
        }

        // Advanced AI: Invalidate our target if it is not in range.
        if (!House->Is_Human_Player() && RuleExtension->AdvancedAIAreaGuard && Team != nullptr && Extension::Fetch(Team)->IsAdvAITeam && TarCom != nullptr && !In_Range_Of(TarCom))
        {
            Assign_Target(nullptr);
        }

        if (!IsFiring && NavCom == nullptr) {
            bool should_return_to_archive_target = false;
            if (TarCom == nullptr) {
                if (escort_range > 0) {
                    should_return_to_archive_target = Distance_To(ArchiveTarget) >= escort_range;
                } else {
                    should_return_to_archive_target = Distance(ArchiveTarget) > maxrange;
                }
            } else if (abandon_target_escort_range <= 0 &&
                (!RuleExtension->AdvancedAIAreaGuard || House->Is_Human_Player())) {
                should_return_to_archive_target = Distance(ArchiveTarget) > maxrange;
            }

            if (should_return_to_archive_target) {
                Assign_Target(nullptr);
                Assign_Destination(ArchiveTarget);
            }
        }

        if (TarCom == nullptr) {
            Target_Something_Nearby(ArchiveTarget->Center_Coord(), THREAT_RANGE);

            if (TarCom == nullptr) {

                Target_Something_Nearby(ArchiveTarget->Center_Coord(), THREAT_AREA);

                if (TarCom != nullptr) {
                    return(1);
                }

                Random_Animate();
            }
            else
            {
                return(1);
            }
        }
        else
        {
            bool should_abandon_target = abandon_target_escort_range > 0 &&
                Distance_To(ArchiveTarget) >= abandon_target_escort_range;

            if (should_abandon_target) {
                Assign_Target(nullptr);
                Assign_Destination(ArchiveTarget);
            } else {
                Approach_Target();
            }
        }
    }

    int dtime = Current_Mission_Control().Normal_Delay();
    if (RTTI == RTTI_AIRCRAFT) {
        dtime *= 2;
    }
    return(dtime + Random_Pick(1, 5));
}


/**
 *  #issue-202
 *
 *  For harvester queue jumping.
 *  Make harvesters seek for a new refinery to unload into when their
 *  existing refinery has dumped them for a different harvester.
 *
 *  @author: Rampastring
 */
DEFINE_HOOK(0x004A49A3, _FootClass_Mission_Enter_Seek_New_Refinery_After_Dropped, 0)
{
    GET(FootClass*, this_ptr, ESI);

    /**
     *  Check if we're a harvester.
     */
    if (this_ptr->What_Am_I() == RTTI_UNIT) {

        UnitTypeClass* unittype = reinterpret_cast<UnitClass*>(this_ptr)->Class;

        if (!this_ptr->House->Is_Human_Player() && (unittype->IsToHarvest || unittype->IsToVeinHarvest)) {

            /**
             *  We're a harvester, try to find a new refinery instead of going idle.
             */
            this_ptr->Assign_Mission(MISSION_HARVEST);
            return 0x004A49B1;
        }
    }

    /**
     *  Put the object into idle mode and continue on to commencing the mission.
     */
    this_ptr->Enter_Idle_Mode(false, true);

    /**
     *  Commences the given mission and exits the function afterwards.
     */
    return 0x004A49B1;
}


/**
 *  Patches FootClass::Per_Cell_Process inside the cloak check.
 *  Trigger cell tags via the TEVENT_PLAYER_ENTERED trigger event.
 *  Cell Tags can only be sprung by cloaked units if the 'CloakedTechnosTriggerCellTags' key is set in rules under [General].
 * 
 *  @author: JoyfulShush
 */
DEFINE_HOOK(0x004A3E25, _FootClass_Spring_Entered_By_Cloaked_Units_Patch, 6)
{
    GET(FootClass*, this_ptr, ESI);
        
    if (!RuleExtension->IsCellTagsIgnoreStealth) {
        CellClass* cellptr = &Map[this_ptr->PositionCell];
        TagClass* tag = cellptr->CellTag;

        if (((!cellptr->IsUnderBridge && !cellptr->WasUnderBridge) || this_ptr->IsOnBridge) && tag != nullptr) {
            tag->Spring(TEVENT_PLAYER_ENTERED, this_ptr, this_ptr->PositionCell);
        }
    }    

    return 0;
}

/**
 *  #issue-177
 *
 *  Patches the harvester counting to count all units listed under HarvesterUnit.
 *
 *  @author: ZivDero
 */
DEFINE_HOOK(0x004A7A3F, _FootClass_Search_For_Tiberium_Weighted_HarvesterUnit_Patch, 0)
{
    GET(FootClass *, this_ptr, EDI);

    int count = this_ptr->House->Count_Owned(Rule->HarvesterUnit);
    R->EAX(count);

    return 0x004A7A65;
}


/**
 *  Main function for patching the hooks.
 */
void FootClassExtension_Hooks()
{
    Patch_Jump(0x004A6A40, &FootClassExt::_Draw_Action_Line);
    Patch_Jump(0x004A4D60, &FootClassExt::_Death_Announcement);
    Patch_Jump(0x004A76F0, &FootClassExt::_Search_For_Tiberium);
    Patch_Jump(0x004A2C70, &FootClassExt::_Unlimbo);
    Patch_Jump(0x004A5E80, &FootClassExt::_Limbo);
    Patch_Jump(0x004A1BE0, &FootClassExt::_Do_MISSION_HUNT);
    Patch_Jump(0x004A2830, &FootClassExt::_Do_MISSION_GUARD_AREA);
}

/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Contains the hooks for the extended HouseClass.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "houseext_hooks.h"

#include "aircraft.h"
#include "aircrafttypeext.h"
#include "asserthandler.h"
#include "building.h"
#include "buildingext.h"
#include "buildingtypeext.h"
#include "ccini.h"
#include "debughandler.h"
#include "extension_globals.h"
#include "factory.h"
#include "fatal.h"
#include "fetchres.h"
#include "hooker.h"
#include "hooker_macros.h"
#include "house.h"
#include "houseext.h"
#include "houseext_init.h"
#include "housetype.h"
#include "infantry.h"
#include "infantrytypeext.h"
#include "language.h"
#include "logic.h"
#include "mouse.h"
#include "msgbox.h"
#include "prerequisitegroup.h"
#include "rules.h"
#include "rulesext.h"
#include "scenarioext.h"
#include "session.h"
#include "sessionext.h"
#include "sideext.h"
#include "spawner.h"
#include "super.h"
#include "syringe.h"
#include "team.h"
#include "teamext.h"
#include "teamtype.h"
#include "techno.h"
#include "technoext.h"
#include "technotype.h"
#include "terrain.h"
#include "terraintype.h"
#include "tiberium.h"
#include "tibsun_functions.h"
#include "tibsun_globals.h"
#include "unit.h"
#include "unittype.h"
#include "unittypeext.h"
#include "verses.h"
#include "vinifera_globals.h"
#include "vinifera_util.h"
#include "vox.h"
#include "weapontype.h"


bool AdvAI_House_Search_For_Next_Expansion_Point(HouseClass* house)
{
    HouseClassExtension* ext = Extension::Fetch(house);

    if (ext->NextExpansionPointLocation.X != 0 && ext->NextExpansionPointLocation.Y != 0) {
        return false;
    }

    // Fetch our first ConYard.
    BuildingClass* firstbuilding = nullptr;
    for (int i = 0; i < Buildings.Count(); i++) {
        BuildingClass* building = Buildings[i];
        if (building->IsActive && !building->IsInLimbo && building->House == house && building->Class->ToBuild == RTTI_BUILDINGTYPE) {
            firstbuilding = Buildings[i];
            break;
        }
    }

    if (firstbuilding == nullptr) {
        return false;
    }

    // Scan through terrain objects that spawn Tiberium, pick the closest one that does not have a refinery near it yet
    int nearestdistance = INT_MAX;
    Cell target = Cell(0, 0);

    int conyardzone = Map.Get_Cell_Zone(firstbuilding->PositionCell, MZONE_NORMAL);

    for (int i = 0; i < Terrains.Count(); i++) {
        TerrainClass* terrain = Terrains[i];
        if (terrain->IsActive && !terrain->IsInLimbo && terrain->Class->IsTiberiumSpawn) {

            Cell terraincell = terrain->Get_Cell();

            // Fetch the cell of the terrain. If the cell has overlay on it,
            // we should not expand towards it. This allows a way for mappers to mark
            // that the AI should not expand towards specific Tiberium trees.
            CellClass& cell = Map[terraincell];
            if (cell.Overlay != OVERLAY_NONE) {
                continue;
            }

            // If the Tiberium tree is not on the same zone with our ConYard,
            // we cannot expand towards it.
            // Doesn't work, tibtrees alter zone values
            // int tibtreezone = Map.Get_Cell_Zone(cell.CellID, MZONE_NORMAL);
            // if (conyardzone != tibtreezone) {
            //     continue;
            // }

            bool found = false;
            for (int j = 0; j < Buildings.Count(); j++) {
                BuildingClass* building = Buildings[j];

                if (!building->IsActive || building->IsInLimbo || !building->Class->IsRefinery) {
                    continue;
                }

                // Check if any existing AI refinery has been assigned for this expansion point yet.
                // If yes, consider it occupied, but only if it is ours.
                BuildingClassExtension* buildingext = Extension::Fetch(building);
                if (building->House == house && buildingext->AssignedExpansionPoint == terraincell) {
                    found = true;
                    break;
                }

                // Not all refineries have an assigned expansion point. For example, initial
                // base refineries and human players' refineries do not.
                // For these refineries, we rely on a distance check.
                if (buildingext->AssignedExpansionPoint == CELL_NONE || building->House != house) {
                    int dist = ::Distance(building->Get_Cell(), terraincell);
                    if (dist <= RuleExtension->AdvancedAIFieldOccupyMaximumDistance) {
                        found = true;
                        break;
                    }
                }
            }

            if (found)
                continue; // Someone is already occupying this Tiberium tree

            int distance = ::Distance(firstbuilding->Center_Coord(), terrain->Center_Coord());

            // Don't expand super far.
            if (distance / CELL_LEPTON_W < RuleExtension->AdvancedAIMaxExpansionDistance) {

                // Bias the distance by tiberium value and a bit of randomness.
                TiberiumClass* tib = Tiberiums[terrain->Class->TiberiumToSpawn];
                int value = distance - tib->CreditValue - 10 + Random_Pick(0, 20);

                if (value < nearestdistance) {
                    nearestdistance = value;
                    target = terrain->Get_Cell();
                }
            }
        }
    }

    if (target.X == 0 || target.Y == 0) {
        // We couldn't find anywhere to expand towards
        return false;
    }

    ext->NextExpansionPointLocation = target;

    return true;
}


bool AdvAI_Can_Build_Building(HouseClass* house, BuildingTypeClass* buildingtype, bool check_prereqs)
{
    ASSERT_FATAL(BuildingTypes.ID(buildingtype) == buildingtype->Fetch_Heap_ID());
    if (buildingtype->RTTI != RTTI_BUILDINGTYPE) {
        DEBUG_ERROR("Invalid BuildingTypeClass pointer in AdvAI_Can_Build_Building!!!");
        Emergency_Exit(0);
    }

    // DEBUG_INFO("Checking if AI %d can build %s. ", house->Get_Heap_ID(), buildingtype->IniName);

    if ((int)buildingtype->Level > house->Control.TechLevel) {
        // DEBUG_INFO("Result: false (TechLevel)\n");
        return false;
    }

    if (!buildingtype->CanAIBuildThis) {
        // DEBUG_INFO("Result: false (AIBuildThis)\n");
        return false;
    }

    if ((buildingtype->Ownable & (1 << house->ActLike)) != (1 << house->ActLike)) {
        // DEBUG_INFO("Result: false (Ownable)\n");
        return false;
    }

    BuildingTypeClassExtension* buildingtypeext = Extension::Fetch(buildingtype);

    if (check_prereqs && !buildingtypeext->IsAdvancedAIIgnoresPrerequisites) {
        for (int i = 0; i < buildingtype->Prerequisite.Count(); i++) {
            int buildingtypeid = buildingtype->Prerequisite[i];

            // Prerequisite groups are decoded as negative building IDs - check for them
            if (buildingtypeid < 0) 
            {
                PrerequisiteGroupType grouptype = PrerequisiteGroupClass::Decode(buildingtypeid);
                if (grouptype == PREREQ_GROUP_NONE) {
                    continue;
                }

                PrerequisiteGroupClass* group = PrerequisiteGroups[grouptype];
                bool found = false;

                for (int j = 0; j < group->Prerequisites.Count(); j++)
                {
                    if (house->ActiveBQuantity.Value((StructType)group->Prerequisites[j]) > 0)
                    {
                        found = true;
                        break;
                    }
                }

                if (!found) {
                    return false;
                }
            }
            else if (buildingtypeid >= 0 && house->ActiveBQuantity.Value((StructType)buildingtypeid) == 0)
            {
                return false;
            }
        }
    }

    // If this is an upgrade, do we have a building we could upgrade with it?
    if (buildingtype->PowersUpBuilding[0] != '\0') {
        const BuildingTypeClass* base = BuildingTypeClass::Find_Or_Make(buildingtype->PowersUpBuilding.c_str());

        if (house->ActiveBQuantity.Value((StructType)base->Fetch_Heap_ID()) == 0) {
            // DEBUG_INFO("Result: false (no upgradeable buildings)\n");
            return false;
        }

        bool found = false;

        // Scan through the buildings...
        for (int i = 0; i < Buildings.Count(); i++) {
            BuildingClass* building = Buildings[i];

            if (!building->IsActive ||
                building->IsInLimbo ||
                building->Class != base ||
                building->House != house) {
                continue;
            }

            if (building->UpgradeLevel >= base->Upgrades) {
                continue;
            }

            found = true;
        }

        if (!found) {
            // DEBUG_INFO("Result: false (no upgradeable building found in scan)\n");
            return false;
        }
    }

    // DEBUG_INFO("Result: true\n");
    return true;
}


/**
 *  Checks if AdvAI is under threat of being start rushed.
 *  Start rushes require specific tactics to counter.
 *
 *  Author: Rampastring
 */
bool AdvAI_Is_Under_Start_Rush_Threat(HouseClass* house)
{
    // If the game has progressed for long enough, it is no longer considered a start rush.
    if (Frame > 10000) {
        return false;
    }

    HouseClassExtension* houseext = Extension::Fetch(house);

    if (houseext->AdvAI_Is_Recently_Attacked()) {
        return true;
    }

    // Counter infantry rushing. If an enemy has significantly more infantry than we do, we are at risk.

    static int house_infantry_strength[10];
    memset(house_infantry_strength, 0, sizeof(int) * std::size(house_infantry_strength));

    // Go through all infantry on the map and gather infantry strength of all enemy human houses.
    for (int i = 0; i < Infantry.Count(); i++)
    {
        InfantryClass* inf = Infantry[i];

        if (inf->IsInLimbo) {
            continue;
        }

        // Also calculate our own infantry strength for comparison.
        if (inf->House == house) {
            house_infantry_strength[house->Fetch_Heap_ID()] += inf->Class->Points;
            continue;
        }

        if (inf->House->Class->IsMultiplayPassive) {
            continue;
        }

        if (inf->House->Is_Ally(house)) {
            continue;
        }

        if (inf->House->Fetch_Heap_ID() >= std::size(house_infantry_strength)) {
            continue;
        }
    }

    int our_infantry_strength = house_infantry_strength[house->Fetch_Heap_ID()];
    for (int i = 0; i < std::size(house_infantry_strength) && i < Houses.Count(); i++)
    {
        if (i == house->HeapID)
            continue;

        // Humans can typically micromanage better than the AI, so increase points for human infantry.
        int theirmultiplier = Houses[i]->Is_Human_Player() ? 5 : 2;

        if (house_infantry_strength[i] * theirmultiplier > our_infantry_strength * 3) {
            return true;
        }
    }

    return false;
}


 /**
  *  Calculates the total number of enemy aircraft in the game.
  *
  *  Author: Rampastring
  */
int AdvAI_Calculate_Enemy_Aircraft_Count(HouseClass* house)
{
    int enemy_aircraft_count = 0;

    for (int i = 0; i < Aircrafts.Count(); i++) {
        AircraftClass* aircraft = Aircrafts[i];

        if (!aircraft->House->Is_Ally(house) && !aircraft->House->Class->IsMultiplayPassive) {
            enemy_aircraft_count++;
        }
    }

    return enemy_aircraft_count;
}


bool AdvAI_Is_Nod_Enemy_Present(HouseClass* house)
{
    for (int i = 0; i < Houses.Count(); i++)
    {
        HouseClass* other = Houses[i];

        if (other == house)
            continue;

        if (house->Is_Ally(other))
            continue;

        if (house->Class->HeapID == HOUSE_NOD && !house->IsDefeated)
            return true;
    }

    return false;
}


bool AdvAI_Is_Disadvantaged(HouseClass* house)
{
    HouseClassExtension* houseext = Extension::Fetch(house);

    int enemytotalstrength = houseext->EnemyNoneStrength + houseext->EnemyLightStrength + houseext->EnemyHeavyStrength;
    int ourstrength = 0;

    for (int i = 0; i < Teams.Count(); i++)
    {
        TeamClass* team = Teams[i];
        if (team->House != house)
            continue;

        TeamClassExtension* teamext = Extension::Fetch(team);
        ourstrength += teamext->CurrentCost;
    }

    return enemytotalstrength > ourstrength;
}


/**
 *  Gets the building that the Advanced AI should build in its current game situation.
 *
 *  Author: Rampastring
 */
const BuildingTypeClass* AdvAI_Evaluate_Get_Best_Building(HouseClass* house)
{
    HouseClassExtension* houseext = Extension::Fetch(house);

    StructType our_refinery = STRUCT_NONE;
    StructType our_basic_power = STRUCT_NONE;
    StructType our_advanced_power = STRUCT_NONE;

    for (int i = 0; i < Rule->BuildRefinery.Count(); i++) {
        BuildingTypeClass* refinery = Rule->BuildRefinery[i];
        if (AdvAI_Can_Build_Building(house, refinery, true)) {
            our_refinery = (StructType)refinery->Fetch_Heap_ID();
        }
    }

    for (int i = 0; i < Rule->BuildPower.Count(); i++) {
        if (AdvAI_Can_Build_Building(house, Rule->BuildPower[i], true)) {
            if (our_basic_power != STRUCT_NONE) {
                our_advanced_power = (StructType)Rule->BuildPower[i]->Fetch_Heap_ID();
            }
            else {
                our_basic_power = (StructType)Rule->BuildPower[i]->Fetch_Heap_ID();
            }
        }
    }

    // Since we do not currently know how to handle prerequisite groups, our basic and
    // advanced power plants might be reversed.
    // Check if this is the case and if so, reverse them.
    if (our_basic_power != STRUCT_NONE && our_advanced_power != STRUCT_NONE &&
        BuildingTypes[our_basic_power]->Power > BuildingTypes[our_advanced_power]->Power) {
        StructType tmp = our_basic_power;
        our_basic_power = our_advanced_power;
        our_advanced_power = tmp;
    }

    // If we have no power plants yet, then build one
    if (house->ActiveBQuantity.Value(our_basic_power) == 0) {
        DEBUG_INFO("AdvAI: Making AI build %s because it has 0 basic power plants\n", BuildingTypes[our_basic_power]->IniName.c_str());
        return BuildingTypes[our_basic_power];
    }

    // On Medium and Hard, build a barracks if we do not have any yet
    if (house->Difficulty < DIFF_HARD && house->Credits >= Rule->AIAlternateProductionCreditCutoff) {
        for (int i = 0; i < Rule->BuildBarracks.Count(); i++) {
            BuildingTypeClass* barracks = Rule->BuildBarracks[i];

            if (AdvAI_Can_Build_Building(house, barracks, true)) {
                int barrackscount = house->ActiveBQuantity.Value((StructType)barracks->Fetch_Heap_ID());
                if (barrackscount < 1) {

                    DEBUG_INFO("AdvAI: Making AI build %s because it does not have a Barracks at all.\n", barracks->IniName.c_str());

                    return barracks;
                }
            }
        }
    }

    // Check how many aircraft our opponents have.
    // This check could be expensive, but usually there are not very
    // high numbers of aircraft in the game, so it's probably fine.
    int enemy_aircraft_count = AdvAI_Calculate_Enemy_Aircraft_Count(house);

    // Check whether we're in threat of being rushed right in the beginning of the game.
    bool is_under_threat = houseext->IsUnderStartRushThreat;

    bool expansion_point_threatened = false;

    // Build a refinery if we have 0 left
    if (our_refinery != STRUCT_NONE && house->ActiveBQuantity.Value(our_refinery) == 0) {
        DEBUG_INFO("AdvAI: Making AI build %s because it has 0 refineries\n", BuildingTypes[our_refinery]->IniName.c_str());
        return BuildingTypes[our_refinery];
    }

    // If we don't have enough barracks, then build one
    int optimal_barracks_count = 1 + ((house->ActiveBQuantity.Value(our_refinery) - 1) / 4);

    // If we are playing on a naval-focused map, then limit barracks to one.
    if (houseext->IsNavalOnly == AdvancedAINavalOnlyState::NAVAL_ONLY) {
        optimal_barracks_count = 1;
    }

    for (int i = 0; i < Rule->BuildBarracks.Count(); i++) {
        BuildingTypeClass* barracks = Rule->BuildBarracks[i];

        if (AdvAI_Can_Build_Building(house, barracks, true)) {
            int barrackscount = house->ActiveBQuantity.Value((StructType)barracks->Fetch_Heap_ID());
            if (barrackscount < optimal_barracks_count) {

                DEBUG_INFO("AdvAI: Making AI build %s because it does not have enough Barracks. Wanted: %d, current: %d\n",
                    barracks->IniName.c_str(), optimal_barracks_count, barrackscount);

                return barracks;
            }
        }
    }

    // Get defenses and calculate for deficiencies on them before making
    // further decisions.
    StructType our_anti_infantry_defense = STRUCT_NONE;
    StructType our_anti_vehicle_defense = STRUCT_NONE;
    StructType our_anti_air_defense = STRUCT_NONE;

    int best_anti_infantry_rating = INT_MIN;
    int best_anti_vehicle_rating = INT_MIN;

    for (int i = 0; i < Rule->BuildDefense.Count(); i++) {

        BuildingTypeClass* buildingtype = Rule->BuildDefense[i];

        if (AdvAI_Can_Build_Building(house, buildingtype, true)) {
            if (buildingtype->AntiInfantryValue > best_anti_infantry_rating) {
                best_anti_infantry_rating = buildingtype->AntiInfantryValue;
                our_anti_infantry_defense = (StructType)buildingtype->Fetch_Heap_ID();
            }

            if (buildingtype->AntiArmorValue > best_anti_vehicle_rating) {
                best_anti_vehicle_rating = buildingtype->AntiArmorValue;
                our_anti_vehicle_defense = (StructType)buildingtype->Fetch_Heap_ID();
            }
        }
    }

    for (int i = 0; i < Rule->BuildAA.Count(); i++) {
        if (AdvAI_Can_Build_Building(house, Rule->BuildAA[i], true)) {
            our_anti_air_defense = (StructType)Rule->BuildAA[i]->Fetch_Heap_ID();
        }
    }

    bool expansion = false;

    // Build refinery if we're expanding and we're not under immediate rush threat
    if (!is_under_threat) {
        if (our_refinery != STRUCT_NONE && houseext->ShouldBuildRefinery) {

            if (Map.Cell_Threat(houseext->NextExpansionPointLocation, house) <= 20) {
                expansion = true;
            }
            else {
                expansion_point_threatened = true;
            }
        }
    }

    int optimal_defense_count = house->ActiveBQuantity.Value(our_refinery) / 3 + (house->ActiveBQuantity.Value(our_basic_power) + house->ActiveBQuantity.Value(our_advanced_power)) / 4;
   
    // Don't overspend on defense if it's still early, we really need to focus on economy at that point.
    if (Frame < 15000) {
        if (optimal_defense_count > 0)
            optimal_defense_count--;
    }
    else if (Frame < 30000 && !houseext->AdvAI_Is_Outnumbered()) {
        optimal_defense_count = optimal_defense_count / 2;
    }
    else if (Frame > 50000) {
        optimal_defense_count++;
    }

    if (houseext->Has_Radar())
    {
        optimal_defense_count++;
    }

    if (houseext->Has_Tech_Center())
    {
        optimal_defense_count++;
    }

    if (expansion_point_threatened) {
        optimal_defense_count++;
    }

    if (houseext->AdvAI_Is_Outnumbered()) {
        optimal_defense_count++;
    }

    if (house->Class->HeapID == 2 && Frame > 20000)
        optimal_defense_count++;

    // Disabled for now.
    bool isfun = false; // houseext->AdvAIFunValue > 90 && Frame < (int)(6400 * house->BuildSpeedBias) && (house->Class->HeapID == 1 || house->Class->HeapID == 3);

    // If we are under attack, prioritize defense.
    if (houseext->AdvAI_Is_Recently_Attacked()) {
        optimal_defense_count++;
    }
    // If we just expanded, then we want to build a defense near the expansion location.
    else if (houseext->DefensePlacementLocation != CELL_NONE) {
        optimal_defense_count++;
    }

    if (isfun && optimal_defense_count > 0)
        optimal_defense_count--;

    // Check which type of defense is most desperately needed.
    int anti_inf_deficiency = 0;
    int anti_vehicle_deficiency = 0;
    int anti_air_deficiency = 0;

    if (our_anti_infantry_defense != STRUCT_NONE) {
        int optimal_anti_inf_defense_count = optimal_defense_count;
        // Special check for early infantry rushes.
        // If we are getting infantry-rushed, build more anti-infantry defenses.
        if (is_under_threat && enemy_aircraft_count == 0) {
            optimal_anti_inf_defense_count++;
            optimal_anti_inf_defense_count *= 3;
        }

        int defensecount = house->ActiveBQuantity.Value(our_anti_infantry_defense);
        anti_inf_deficiency = optimal_anti_inf_defense_count - defensecount;
    }

    if (our_anti_infantry_defense != our_anti_vehicle_defense && our_anti_vehicle_defense != STRUCT_NONE) {
        int defensecount = house->ActiveBQuantity.Value(our_anti_vehicle_defense);
        anti_vehicle_deficiency = optimal_defense_count - defensecount;
    }

    // We're just going to bluntly assume that we need 1 AA defense for every 2 enemy aircraft present.
    int needed_aa_count = enemy_aircraft_count / 2;
    // ...but don't overspend on AA.
    if (needed_aa_count > optimal_defense_count * 2) {
        needed_aa_count = optimal_defense_count;
    }

    int aa_defensecount = 0;

    if (our_anti_air_defense != STRUCT_NONE) {
        aa_defensecount = house->ActiveBQuantity.Value(our_anti_air_defense);
    }

    anti_air_deficiency = needed_aa_count - aa_defensecount;

    // If we are under threat of an immediate early-game rush, then skip the WF and refinery minimums.
    // Instead build defenses or tech up so we can get AA ASAP.
    if ((!is_under_threat || (anti_inf_deficiency == 0 && anti_air_deficiency == 0)) && 
        anti_inf_deficiency + anti_vehicle_deficiency < RuleExtension->AdvancedAICriticalBaseDefenseDeficiencyThreshold)
    {
        // If we don't have enough weapons factory, then build one.
        int optimal_wf_count = 1 + ((house->ActiveBQuantity.Value(our_refinery) - 1) / 4);

        // If we are playing on a naval-focused map, then limit war factories to one.
        if (houseext->IsNavalOnly == AdvancedAINavalOnlyState::NAVAL_ONLY) {
            if (houseext->Has_Naval_Yard()) {
                optimal_wf_count = 1;
            }
            else {
                optimal_wf_count = 0;
            }
        }

        int minimum_ref_count = RuleExtension->AdvancedAIMinimumRefineryCount;

        // Maybe we'll do something "fun"?
        if (isfun) {
            optimal_wf_count = 0;
            minimum_ref_count = 1;
        }

        if (Session.Options.Credits > 8000 || our_refinery == STRUCT_NONE || house->ActiveBQuantity.Value(our_refinery) > 2) {
            for (int i = 0; i < Rule->BuildWeapons.Count(); i++) {
                BuildingTypeClass* weaponsfactory = Rule->BuildWeapons[i];

                if (AdvAI_Can_Build_Building(house, weaponsfactory, true)) {
                    int wfcount = house->ActiveBQuantity.Value((StructType)weaponsfactory->Fetch_Heap_ID());
                    if (wfcount < optimal_wf_count) {

                        DEBUG_INFO("AdvAI: Making AI build %s because it does not have enough Weapons Factories. Wanted: %d, current: %d\n",
                            weaponsfactory->IniName.c_str(), optimal_wf_count, wfcount);

                        return weaponsfactory;
                    }
                }
            }
        }

        // If we have too few refineries, build enough to match the minimum.
        // Because this is not for expanding but an emergency situation,
        // cancel any potential expanding.
        if (our_refinery != STRUCT_NONE && house->ActiveBQuantity.Value(our_refinery) < minimum_ref_count) {
            houseext->ArchivedExpansionPointLocation = houseext->NextExpansionPointLocation;
            houseext->NextExpansionPointLocation = Cell(0, 0);
            DEBUG_INFO("AdvAI: Making AI build %s because it only has too few refineries\n", BuildingTypes[our_refinery]->IniName.c_str());
            return BuildingTypes[our_refinery];
        }
    }

    if (expansion) {
        DEBUG_INFO("AdvAI: Making AI build %s because it has reached an expansion point\n", BuildingTypes[our_refinery]->IniName.c_str());
        return BuildingTypes[our_refinery];
    }

    // If we don't have enough naval yards, then build one.
    int optimal_naval_count = 1 + ((house->ActiveBQuantity.Value(our_refinery) - 1) / 6);
    if (houseext->IsNavalOnly == AdvancedAINavalOnlyState::NAVAL_ONLY) {
        int optimal_naval_count = 1 + ((house->ActiveBQuantity.Value(our_refinery) - 1) / 5);
    }

    for (int i = 0; i < RuleExtension->BuildNavalYard.Count(); i++) {
        BuildingTypeClass* navalyard = RuleExtension->BuildNavalYard[i];

        if (AdvAI_Can_Build_Building(house, navalyard, true)) {
            int navalyardcount = house->ActiveBQuantity.Value((StructType)navalyard->Fetch_Heap_ID());
            if (navalyardcount < optimal_naval_count) {
                DEBUG_INFO("AdvAI: Making AI build %s because it does not have enough Naval Yards. Wanted: %d, current: %d\n",
                    navalyard->IniName.c_str(), optimal_naval_count, navalyardcount);

                return navalyard;
            }
        }
    }

    // If the enemy has much more armor than infantry, then prioritize anti-armor defense.
    if (house->Enemy != HOUSE_NONE) {
        if (anti_vehicle_deficiency > 0 && houseext->EnemyHeavyStrength > houseext->EnemyNoneStrength) {
            int multi = houseext->EnemyHeavyStrength / houseext->EnemyNoneStrength;
            anti_vehicle_deficiency = anti_vehicle_deficiency * multi;
        }
    }

    if (anti_inf_deficiency > 0 && anti_inf_deficiency > anti_vehicle_deficiency && anti_inf_deficiency > anti_air_deficiency) {
        DEBUG_INFO("AdvAI: Making AI build %s because it does not have enough anti-inf defenses. Wanted: %d, deficiency: %d\n",
            BuildingTypes[our_anti_infantry_defense]->IniName.c_str(), optimal_defense_count, anti_inf_deficiency);

        return BuildingTypes[our_anti_infantry_defense];
    }

    if (anti_vehicle_deficiency > 0 && anti_vehicle_deficiency >= anti_air_deficiency) {
        DEBUG_INFO("AdvAI: Making AI build %s because it does not have enough anti-vehicle defenses. Wanted: %d, deficiency: %d\n",
            BuildingTypes[our_anti_vehicle_defense]->IniName.c_str(), optimal_defense_count, anti_vehicle_deficiency);

        return BuildingTypes[our_anti_vehicle_defense];
    }

    if (anti_air_deficiency > 0)
    {
        // If we actually can't build AA yet, then we need to tech up first.

        if (our_anti_air_defense != STRUCT_NONE) {
            DEBUG_INFO("AdvAI: Making AI build %s because it does not have enough anti-air defenses. Deficiency: %d\n",
                BuildingTypes[our_anti_air_defense]->IniName.c_str(), anti_air_deficiency);

            return BuildingTypes[our_anti_air_defense];
        }
    }

    // If we have no radar, then build one
    for (int i = 0; i < Rule->BuildRadar.Count(); i++) {
        BuildingTypeClass* radar = Rule->BuildRadar[i];

        // Don't check prereqs to hack around TDPROC vs TDPROC_AI difference
        if (AdvAI_Can_Build_Building(house, radar, false)) {
            int radarcount = house->ActiveBQuantity.Value((StructType)radar->Fetch_Heap_ID());

            if (radarcount < 1) {
                DEBUG_INFO("AdvAI: Making AI build %s because it does not have enough radars. Current count: %d\n",
                    radar->IniName.c_str(), radarcount);

                return radar;
            }
        }
    }

    // Don't process helipads or tech centers if we have been recently attacked - they are not a priority item.
    if (!houseext->AdvAI_Is_Recently_Attacked() || isfun)
    {
        // If we don't have enough helipads, then build one
        int optimal_helipad_count = isfun ? 0 : 1 + ((house->ActiveBQuantity.Value(our_refinery) - 1) / 2);

        for (int i = 0; i < Rule->BuildHelipad.Count(); i++) {
            BuildingTypeClass* helipad = Rule->BuildHelipad[i];

            if (AdvAI_Can_Build_Building(house, helipad, true)) {
                int helipadcount = house->ActiveBQuantity.Value((StructType)helipad->Fetch_Heap_ID());

                if (helipadcount < optimal_helipad_count) {
                    DEBUG_INFO("AdvAI: Making AI build %s because it does not have enough helipads. Wanted: %d, current: %d\n",
                        helipad->IniName.c_str(), optimal_helipad_count, helipadcount);

                    return helipad;
                }
            }
        }

        // If we have no tech center, then build one if we are allowed to
        if (Frame > RuleExtension->AdvancedAINoTechCenterBeforeFrame)
        {
            for (int i = 0; i < Rule->BuildTech.Count(); i++) {
                BuildingTypeClass* techcenter = Rule->BuildTech[i];
                if (AdvAI_Can_Build_Building(house, techcenter, true)) {
                    if (house->ActiveBQuantity.Value((StructType)techcenter->Fetch_Heap_ID()) < 1) {
                        DEBUG_INFO("AdvAI: Making AI build %s because it does not have a tech center.\n",
                            techcenter->IniName.c_str());

                        houseext->AdvAIFunValue = 1;
                        return techcenter;
                    }
                }
            }
        }
    }
    else
    {
        optimal_defense_count++;
    }

    // Build some advanced defenses if we do not have enough
    int optimal_adv_defense_count = optimal_defense_count / 2;

    StructType our_adv_defense = STRUCT_NONE;

    for (int i = 0; i < Rule->BuildPDefense.Count(); i++) {
        if (AdvAI_Can_Build_Building(house, Rule->BuildPDefense[i], true)) {
            our_adv_defense = (StructType)Rule->BuildPDefense[i]->Fetch_Heap_ID();
        }
    }

    if (our_adv_defense != STRUCT_NONE) {
        int advdefensecount = house->ActiveBQuantity.Value(our_adv_defense);

        if (advdefensecount < optimal_adv_defense_count) {
            DEBUG_INFO("AdvAI: Making AI build %s because it does not have enough. Wanted: %d, current: %d.\n",
                BuildingTypes[our_adv_defense]->IniName.c_str(), optimal_adv_defense_count, advdefensecount);

            return BuildingTypes[our_adv_defense];
        }
    }

    // Are there other AIBuildThis=yes buildings that we haven't built yet?
    // However, don't build these if we are badly outnumbered.
    if (!houseext->AdvAI_Is_Outnumbered())
    {
        for (int i = 0; i < BuildingTypes.Count(); i++) {

            if (BuildingTypes[i]->IsSensorArray && !AdvAI_Is_Nod_Enemy_Present(house))
                continue;

            if (BuildingTypes[i]->CanAIBuildThis && i != our_anti_infantry_defense && i != our_anti_vehicle_defense && i != our_anti_air_defense) {
                if (house->ActiveBQuantity.Value((StructType)i) < 1 && AdvAI_Can_Build_Building(house, BuildingTypes[i], true)) {
                    DEBUG_INFO("AdvAI: Making AI build %s because it has AIBuildThis=yes and the AI has none.\n",
                        BuildingTypes[i]->IniName.c_str());
                    return BuildingTypes[i];
                }
            }
        }
    }

    // Build power if we have somewhere to expand towards, and nothing else to expand with.
    if (houseext->NextExpansionPointLocation.X != 0 && houseext->NextExpansionPointLocation.Y != 0) {
        if (our_basic_power != STRUCT_NONE) {
            DEBUG_INFO("AdvAI: Making AI build %s because the AI is expanding.\n",
                BuildingTypes[our_basic_power]->IniName.c_str());
            return BuildingTypes[our_basic_power];
        }
    }

    // Build surplus power if we don't have some.
    if (!is_under_threat && /*Frame > 5000 &&*/ house->Power - house->Drain < 100) {
        if (our_advanced_power != STRUCT_NONE && (Frame > 2000 || our_basic_power == STRUCT_NONE)) {
            DEBUG_INFO("AdvAI: Making AI build %s because it is out of power and can build an adv. power plant\n", BuildingTypes[our_advanced_power]->IniName.c_str());
            return BuildingTypes[our_advanced_power];
        }

        if (our_basic_power != STRUCT_NONE) {
            DEBUG_INFO("AdvAI: Making AI build %s because it is out of power and can only build a basic power plant\n", BuildingTypes[our_basic_power]->IniName.c_str());
            return BuildingTypes[our_basic_power];
        }
    }

    return nullptr;
}


const BuildingTypeClass* AdvAI_Get_Building_To_Build(HouseClass* house)
{
    const BuildingTypeClass* buildchoice = AdvAI_Evaluate_Get_Best_Building(house);

    if (buildchoice == nullptr) {
        return nullptr;
    }

    // If our power budget couldn't afford the building, then build a power plant first instead.
    // Unless it's a refinery that we're building, those are considered more critical.
    if (buildchoice->Drain > 0 && !buildchoice->IsRefinery && (house->Drain + buildchoice->Drain > house->Power)) {
        StructType our_basic_power = STRUCT_NONE;
        StructType our_advanced_power = STRUCT_NONE;

        for (int i = 0; i < Rule->BuildPower.Count(); i++) {
            if (AdvAI_Can_Build_Building(house, Rule->BuildPower[i], true)) {
                if (our_basic_power != STRUCT_NONE) {
                    our_advanced_power = (StructType)Rule->BuildPower[i]->Fetch_Heap_ID();
                }
                else {
                    our_basic_power = (StructType)Rule->BuildPower[i]->Fetch_Heap_ID();
                }
            }
        }

        if (our_advanced_power != STRUCT_NONE && (Frame > 10000 || our_basic_power == STRUCT_NONE || house->Difficulty != DIFF_EASY)) {
            return BuildingTypes[our_advanced_power];
        }

        if (our_basic_power != STRUCT_NONE) {
            return BuildingTypes[our_basic_power];
        }
    }

    return buildchoice;
}


/**
 *  Checks if AdvAI should raise money.
 *  If it should, then raises money.
 *
 *  Author: Rampastring
 */
void AdvAI_Raise_Money(HouseClass* house)
{
    // We should raise money if we are low on funds and have zero refineries.

    if (house->Credits > 1000) {
        return;
    }

    StructType our_refinery = STRUCT_NONE;

    for (int i = 0; i < Rule->BuildRefinery.Count(); i++) {
        BuildingTypeClass* refinery = Rule->BuildRefinery[i];
        if (AdvAI_Can_Build_Building(house, refinery, true)) {
            our_refinery = (StructType)refinery->Fetch_Heap_ID();
        }
    }

    if (our_refinery == STRUCT_NONE) {
        return;
    }

    int refinery_count = house->ActiveBQuantity.Value(our_refinery);

    if (refinery_count > 0) {
        return;
    }

    // Look for buildings to sell.
    DEBUG_INFO("AdvAI: Attempting to raise money.\n");

    BuildingClass* bestbuilding = nullptr;
    int bestcost = INT_MIN;

    for (int i = 0; i < Buildings.Count(); i++) {
        BuildingClass* building = Buildings[i];

        if (!building->IsActive || building->IsInLimbo || building->House != house || building->Class->IsConstructionYard) {
            continue;
        }

        if (building->Mission == MISSION_CONSTRUCTION || building->MissionQueue == MISSION_CONSTRUCTION) {

            // Don't sell something that we've just built.
            continue;
        }

        if (building->Mission == MISSION_DECONSTRUCTION || building->MissionQueue == MISSION_DECONSTRUCTION) {

            // We are already in the process of selling something.
            return;
        }

        // Prefer selling the most expensive stuff first.
        // Give a lower priority to super-weapon buildings, however.
        // They'll be expensive to replace later on.
        int cost = building->Class->Cost;
        if (building->Class->SuperWeapon != SUPER_NONE && building->Class->SuperWeapon2 != SUPER_NONE) {
            cost = cost / 3;
        }

        if (cost > bestcost) {
            bestbuilding = building;
            bestcost = cost;
        }
    }

    // If we found something to sell, then sell it.
    if (bestbuilding != nullptr) {
        DEBUG_INFO("AdvAI: Found a building to sell.\n");
        bestbuilding->Sell_Back(1);
    }
}


/**
 *  Perfoms some general economy maintenance.
 *  Raises money if necessary.
 *
 *  Author: Rampastring
 */
void AdvAI_Economy_Upkeep(HouseClass* house)
{
    AdvAI_Raise_Money(house);

    // Don't sell refineries on Easy mode.
    if (house->Difficulty == DIFF_HARD) {
        return;
    }

    StructType our_refinery = STRUCT_NONE;

    for (int i = 0; i < Rule->BuildRefinery.Count(); i++) {
        BuildingTypeClass* refinery = Rule->BuildRefinery[i];
        if (AdvAI_Can_Build_Building(house, refinery, true)) {
            our_refinery = (StructType)refinery->Fetch_Heap_ID();
        }
    }

    if (our_refinery == STRUCT_NONE) {
        return;
    }

    int refinery_count = house->ActiveBQuantity.Value(our_refinery);

    int harvester_count = 0;
    for (int i = 0; i < Rule->HarvesterUnit.Count(); i++) {
        UnitTypeClass* harvtype = Rule->HarvesterUnit[i];
        harvester_count += house->ActiveUQuantity.Value((UnitType)harvtype->Fetch_Heap_ID());
    }

    int to_sell_count = refinery_count - harvester_count;
    if (to_sell_count <= 0) {
        return;
    }

    DEBUG_INFO("AdvAI: Looking for a refinery to sell because we have %d excess.\n", to_sell_count);

    // Sell the refinery that is closest to our primary enemy.
    // If we have extra refineries, we have lost harvesters, and harvesters are most likely
    // lost near the expansion that is closest to our primary enemy.
    // If we have no primary enemy, then sell one near our base center.
    // It probably won't go horribly wrong anyway.

    HouseClass* enemy = nullptr;
    if (house->Enemy != HOUSE_NONE) {
        enemy = Houses[house->Enemy];
    }

    Cell centerpoint;

    if (enemy != nullptr) {
        centerpoint = enemy->Base_Center();
    }
    else {
        centerpoint = house->Base_Center();
    }

    BuildingClass* farthest_refinery = nullptr;
    int closest_distance = INT_MAX;

    for (int i = 0; i < Buildings.Count(); i++) {
        BuildingClass* building = Buildings[i];

        if (!building->IsActive || building->IsInLimbo || building->House != house || !building->Class->IsRefinery) {
            continue;
        }

        if (building->Mission == MISSION_CONSTRUCTION || building->MissionQueue == MISSION_CONSTRUCTION) {
            // If a refinery is in process of being constructed, it hasn't got the spawn its FreeUnit
            // harvester yet.
            DEBUG_INFO("AdvAI: We have a refinery in construction phase, skip.\n");
            return;
        }

        if (building->Mission == MISSION_DECONSTRUCTION || building->MissionQueue == MISSION_DECONSTRUCTION) {
            // We are already in the process of selling a refinery, don't sell more
            // until it's finished.
            DEBUG_INFO("AdvAI: We are already selling a refinery, skip.\n");
            return;
        }

        int distance = ::Distance(centerpoint, building->Center_Coord().As_Cell());
        if (distance < closest_distance) {
            closest_distance = distance;
            farthest_refinery = building;
        }
    }

    if (farthest_refinery != nullptr) {
        DEBUG_INFO("AdvAI: Found a Refinery to sell.\n");
        farthest_refinery->Sell_Back(1);
    }
}


/**
 *  Checks for sleeping harvesters. If found, puts them to Harvest mode.
 *
 *  Author: Rampastring
 */
void AdvAI_Awaken_Sleeping_Harvesters(HouseClass* house)
{
    for (int i = 0; i < Units.Count(); i++) {
        UnitClass* unit = Units[i];

        if (!unit->IsActive || unit->IsInLimbo || unit->House != house || !unit->Class->IsToHarvest) {
            continue;
        }

        if (unit->Mission == MISSION_SLEEP || unit->Mission == MISSION_GUARD) {
            DEBUG_INFO("AdvAI: Waking up a sleeping harvester.\n");
            unit->Assign_Mission(MISSION_HARVEST);
            unit->Commence();
        }
    }
}


/**
 *  Implements DTA's custom AI building selection logic.
 *
 *  Author: Rampastring
 */
int Vinifera_HouseClass_AI_Building(HouseClass* this_ptr)
{
    // Decide what to build.
    // If we already have something to build, do nothing.
    if (this_ptr->BuildStructure != STRUCT_NONE) return TICKS_PER_SECOND;

    if (this_ptr->ConstructionYards.Count() <= 0) return TICKS_PER_SECOND;

    HouseClassExtension* houseext = Extension::Fetch(this_ptr);

    if (RuleExtension->AdvancedAIBaseBuilding) {

        // If we have nowhere to expand towards, check for a new location to expand to.
        if (houseext->NextExpansionPointLocation.X <= 0 || houseext->NextExpansionPointLocation.Y <= 0) {
            AdvAI_House_Search_For_Next_Expansion_Point(this_ptr);
        }

        const BuildingTypeClass* tobuild = AdvAI_Get_Building_To_Build(this_ptr);

        if (tobuild == nullptr) {
            return TICKS_PER_SECOND * 5;
        }

        DEBUG_INFO("AI %d selected building %s to build. Frame: %d\n", this_ptr->Fetch_Heap_ID(), tobuild->IniName.c_str(), Frame);

        this_ptr->BuildStructure = (StructType)(tobuild->Fetch_Heap_ID());

        // Limit the tick rate a bit for better performance and fairness.
        // Also, add some randomization to reduce the "all AIs place buildings at the same time"
        // effect, avoiding a lag spike.
        return TICKS_PER_SECOND * 5 + Random_Pick(0, 3);

    }
    else {
        BaseNodeClass* node = this_ptr->Base.Next_Buildable();

        if (node != nullptr) {
            this_ptr->BuildStructure = node->Type;
        }
    }

    return TICKS_PER_SECOND;
}


BuildingClass* AdvAI_Find_Nearest_Capturable_Oil_Refinery_To(InfantryClass* infantry)
{
    BuildingClass* target = nullptr;
    int shortestdistance = INT_MAX;

    HouseClassExtension* houseext = Extension::Fetch(infantry->House);

    for (int i = 0; i < Buildings.Count(); i++)
    {
        BuildingClass* building = Buildings[i];

        bool okintheory = false;

        if (houseext->Is_Valid_Building_For_Capturing(building, infantry->PositionCell, okintheory))
        {
            int distance = infantry->Distance(building);
            if (distance < shortestdistance) {
                target = building;
                shortestdistance = distance;
            }
        }
    }

    return target;
}


void AdvAI_Check_For_Engineer_Capturing_Neutral_Building(HouseClass* house)
{
    HouseClassExtension* houseext = Extension::Fetch(house);

    for (int i = 0; i < Infantry.Count(); i++)
    {
        InfantryClass* infantry = Infantry[i];

        if (infantry->IsActive && infantry->IsDown && infantry->House == house && infantry->Class->IsEngineer) {

            if (infantry->NavCom != nullptr && infantry->NavCom->RTTI == RTTI_BUILDING) {
                continue;
            }

            BuildingClass* building = AdvAI_Find_Nearest_Capturable_Oil_Refinery_To(infantry);
            if (building != nullptr) {

                if (infantry->Team != nullptr) {
                    infantry->Team->Remove(infantry);
                    infantry->field_205 = false;
                    infantry->field_206 = false;
                }

                infantry->Assign_Target(building);
                infantry->Assign_Destination(building);
                infantry->Assign_Mission(MISSION_CAPTURE);
                infantry->Commence();

                int attemptindex = houseext->Get_Building_Capture_Attempt_Index_For(building);
                if (attemptindex > -1) {
                    houseext->AttemptedBuildingCaptures[attemptindex].Frame = Frame;
                }
                else {
                    houseext->Add_Building_Capture_Attempt(building);
                }

                // only process one infantry at a time or we break teams that contain engineers,
                // causing the AI to build only engineers for a long time
                break; 
            }
        }
    }
}


void AdvAI_Calculate_Enemy_Strength(HouseClass* house)
{
    HouseClassExtension* houseext = Extension::Fetch(house);
    houseext->EnemyNoneStrength = 0;
    houseext->EnemyLightStrength = 0;
    houseext->EnemyHeavyStrength = 0;
    houseext->EnemyArtilleryStrength = 0;
    houseext->EnemyBaseDefenseStrength = 0;
    houseext->EnemyNavalStrength = 0;

    houseext->EnemyAntiGroundStrength.Clear();
    houseext->EnemyAntiAirStrength.Clear();
    houseext->EnemyAntiNavalStrength.Clear();

    houseext->EnemyHarvesterCount = 0;
    houseext->EnemyRefineryCount = 0;

    for (int i = 0; i < Technos.Count(); i++)
    {
        TechnoClass* techno = Technos[i];

        if (house->Enemy != HOUSE_NONE) {
            if (techno->House->HeapID != house->Enemy) {
                continue;
            }
        }
        else {
            if (house->Is_Ally(techno->House)) {
                continue;
            }
        }

        const TechnoTypeClass* technotype = techno->TClass;
        TechnoTypeClassExtension* technotypeext = Extension::Fetch(technotype);

        if (techno->RTTI == RTTI_BUILDING) {
            BuildingClass* building = reinterpret_cast<BuildingClass*>(techno);

            if (building->Class->IsRefinery) {
                houseext->EnemyRefineryCount++;
            }

            if (building->Class->IsSensorArray) {
                houseext->EnemyHasSensors = true;
            }

            if (techno->Is_Weapon_Equipped()) {
                houseext->EnemyBaseDefenseStrength += technotype->Cost;

                if (!Extension::Fetch(building->Class)->IsNaval) {
                    houseext->EnemyAntiGroundStrength.Add_Techno_Type_Half_Weight(technotypeext);
                }
                else {
                    houseext->EnemyAntiNavalStrength.Add_Techno_Type_Half_Weight(technotypeext);
                }
            }

            continue;
        }

        if (techno->RTTI == RTTI_UNIT && reinterpret_cast<UnitClass*>(techno)->Class->IsToHarvest) {
            houseext->EnemyHarvesterCount++;
            continue;
        }

        if (!techno->Is_Weapon_Equipped()) {
            continue;
        }

        if (technotypeext->IsNaval) {
            houseext->EnemyNavalStrength += technotype->Cost;
            houseext->EnemyAntiNavalStrength.Add_Techno_Type(technotypeext);
            continue;
        }

        houseext->EnemyAntiAirStrength.Add_Techno_Type_AntiAir(technotypeext);
        houseext->EnemyAntiGroundStrength.Add_Techno_Type(technotypeext);

        if (technotype->Armor == ARMOR_NONE) {
            houseext->EnemyNoneStrength += technotype->Cost;
        }
        else if (technotype->Armor == ARMOR_WOOD || technotype->Armor == ARMOR_ALUMINUM || technotype->Armor == (ArmorType)5) {
            houseext->EnemyLightStrength += technotype->Cost;
        }
        else if (technotype->Armor == ARMOR_STEEL || technotype->Armor == ARMOR_CONCRETE) {
            houseext->EnemyHeavyStrength += technotype->Cost;
        }

        WeaponTypeClass* primaryweapon = techno->Get_Primary_Weapon();
        if (primaryweapon != nullptr && primaryweapon->Range >= CELL_LEPTON * 8) {
            houseext->EnemyArtilleryStrength += technotype->Cost;
        }
    }

    // DEBUG_INFO("AdvAI: House %d: Enemy (%d) strength calculated. Values: None: %d, Light: %d, Heavy: %d, Artillery: %d, BaseDefense: %d, Naval: %d, AGAntiNone: %d, AGAntiLight: %d, AGAntiHeavy: %d, AAAntiNone: %d, AAAntiLight: %d, AAAntiHeavy: %d\n",
    //     house->HeapID, house->Enemy, houseext->EnemyNoneStrength, houseext->EnemyLightStrength, houseext->EnemyHeavyStrength, houseext->EnemyArtilleryStrength, houseext->EnemyBaseDefenseStrength,
    //     houseext->EnemyBaseDefenseStrength, houseext->EnemyNavalStrength, houseext->EnemyAntiGroundStrength.None, houseext->EnemyAntiGroundStrength.Light, houseext->EnemyAntiGroundStrength.Heavy,
    //     houseext->EnemyAntiAirStrength.None, houseext->EnemyAntiAirStrength.Light, houseext->EnemyAntiAirStrength.Heavy);
}


/**
 *  Commences a single team.
 *
 *  Author: Rampastring
 */
void AdvAI_Commence_Team(TeamClass* team)
{
    team->IsFullStrength = true;
    team->IsHasBeen = true;
    team->IsForcedActive = true;
    Extension::Fetch(team)->OriginalTeamMemberCount = team->Total;
}


/**
 *  Splits a team up if it's too large and eligible for getting split.
 *  Afterwards, commences all the teams potentially created from the split,
 *  as well the primary team.
 *
 *  Author: Rampastring
 */
void AdvAI_Check_Split_And_Commence_Team(TeamClass* team)
{
    auto ext = Extension::Fetch(team);

    // Check if this team should be limited by unit count
    if (!ext->IsAircraftTeam && ext->ProdFlags == PRODFLAG_NONE && !ext->IsTransportTeam)
    {
        // Check if the team has too many units in it.
        // If yes, we need to split it up.
        if (team->Total > RuleExtension->AdvancedAIMaxTeamSize)
        {
            // Calculate number of new teams to create
            int numteams = team->Total / RuleExtension->AdvancedAIMaxTeamSize;
            int remainder = team->Total % RuleExtension->AdvancedAIMaxTeamSize;
            if (remainder == 0)
                numteams--;

            DEBUG_INFO("AdvAI: House %d: Splitting a team of %d units into %d teams.\n", (int)team->House->HeapID, team->Total, (numteams + 1));

            // Create the new teams
            for (int i = 0; i < numteams; i++)
            {
                TeamClass* other = team->Class->Create_One_Of(team->House);

                auto otherext = Extension::Fetch(other);
                otherext->Copy_Executive_State_From(ext);

                // Recruit units from the primary team
                for (int unitindex = 0; unitindex < RuleExtension->AdvancedAIMaxTeamSize; unitindex++)
                {
                    if (!other->Add(team->Member)) {
                        Vinifera_Log_And_Show_WWMessageBox("AdvAI_Check_Split_And_Commence_Team: Failed to add unit to new team!");
                    }
                }

                // Commence the new team
                AdvAI_Commence_Team(other);
            }
        }
    }
    else
    {
        DEBUG_INFO("AdvAI: House %d: Commencing a single team.\n", (int)team->House->HeapID);
    }

    // Finally, commence the primary team
    AdvAI_Commence_Team(team);
}

/**
 *  Executes the current tactic for the Advanced AI if preferred.
 *
 *  Author: Rampastring
 */
void AdvAI_Commence_Current_Tactic(HouseClass* house)
{
    HouseClassExtension* houseext = Extension::Fetch(house);

    TeamClass* team = nullptr;
    int id = 0;

    if (houseext->AdvAIGroundTactic.Tactic != AdvAITacticType::TACTIC_NONE)
    {
        if (Frame >= houseext->AdvAIGroundTactic.EndFrame()) {

            DEBUG_INFO("AdvAI: House %d: Commencing tactic \"%s\". Frame: %d\n", (int)house->HeapID, AdvAITacticType_To_Name(houseext->AdvAIGroundTactic.Tactic), Frame);

            // Direct attack tactics might not be executed at the first opportunity.
            if (house->Enemy != HOUSE_NONE &&
                (houseext->AdvAIGroundTactic.Tactic == AdvAITacticType::TACTIC_DIRECT_ATTACK_FAST || houseext->AdvAIGroundTactic.Tactic == AdvAITacticType::TACTIC_DIRECT_ATTACK_REGULAR))
            {
                // Calculate total cost of our current teams. If our current teams are 
                // very small relative to the enemy's strength, attacking is not worth it - just continue accumulating forces.
                // Unless our economy is significantly worse than the enemy's - then we need to go all-in.

                int ourharv = house->ActiveUQuantity.Value(house->Get_First_ActLike(Rule->HarvesterUnit)->HeapID);
                int ourref = house->ActiveBQuantity.Value(house->Get_First_ActLike(Rule->BuildRefinery)->HeapID);
                int ourtotalecon = std::min(ourref * 2, ourharv);

                int enemytotalecon = std::min(houseext->EnemyRefineryCount * 2, houseext->EnemyHarvesterCount);

                if (ourtotalecon >= enemytotalecon) 
                {
                    int totalcost = 0;
                    int artillerystrength = 0;

                    id = 0;
                    team = houseext->Get_Team_In_Production(id, RTTI_NONE, PRODFLAG_NONE);
                    while (team != nullptr) {

                        TeamClassExtension* teamext = Extension::Fetch(team);
                        totalcost += teamext->CurrentCost;
                        artillerystrength += teamext->ArtilleryStrength;
                        team = houseext->Get_Team_In_Production(id, RTTI_NONE, PRODFLAG_NONE);
                    }

                    // Require us to have at least 50% of the enemy's strength.
                    // If we lack that force, extend the current tactic.
                    int enemytotalstrength = houseext->EnemyNoneStrength + houseext->EnemyLightStrength + houseext->EnemyHeavyStrength;
                    if (artillerystrength <= 0) {
                        enemytotalstrength += houseext->EnemyBaseDefenseStrength;
                    }

                    if (totalcost * 2 < enemytotalstrength) {
                        houseext->AdvAIGroundTactic.Duration += 1000;
                        return;
                    }
                }
            }

            // It's time to adopt another tactic. Commence the teams we have produced for the current tactic.

            id = 0;
            team = houseext->Get_Team_In_Production(id, RTTI_NONE, PRODFLAG_NONE);
            while (team != nullptr) {

                TeamClassExtension* teamext = Extension::Fetch(team);

                if (!teamext->IsAircraftTeam) {
                    if (teamext->MinimumReadyFrame <= 0 || Frame >= teamext->MinimumReadyFrame) {
                        AdvAI_Check_Split_And_Commence_Team(team);
                    }
                }

                team = houseext->Get_Team_In_Production(id, RTTI_NONE, PRODFLAG_NONE);
            }

            houseext->AdvAILastExecutionFrameForTactic[houseext->AdvAIGroundTactic.Tactic] = Frame;
            houseext->AdvAILastTacticExecutionFrame = Frame;
            houseext->AdvAIGroundTactic.Tactic = AdvAITacticType::TACTIC_NONE;
        }
    }

    bool commence_air_tactic = false;
    bool commence_naval_tactic = false;

    if (Frame > houseext->AdvAIAirTactic.EndFrame()) {
        commence_air_tactic = true;
    }

    if (Frame > houseext->AdvAINavalTactic.EndFrame()) {
        commence_naval_tactic = true;
    }

    if (commence_air_tactic) 
    {
        id = 0;
        team = houseext->Get_Team_In_Production(id, RTTI_AIRCRAFT, PRODFLAG_NONE);
        while (team != nullptr) {

            TeamClassExtension* teamext = Extension::Fetch(team);

            if (teamext->IsAircraftTeam) {
                if (teamext->MinimumReadyFrame <= 0 || Frame >= teamext->MinimumReadyFrame) {
                    AdvAI_Check_Split_And_Commence_Team(team);
                }
            }

            team = houseext->Get_Team_In_Production(id, RTTI_AIRCRAFT, PRODFLAG_NONE);
        }

        houseext->AdvAIAirTactic.Tactic = AIRTACTIC_NONE;
    }

    if (commence_naval_tactic) 
    {
        id = 0;
        team = houseext->Get_Team_In_Production(id, RTTI_NONE, PRODFLAG_NAVAL);
        while (team != nullptr) {

            TeamClassExtension* teamext = Extension::Fetch(team);

            if (teamext->MinimumReadyFrame <= 0 || Frame >= teamext->MinimumReadyFrame) {
                AdvAI_Check_Split_And_Commence_Team(team);
            }

            team = houseext->Get_Team_In_Production(id, RTTI_NONE, PRODFLAG_NAVAL);
        }

        houseext->AdvAINavalTactic.Tactic = NAVALTACTIC_NONE;
    }
}


void AdvAI_Air_Tactic_AI(HouseClass* house)
{
    HouseClassExtension* houseext = Extension::Fetch(house);

    if (houseext->AdvAIAirTactic.Tactic != AIRTACTIC_NONE || (Frame < houseext->AdvAIAirTactic.EndFrame()))
    {
        // If we have existing aircraft teams that are at max cost, commence them.
        bool allatmaxcost = true;
        int id = 0;
        TeamClass* team = houseext->Get_Team_In_Production(id, RTTI_AIRCRAFT, PRODFLAG_NONE);
        
        // Find the first actually aircraft team.
        while (team != nullptr) {
            TeamClassExtension* teamext = Extension::Fetch(team);

            if (teamext->IsAircraftTeam) {
                break;
            }
            else {
                team = houseext->Get_Team_In_Production(id, RTTI_AIRCRAFT, PRODFLAG_NONE);
            }
        }

        // Nothing to process if we have no aircraft teams.
        if (team == nullptr) {
            return;
        }

        while (team != nullptr) 
        {
            TeamClassExtension* teamext = Extension::Fetch(team);

            if (teamext->IsAircraftTeam) {
                if (teamext->CurrentCost < teamext->MaxCost - 1500) {
                    allatmaxcost = false;
                    break;
                }
            }

            team = houseext->Get_Team_In_Production(id, RTTI_AIRCRAFT, PRODFLAG_NONE);
        }

        if (allatmaxcost || !houseext->Has_Helipad()) {

            // All aircraft teams are built up, commence them!
            id = 0;

            team = houseext->Get_Team_In_Production(id, RTTI_AIRCRAFT, PRODFLAG_NONE);
            while (team != nullptr) {
                TeamClassExtension* teamext = Extension::Fetch(team);

                if (teamext->IsAircraftTeam) {
                    AdvAI_Check_Split_And_Commence_Team(team);
                }

                team = houseext->Get_Team_In_Production(id, RTTI_AIRCRAFT, PRODFLAG_NONE);
            }

            houseext->AdvAIAirTactic.Tactic = AIRTACTIC_NONE;
        }

        return;
    }

    // Do we have a helipad/airfield? If not, there is nothing to do here.
    if (!houseext->Has_Helipad()) {
        return;
    }

    int chance = 0;
    if (houseext->EnemyAntiAirStrength.Total() <= 0) {
        // If our enemy has absolutely no anti-air capability, we need to exploit that.
        chance = 100;
    }
    else {
        chance = 100 - houseext->EnemyAntiAirStrength.Total() / 100;
        chance = std::max(chance, 5);

        if (house->Credits > Rule->AIAlternateProductionCreditCutoff * 2) {
            chance *= house->Credits / Rule->AIAlternateProductionCreditCutoff;
        }
        else if (house->Credits < Rule->AIAlternateProductionCreditCutoff / 2) {
            chance = chance / 2;
        }
    }

    // If we are significantly outnumbered, it's usually better to focus on ground.
    if (houseext->AdvAI_Is_Outnumbered()) {
        chance = chance / 2;
    }

    // If aircraft is the only thing we can build, then we should build it.
    if (!houseext->Has_Barracks() && !houseext->Has_War_Factory()) {
        chance = 100;
    }

    if (chance >= 100 || Percent_Chance(chance)) {

        for (int i = 0; i < AdvancedAITacticTypes.Count(); i++)
        {
            if (!AdvancedAITacticTypes[i]->IsAir) {
                continue;
            }

            if (AdvancedAITacticTypes[i]->Process(house))
            {
                return;
            }
        }
    }
    else {
        houseext->Assign_AdvAI_Air_Tactic(AIRTACTIC_NONE, 2000);
    }
}

void AdvAI_Naval_Tactic_AI(HouseClass* house)
{
    HouseClassExtension* houseext = Extension::Fetch(house);

    if (!houseext->Has_Naval_Yard() || houseext->AdvAINavalTactic.Tactic != NAVALTACTIC_NONE)
    {
        return;
    }

    for (int i = 0; i < AdvancedAITacticTypes.Count(); i++)
    {
        if (!AdvancedAITacticTypes[i]->IsNaval) {
            continue;
        }

        if (AdvancedAITacticTypes[i]->Process(house))
        {
            return;
        }
    }
}

void AdvAI_Tactic_Selection_AI(HouseClass* house)
{
    // We have no need for tactics if we are dead.
    if (house->IsDefeated) {
        return;
    }

    HouseClassExtension* houseext = Extension::Fetch(house);

    // If we have no ground based factories, there is nothing to do.
    if (!houseext->Has_Barracks() && !houseext->Has_War_Factory()) {
        return;
    }

    AdvAI_Calculate_Enemy_Strength(house);

    AdvAI_Commence_Current_Tactic(house);

    AdvAI_Air_Tactic_AI(house);

    AdvAI_Naval_Tactic_AI(house);

    // Don't be too mean against newbies.
    if (house->Difficulty == DIFF_HARD && Frame < 1500) {
        return;
    }

    // On some difficulty levels, the AI should play imperfectly.
    // One part of this is having an artificial wait between tactics to give the player a moment to breathe.
    // However, don't allow us to get rushed too easily.
    int tacticselectiondelay = RuleExtension->AdvancedAITacticSelectionDelay[house->Difficulty];
    if (!houseext->IsUnderStartRushThreat && tacticselectiondelay > 0 && Frame < houseext->AdvAILastTacticExecutionFrame + tacticselectiondelay) {
        return;
    }

    if (houseext->AdvAIGroundTactic.Tactic == AdvAITacticType::TACTIC_NONE)
    {
        if (houseext->FactionSpecificTacticalValues[0] == 0)
            houseext->FactionSpecificTacticalValues[0] = Random_Pick(1, 10);

        if (houseext->AdvAIGroundTactic.Tactic == AdvAITacticType::TACTIC_NONE)
        {
            for (int i = 0; i < AdvancedAITacticTypes.Count(); i++)
            {
                if (AdvancedAITacticTypes[i]->IsAir || AdvancedAITacticTypes[i]->IsNaval) {
                    continue;
                }

                // If we play naval only, we should only build a limited number of ground units for a defensive purpose.
                if (houseext->IsNavalOnly == AdvancedAINavalOnlyState::NAVAL_ONLY && AdvancedAITacticTypes[i]->GroundTacticType != TACTIC_DEFEND) {
                    continue;
                }

                if (AdvancedAITacticTypes[i]->Process(house))
                {
                    return;
                }
            }
        }
    }
}

void AdvAI_Team_Recruitment_AI(HouseClass* house)
{
    HouseClassExtension* houseext = Extension::Fetch(house);

    int id = 0;
    TeamClass* team = houseext->Get_Team_In_Production(id, RTTI_NONE, PRODFLAG_NONE);
    while (team != nullptr) {
        TeamClassExtension* teamext = Extension::Fetch(team);

        teamext->AdvAI_Team_Recruit_AI();
        team = houseext->Get_Team_In_Production(id, RTTI_NONE, PRODFLAG_NONE);
    }

    id = 0;
    team = houseext->Get_Team_In_Production(id, RTTI_NONE, PRODFLAG_NAVAL);
    while (team != nullptr) {
        TeamClassExtension* teamext = Extension::Fetch(team);

        teamext->AdvAI_Team_Recruit_AI();
        team = houseext->Get_Team_In_Production(id, RTTI_NONE, PRODFLAG_NAVAL);
    }
}


void AdvAI_Team_Maintenance_AI(HouseClass* house)
{
    for (int i = 0; i < Teams.Count(); i++)
    {
        TeamClass* team = Teams[i];

        if (team->House != house)
            continue;

        TeamClassExtension* teamext = Extension::Fetch(team);

        if (!team->IsForcedActive && teamext->IsBiasedForEnemyStrength && !teamext->IsAircraftTeam && teamext->ProdFlags == PRODFLAG_NONE)
            Extension::Fetch(house)->AdvAI_Set_Ground_Team_Desired_Ratios(team, teamext->AdvAIGroundTacticType);

        teamext->AdvAI_Team_Maintenance_AI();
    }
}


void AdvAI_Self_Defense_AI(HouseClass* house)
{
    if (house->LATime + TICKS_PER_MINUTE < Frame) {
        return;
    }

    Cell attackedcell = CELL_NONE;

    // Find a damaged building of ours - assume it to be attacked.
    for (int i = 0; i < Buildings.Count(); i++) 
    {
        BuildingClass* building = Buildings[i];
        
        if (building->IsActive && !building->IsInLimbo && building->IsDown && building->House == house &&
            building->Strength < building->Class->MaxStrength && !building->Class->IsBaseDefense)
        {
            attackedcell = building->Center_Coord().As_Cell();
            break;
        }
    }

    for (int i = 0; i < Foots.Count(); i++)
    {
        FootClass* foot = Foots[i];

        if (!foot->IsActive || foot->IsInLimbo || !foot->IsDown || foot->House != house || !foot->Is_Weapon_Equipped())
            continue;

        if (foot->NavCom != nullptr)
            continue;

        if (foot->Team != nullptr && foot->Team->IsForcedActive)
            continue;

        if (foot->RTTI == RTTI_AIRCRAFT) {
            if (foot->In_Air())
                continue;

            foot->Assign_Mission(MISSION_GUARD_AREA);
            foot->Commence();
            continue;
        }

        if (foot->RTTI == RTTI_UNIT && Extension::Fetch(reinterpret_cast<UnitClass*>(foot)->Class)->IsNaval)
            continue;

        if (foot->Mission == MISSION_GUARD) {
            foot->Assign_Mission(MISSION_GUARD_AREA);
            foot->Commence();
        }

        if (attackedcell != CELL_NONE) {
            Cell nearbyloc = Map.Nearby_Location(attackedcell, foot->TClass->Speed, Map.Get_Cell_Zone(foot->PositionCell, foot->TClass->MZone), foot->TClass->MZone);

            if (nearbyloc != CELL_NONE) {
                foot->Assign_Destination(&Map[nearbyloc]);
            }
        }
    }
}

void AdvAI_Undeploy_Enforcers(HouseClass* house)
{
    // This is only a problem for Allies.
    if (house->Class->HeapID != 2)
        return;

    HouseClassExtension* houseext = Extension::Fetch(house);

    if (Frame < houseext->AdvAILastUndeployableUnitCheckFrame + 2250) {
        return;
    }

    for (int i = 0; i < Buildings.Count(); i++)
    {
        BuildingClass* building = Buildings[i];

        if (!building->IsActive || building->IsInLimbo || !building->IsDown || building->House != house)
            continue;

        if (building->Class->UndeploysInto == nullptr)
            continue;

        if (building->TarCom != nullptr)
            continue;

        if (building->Class->UndeploysInto->Fetch_Weapon_Info(WEAPON_SLOT_PRIMARY).Weapon == nullptr)
            continue;

        building->Assign_Mission(MISSION_DECONSTRUCTION);
        building->Commence();
    }

    // Scan for deployable foots that are not recruitable and set them to be recruitable
    // !! Not necessary, AdvAI v2 recruitment logic does not currently care about whether the unit is recruitable or not
    // for (int i = 0; i < Foots.Count(); i++)
    // {
    //     FootClass* foot = Foots[i];
    // 
    //     if (!foot->IsActive || foot->IsInLimbo || !foot->IsDown || foot->House != house)
    //         continue;
    // 
    //     if (!foot->Is_Weapon_Equipped())
    //         continue;
    // 
    //     if (foot->TClass->DeploysInto == nullptr)
    //         continue;
    // 
    //     if (foot->Recrui)
    // }
}

void AdvAI_Check_Naval_Only(HouseClass* house)
{
    HouseClassExtension* houseext = Extension::Fetch(house);

    houseext->LastNavalOnlyCheckFrame = Frame;

    // Check if there are no enemy technos on the same land-passable zone with our first object.

    int ourzone = -1;

    // First, find our zone.
    // Take it from the first land object owned by us that we find.
    // It is likely the MCV or a Construction Yard.
    // Use the Destroyer movement zone to indicate we can destroy walls or trees if they are on the way to an enemy.
    for (int i = 0; i < Technos.Count(); i++)
    {
        TechnoClass* techno = Technos[i];
        if (!techno->IsActive || !techno->IsDown || techno->IsInLimbo || techno->House != house || Extension::Fetch(techno->TClass)->IsNaval || 
            (techno->RTTI == RTTI_BUILDING && reinterpret_cast<BuildingClass*>(techno)->Class->IsInvisibleInGame))
            continue;

        if (!Map.In_Local_Radar(techno->Center_Coord()))
            continue;

        ourzone = Map.Get_Cell_Zone(techno->PositionCell, MZONE_DESTROYER);
        break;
    }

    if (ourzone < 0) {
        return;
    }

    // Then, scan all enemy objects and check whether any are on the same zone with us.
    for (int i = 0; i < Technos.Count(); i++)
    {
        TechnoClass* techno = Technos[i];
        if (!techno->IsActive || !techno->IsDown || techno->IsInLimbo || !techno->TClass->IsLegalTarget || 
            techno->House->Class->IsMultiplayPassive ||
            techno->House->Is_Ally(house) || Extension::Fetch(techno->TClass)->IsNaval)
            continue;

        if (!Map.In_Local_Radar(techno->Center_Coord()))
            continue;

        int enemyzone = Map.Get_Cell_Zone(techno->PositionCell, MZONE_DESTROYER);

        if (enemyzone == ourzone) {
            houseext->IsNavalOnly = AdvancedAINavalOnlyState::NORMAL;
            return;
        }
    }

    // If no enemy was found to be on the same zone, mark that we should focus on naval.
    houseext->IsNavalOnly = AdvancedAINavalOnlyState::NAVAL_ONLY;
}

/**
 *  Performs some maintenance for the Advanced AI.
 *
 *  Author: Rampastring
 */
void AdvAI_HouseClass_Expert_AI(HouseClass* house)
{
    if (house->Class->IsMultiplayPassive) {
        return;
    }

    // Only enable our custom logic when using Advanced AI.
    if (!RuleExtension->IsUseAdvancedAI) {
        return;
    }

    HouseClassExtension* houseext = Extension::Fetch(house);
    houseext->IsUnderStartRushThreat = AdvAI_Is_Under_Start_Rush_Threat(house);

    if (houseext->AdvAIFunValue < 1) {
        houseext->AdvAIFunValue = Random_Pick(1, 100);
    }

    // If we are under attack, check for units in Guard mode and set them to Area Guard mode.
    AdvAI_Self_Defense_AI(house);

    AdvAI_Undeploy_Enforcers(house);

    // If our current enemy is Neutral, clear our enemy.
    if (house->Enemy != HOUSE_NONE && Houses[house->Enemy]->Class->IsMultiplayPassive) {
        house->Clear_Anger(Houses[house->Enemy]);
        house->Enemy = HOUSE_NONE;
    }

    // If we have no enemy, then pick one.
    if (house->Enemy == HOUSE_NONE) {
        house->ExpertAITimer = 0;
    }

    // If we haven't yet checked whether this is a "naval-only" map, check that now.
    // Also re-check it periodically in case the situation changes.
    if (houseext->IsNavalOnly == AdvancedAINavalOnlyState::NOT_CHECKED || Frame > houseext->LastNavalOnlyCheckFrame + 5000) {
        AdvAI_Check_Naval_Only(house);
    }

    if (RuleExtension->AdvancedAIUnitProduction)
    {
        // Try to recruit units to teams.
        AdvAI_Team_Recruitment_AI(house);
        AdvAI_Team_Maintenance_AI(house);

        // Refresh our tactics. It is important that this is done post-recruitment,
        // as team composition can impact whether our tactics are commenced.
        AdvAI_Tactic_Selection_AI(house);
    }

    if (houseext->NextEngineerCheckFrame < Frame && houseext->NextOilRefineryCaptureCheckFrame < INT_MAX) {
        AdvAI_Check_For_Engineer_Capturing_Neutral_Building(house);
        houseext->NextEngineerCheckFrame = Frame + 500 + Random_Pick(10, 50);
    }

    // Do some economy upkeep to keep the AI running.

    if (Frame > houseext->LastExcessRefineryCheckFrame + 500) {
        houseext->LastExcessRefineryCheckFrame = Frame;
        AdvAI_Economy_Upkeep(house);
    }

    if (Frame > houseext->LastSleepingHarvesterCheckFrame + 1000) {
        houseext->LastSleepingHarvesterCheckFrame = Frame;
        AdvAI_Awaken_Sleeping_Harvesters(house);
    }

    // If we have 0 ConYards and 0 War Factories, it is very unlikely we could get
    // back into the game. Send all our non-Harvester vehicles into Hunt mode.
    if (Frame > 5000 && house->ConstructionYards.Count() == 0 && house->UnitFactories == 0 && !houseext->HasPerformedVehicleCharge)
    {
        houseext->HasPerformedVehicleCharge = true;

        for (int i = 0; i < Units.Count(); i++)
        {
            UnitClass* unit = Units[i];

            if (unit->House == house &&
                (unit->Class->DeploysInto == nullptr || !unit->Class->DeploysInto->IsConstructionYard) &&
                !unit->Class->IsToHarvest &&
                !unit->Class->IsToVeinHarvest)
            {
                if (unit->Team != nullptr) {
                    unit->Team->Remove(unit);
                }

                unit->Assign_Mission(MISSION_HUNT);
            }
        }
    }

    // If we are under threat of getting rushed early and our ConYard is producing something non-defensive and non-power-granting, abandon it.
    int enemy_aircraft_count = AdvAI_Calculate_Enemy_Aircraft_Count(house);
    bool is_under_threat = AdvAI_Is_Under_Start_Rush_Threat(house);

    if (is_under_threat) {
        FactoryClass* buildingfactory = houseext->Fetch_Factory(RTTI_BUILDING, PRODFLAG_NONE);
        if (buildingfactory != nullptr) {
            if (buildingfactory->Get_Object() != nullptr) {
                BuildingClass* building = reinterpret_cast<BuildingClass*>(buildingfactory->Get_Object());

                if (building->Class->Power <= 0 ||
                    building->Class->Fetch_Weapon_Info(WEAPON_SLOT_PRIMARY).Weapon == nullptr ||
                    building->Class->ToBuild != RTTI_INFANTRYTYPE)
                {
                    buildingfactory->Abandon();
                }
            }
        }
    }
}


/**
 *  A fake class for implementing new member functions which allow
 *  access to the "this" pointer of the intended class.
 *
 *  @note: This must not contain a constructor or destructor!
 *  @note: All functions must be prefixed with "_" to prevent accidental virtualization.
 */
static DECLARE_EXTENDING_CLASS_AND_PAIR(HouseClass)
{
public:
    void _AI();
    int _AI_Building();
    int _AI_Unit();
    int _AI_Infantry();
    int _AI_Aircraft();
    int _Expert_AI();
    bool _Can_Build_Required_Forbidden_Houses(const TechnoTypeClass* techno_type);
    bool _Can_Build_Buildability(const TechnoTypeClass * techno_type);
    void _Active_Remove(TechnoClass const* techno);
    void _Active_Add(TechnoClass const* techno);
    Cell _Find_Build_Location(BuildingTypeClass* btype, int(__fastcall* callback)(int, Cell&, int, int), int a3 = -1);
    void _Production_Check();
    bool _AI_Has_Prerequisites(const TechnoTypeClass* type, DynamicVectorClass<const BuildingTypeClass*>& owned, int ownedcount) const;
    void _Harvested(int tiberium, TiberiumType slot);
    bool _AI_Target_MultiMissile(SuperClass* super);
    void _AI_Super_Weapons();
    void _AI_Ion_Cannon(SuperClass* super);
    bool _Can_Make_Money();
    UrgencyType _Check_Raise_Money();
    void _MPlayer_Defeated();
    void _Make_Ally(HouseClass* house);

    // stubs
    FactoryClass* _Fetch_Factory(RTTIType rtti);
    void _Set_Factory(RTTIType rtti, FactoryClass* factory);
    int* _Factory_Counter(RTTIType rtti);
    int _Factory_Count(RTTIType rtti) const;
    ProdFailType _Suspend_Production(RTTIType type);
    ProdFailType _Begin_Production(RTTIType type, int id, bool resume);
    ProdFailType _Abandon_Production(RTTIType type, int id);
    bool _Place_Object(RTTIType type, Cell const& cell);
    void _Update_Factories(RTTIType rtti);
    TechnoTypeClass const* _Suggest_New_Object(RTTIType objecttype, bool kennel) const;
    ExtDiffType _Assign_Handicap(ExtDiffType handicap);
};


/**
 *  Determines what building to build.
 *
 *  @author: 09/29/1995 JLB - Created.
 *           ZivDero - Adjustments for Tiberian Sun
 */
int HouseClassExt::_AI_Building()
{
    enum {
        BASE_WALL = -3,
        BASE_UNKNOWN = -2,
        BASE_DEFENSE = -1
    };

    /**
     *  If our custom AI logic is enabled, transfer control to it and return.
     */
    if (RuleExtension->AdvancedAIBaseBuilding) {
        return Vinifera_HouseClass_AI_Building(this);
    }

    if (BuildStructure != STRUCT_NONE) return TICKS_PER_SECOND;

    if (ConstructionYards.Count() == 0) return TICKS_PER_SECOND;

    BaseNodeClass* node = Base.Next_Buildable();

    if (!node) return TICKS_PER_SECOND;

    /**
     *  Build some walls.
     */
    if (node->Type == BASE_WALL) {
        Base.Nodes.Delete(*node);
        AI_Build_Wall();
        return 1;
    }

    /**
     *  Build some defenses.
     */
    if (node->Type == BASE_DEFENSE || BuildingTypes[node->Type] == Rule->WallTower && node->CellID == Cell(0, 0)) {

        const int nodeid = Base.Nodes.ID(node);
        if (!AI_Build_Defense(nodeid, Base.field_38.Count() > 0 ? &Base.field_38 : nullptr)) {

            /**
             *  If it's a wall tower, delete it twice?
             *  Perhaps it's assumed that the wall tower is followed by its upgrade?
             */
            if (node->Type == Rule->WallTower->HeapID) {
                Base.Nodes.Delete(nodeid);
            }

            /**
             *  Remove the node from the list.
             */
            Base.Nodes.Delete(nodeid);
            return 1;
        }

        node = Base.Next_Buildable();
    }

    if (!node || node->Type == BASE_UNKNOWN) return TICKS_PER_SECOND;

    /**
     *  In campaigns, or if we have enough power, or if we're trying to building a construction yard,
     *  just proceed with building the base node.
     */
    BuildingTypeClass* b = BuildingTypes[node->Type];

    if (Session.Type != GAME_NORMAL && !ScenExtension->IsUseMPAIBaseNodes && b->Drain + Drain > Power - PowerSurplus && b != Rule->BuildConst[0] && b->Drain > 0) {

        /**
         *  In skirmish, try to build a power plant if there is insufficient power.
         */
        const BuildingTypeClass* choice = nullptr;
        const auto side_ext = Extension::Fetch(Sides[Class->Side]);

        /**
         *  First let's see if we can upgrade a power plant with a turbine (like GDI).
         */
        if (side_ext->PowerTurbine) {

            bool can_build_turbine = false;
            for (int i = 0; i < Buildings.Count(); i++) {

                BuildingClass* owned_b = Buildings[i];
                if (owned_b->Owner_HouseClass() == this) {
                    if (owned_b->Class == side_ext->RegularPowerPlant && owned_b->UpgradeLevel < owned_b->Class->Upgrades) {
                        can_build_turbine = true;
                        break;
                    }
                }
            }

            if (can_build_turbine && Probability_Of2(Rule->AIUseTurbineUpgradeProbability)) {
                choice = side_ext->PowerTurbine;
            }
        }

        /**
         *  If we can't build a turbine, try to build an advanced power plant (like Nod).
         */
        if (!choice && side_ext->AdvancedPowerPlant) {
            DynamicVectorClass<BuildingTypeClass*> owned_buildings;

            for (int i = 0; i < Buildings.Count(); i++) {
                BuildingClass* b2 = Buildings[i];
                if (b2->Owner_HouseClass() == this) {
                    owned_buildings.Add(b2->Class);
                }
            }

            if (Has_Prerequisites(side_ext->AdvancedPowerPlant, owned_buildings, owned_buildings.Count())) {
                choice = side_ext->AdvancedPowerPlant;
            }
        }

        /**
         *  If neither worked out, just build a normal power plant.
         */
        if (!choice) {
            choice = side_ext->RegularPowerPlant;
        }

        /**
         *  Build our chosen power structure before building whatever else we're trying to build.
         */
        const int id = Base.Nodes.ID(node);
        Base.Nodes.Insert(id, BaseNodeClass(choice->HeapID, Cell(0, 0)));

        return 1;
    }

    /**
     *  Check if this is a building upgrade if we can actually place the upgrade where it's scheduled to be placed.
     */
    if (b->PowersUpToLevel == -1 && node->CellID != Cell(0, 0) && !b->PowersUpBuilding.empty()) {

        BuildingClass* existing_building = Map[node->CellID].Cell_Building();
        BuildingTypeClass* node_building = BuildingTypes[BuildingTypeClass::From_Name(b->PowersUpBuilding.c_str())];

        if (existing_building == nullptr) {
            node->CellID = Cell(0, 0);
        }
        else if (existing_building->Class != node_building) {
            node->CellID = Cell(0, 0);
        }
        else if (existing_building->Class->PowersUpToLevel == -1 && existing_building->UpgradeLevel >= existing_building->Class->Upgrades || existing_building->Class->PowersUpToLevel > 0 && existing_building->UpgradeLevel > 0) {
            node->CellID = Cell(0, 0);
        }
    }

    BuildStructure = node->Type;
    return TICKS_PER_SECOND;
}


int HouseClassExt::_AI_Unit()
{
    auto extension = Extension::Fetch(this);
    int delay1 = extension->AI_Unit();
    int delay2 = extension->AI_Naval_Unit();
    return std::min(delay1, delay2);
}


InfantryType Find_Engineer(HouseClass* house)
{
    for (int i = 0; i < InfantryTypes.Count(); i++)
    {
        auto infantrytype = InfantryTypes[i];

        if (infantrytype->IsEngineer && house->Can_Build(infantrytype, false, true)) {
            return infantrytype->HeapID;
        }
    }

    return INFANTRY_NONE;
}


int AdvancedAI_AI_Infantry_Start_Rush_Counter(HouseClass* house)
{
    DynamicVectorClass<BuildingTypeClass const*> owned_buildings;
    Extension::Fetch(house)->Fill_Owned_Buildings_List(owned_buildings);

    InfantryType mostvaluable = INFANTRY_NONE;
    int highestvalue = -1;

    // Build a list of all infantry that we can build, alongside their scores for our current tactic.
    for (int i = 0; i < InfantryTypes.Count(); i++)
    {
        InfantryTypeClass* inftype = InfantryTypes[i];
        InfantryTypeClassExtension* inftypeext = Extension::Fetch(inftype);

        if ((inftype->Ownable & (1 << house->ActLike)) == (1 << house->ActLike) &&
            house->Can_Build(inftype, false, true) && reinterpret_cast<HouseClassExt*>(house)->_AI_Has_Prerequisites(inftype, owned_buildings, owned_buildings.Count())) {

            if (inftype->BuildLimit > 0 && house->ActiveIQuantity.Value(inftype->HeapID) >= inftype->BuildLimit)
                continue;

            int value = inftypeext->AntiNoneArmorValue();
            if (value > highestvalue) {
                mostvaluable = inftype->HeapID;
                highestvalue = value;
            }
        }
    }

    if (mostvaluable != INFANTRY_NONE) {
        house->BuildInfantry = mostvaluable;
    }

    return TICKS_PER_SECOND;
}


int AdvancedAI_AI_Infantry(HouseClass* house)
{
    auto extension = Extension::Fetch(house);
    if (extension->NextOilRefineryCaptureCheckFrame < Frame && house->ConstructionYards.Count() > 0)
    {
        bool oilreffound = false;

        for (int i = 0; i < Buildings.Count(); i++)
        {
            BuildingClass* building = Buildings[i];
            BuildingTypeClassExtension* buildingtypeext = Extension::Fetch(building->Class);

            bool okintheory = false;

            bool valid = extension->Is_Valid_Building_For_Capturing(building, house->ConstructionYards[0]->PositionCell, okintheory);

            if ((valid || okintheory) && !oilreffound) {
                // There is an oil refinery on the map that is theoretically capturable by us.
                extension->NextOilRefineryCaptureCheckFrame = Frame + 5000 + Random_Pick(0, 500);
                oilreffound = true;
            }

            if (valid) {
                InfantryType engineer = Find_Engineer(house);
                if (engineer != INFANTRY_NONE && house->ActiveIQuantity.Value(engineer) == 0) {
                    house->BuildInfantry = engineer;
                    return TICKS_PER_SECOND;
                }

                break;
            }
        }

        // If no capturable oil refinery exists even in theory, assume they are gone for the rest of the game.
        if (!oilreffound) {
            extension->NextOilRefineryCaptureCheckFrame = INT_MAX;
        }
    }

    int id = 0;
    TeamClass* team = extension->Get_Team_In_Production(id, RTTI_INFANTRY, PRODFLAG_NONE);

    if (team == nullptr) {

        // If we are being rushed, just build the best anti-infantry infantry type we can.
        if (extension->IsUnderStartRushThreat) {
            return AdvancedAI_AI_Infantry_Start_Rush_Counter(house);
        }

        return TICKS_PER_SECOND;
    }

    TeamClassExtension* teamext = Extension::Fetch(team);

    InfantryType mostvaluable = INFANTRY_NONE;
    int highestvalue = 0;

    if (teamext->AdvAIGroundTacticType == AdvAITacticType::TACTIC_DEFEND && !AdvAI_Is_Disadvantaged(house))
    {
        return TICKS_PER_SECOND;
    }

    bool haswarfactory = extension->Has_War_Factory();

    DynamicVectorClass<BuildingTypeClass const*> owned_buildings;
    extension->Fill_Owned_Buildings_List(owned_buildings);

    bool debugprint = Frame > extension->LastInfantryValueDebugPrintFrame + 10000;
    if (debugprint) {
        extension->LastInfantryValueDebugPrintFrame = Frame;
        DEBUG_INFO("AdvAI: House %d: Infantry values on Frame %d: Current Tactic: %s\n", (int)house->HeapID, Frame, AdvAITacticType_To_Name(extension->AdvAIGroundTactic.Tactic));
        DEBUG_INFO("    Has War Factory: %d\n", haswarfactory);
    }

    // Build a list of all infantry that we can build, alongside their scores for our current tactic.
    for (int i = 0; i < InfantryTypes.Count(); i++)
    {
        InfantryTypeClass* inftype = InfantryTypes[i];
        InfantryTypeClassExtension* inftypeext = Extension::Fetch(inftype);

        if (inftypeext->Buildability != TechnoTypeBuildability::BUILDABILITY_HUMAN_ONLY &&
            (inftype->Ownable & (1 << house->ActLike)) == (1 << house->ActLike) &&
            house->Can_Build(inftype, false, true) && reinterpret_cast<HouseClassExt*>(house)->_AI_Has_Prerequisites(inftype, owned_buildings, owned_buildings.Count())) {

            if (inftype->BuildLimit > 0 && house->IQuantity.Value(inftype->HeapID) >= inftype->BuildLimit)
                continue;

            int value = teamext->AdvAI_Get_Object_Value_For_Team(inftype, debugprint);

            if (debugprint) {
                DEBUG_INFO("    %s: %d\n", inftype->IniName.c_str(), value);
            }

            if (value > highestvalue) {
                mostvaluable = inftype->HeapID;
                highestvalue = value;
            }
        }
    }

    if (mostvaluable != INFANTRY_NONE) {

        if (debugprint) {
            DEBUG_INFO("    Selected: %s\n", InfantryTypes[mostvaluable]->IniName.c_str());
        }

        if (haswarfactory) {

            if (highestvalue < RuleExtension->AdvancedAISkipInfantryProductionValueThreshold)
            {
                if (house->Available_Money() < Rule->AIAlternateProductionCreditCutoff &&
                    extension->AdvAIGroundTactic.Tactic != AdvAITacticType::TACTIC_RUSH_ATTACK &&
                    extension->AdvAIGroundTactic.Tactic != AdvAITacticType::TACTIC_APC_ATTACK &&
                    extension->AdvAIGroundTactic.Tactic != AdvAITacticType::TACTIC_CHINOOK_ATTACK)
                {
                    // DEBUG_INFO("    Skipping infantry production because no infantry was valuable enough. %d\n", highestvalue);

                    return TICKS_PER_SECOND * 10;
                }
            }
            else if (highestvalue < RuleExtension->AdvancedAIConditionalSkipInfantryProductionValueThreshold)
            {
                if (house->Available_Money() < Rule->AIAlternateProductionCreditCutoff * 2 &&
                    extension->AdvAIGroundTactic.Tactic != AdvAITacticType::TACTIC_RUSH_ATTACK &&
                    extension->AdvAIGroundTactic.Tactic != AdvAITacticType::TACTIC_APC_ATTACK &&
                    extension->AdvAIGroundTactic.Tactic != AdvAITacticType::TACTIC_CHINOOK_ATTACK &&
                    Percent_Chance(50))
                {
                    // DEBUG_INFO("    Skipping infantry production because of RNG + no infantry was valuable enough. %d\n", highestvalue);

                    return TICKS_PER_SECOND * 30;
                }
            }
        }

        if (extension->AdvAILastBuiltInfantry == mostvaluable) {
            extension->AdvAILastBuiltInfantryCount++;
        }
        else {
            extension->AdvAILastBuiltInfantry = mostvaluable;
            extension->AdvAILastBuiltInfantryCount = 1;
        }

        house->BuildInfantry = mostvaluable;
    }
    else
    {
        // If we are under start rush threat, we should still build something.
        if (extension->IsUnderStartRushThreat) {
            return AdvancedAI_AI_Infantry_Start_Rush_Counter(house);
        }
    }

    return TICKS_PER_SECOND;
}

/**
 *  Advanced AI replacement for AI infantry production.
 *
 *  Author: original reverse-engineered by tomsons26/ZivDero, modified by Rampastring.
 */
int HouseClassExt::_AI_Infantry()
{
    InfantryType& BUILD = BuildInfantry;
    const InfantryType OBJNONE = INFANTRY_NONE;

    if (BUILD != OBJNONE) return(TICKS_PER_SECOND);

    if (RuleExtension->AdvancedAIUnitProduction)
    {
        return AdvancedAI_AI_Infantry(this);
    }

    int i;
    int counter[1000];
    int value[std::size(counter)];
    memset(counter, 0x00, sizeof(counter));
    for (i = 0; i < std::size(value); i++) {
        value[i] = 0x7FFFFFFF;
    }

    /*
    **	Build a list of the maximum of each type we wish to produce. This will be
    **	twice the number required to fill all teams.
    */
    for (i = 0; i < Teams.Count(); i++) {
        TeamClass* tptr = Teams[i];
        if (tptr != NULL) {

            int val = tptr->field_40;

            if (((tptr->Class->IsReinforcable && !tptr->IsFullStrength) || (!tptr->IsForcedActive && !tptr->IsHasBeen)) && tptr->House == this) {
                DynamicVectorClass<const TechnoTypeClass*> _members;
                tptr->Team_Members(_members);

                for (int subindex = 0; subindex < _members.Count(); subindex++) {

                    TechnoTypeClass const* memtype = (TechnoTypeClass const*)_members[subindex];

                    if (memtype->RTTI == RTTI_INFANTRYTYPE) {
                        InfantryTypeClass const * infantrytype = reinterpret_cast<InfantryTypeClass const*>(memtype);
                        counter[infantrytype->HeapID]++;
                        if (val < value[infantrytype->HeapID]) {
                            value[infantrytype->HeapID] = val;
                        }
                    }
                }
            }
        }
    }

    /*
    **	Reduce the theoretical maximum by the actual number of objects currently
    **	in play.
    */
    for (int oindex = 0; oindex < Infantry.Count(); oindex++) {
        InfantryClass* obj = Infantry[oindex];
        if (obj != NULL && obj->Is_Recruitable(this) && counter[obj->Class->HeapID] > 0) {
            counter[obj->Class->HeapID]--;
        }
    }

    /*
    **	Pick to build the most needed object but don't consider those object that
    **	can't be built because of scenario restrictions or insufficient cash.
    */
    int bestval = -1;
    int bestcount = 0;
    InfantryType lasttype = INFANTRY_NONE;
    int lastval = 0x7FFFFFFF;
    InfantryType bestlist[std::size(counter)];
    for (InfantryType type = InfantryType(0); type < InfantryTypes.Count(); type++) {
        if (counter[type] > 0 && Can_Build(InfantryTypes[type], false, false) && InfantryTypes[type]->Cost_Of(this) <= Available_Money()) {
            if (bestval == -1 || bestval < counter[type]) {
                bestval = counter[type];
                bestcount = 0;
            }
            bestlist[bestcount++] = type;

            if (lasttype == OBJNONE || value[type] < lastval) {
                lasttype = type;
                lastval = value[type];
            }
        }
    }

    if (Random_Pick2(0, 0x7FFFFFFE) < Rule->FillEarliestTeamProbability[Difficulty] / 100.0) {
        BUILD = lasttype;
    }
    else {
        /*
        **	The object type to build is now known. Fetch a pointer to the techno type class.
        */
        if (bestcount) {
            BUILD = bestlist[Random_Pick(0, bestcount - 1)];
        }
    }

    return TICKS_PER_SECOND;
}

int AdvancedAI_AI_Aircraft(HouseClass* house)
{
    auto extension = Extension::Fetch(house);

    int id = 0;
    TeamClass* team = extension->Get_Team_In_Production(id, RTTI_AIRCRAFT, PRODFLAG_NONE);

    if (team == nullptr) {
        return TICKS_PER_SECOND;
    }

    TeamClassExtension* teamext = Extension::Fetch(team);

    AircraftType mostvaluable = AIRCRAFT_NONE;
    int highestvalue = 0;

    DynamicVectorClass<BuildingTypeClass const*> owned_buildings;
    Extension::Fetch(house)->Fill_Owned_Buildings_List(owned_buildings);

    // Need to hack around the owners for GDI aircraft...
    BuildingTypeClass* ourhelipad = nullptr;
    for (int i = 0; i < Rule->BuildHelipad.Count(); i++)
    {
        if ((Rule->BuildHelipad[i]->Ownable & (1 << house->ActLike)) == (1 << house->ActLike)) {
            ourhelipad = Rule->BuildHelipad[i];
            break;
        }
    }

    if (ourhelipad == nullptr) {
        return TICKS_PER_HOUR;
    }

    HouseClassExtension* houseext = Extension::Fetch(house);
    bool debugprint = Frame > houseext->LastAircraftValueDebugPrintFrame + 10000;

    if (debugprint) {
        houseext->LastAircraftValueDebugPrintFrame = Frame;
        DEBUG_INFO("AdvAI: House %d: Aircraft values on frame %d:\n", (int)house->HeapID, Frame);
    }

    // Build a list of all aircraft that we can build, alongside their scores for our current tactic.
    for (int i = 0; i < AircraftTypes.Count(); i++)
    {
        AircraftTypeClass* airtype = AircraftTypes[i];
        AircraftTypeClassExtension* airtypeext = Extension::Fetch(airtype);

        if (airtypeext->Buildability != TechnoTypeBuildability::BUILDABILITY_HUMAN_ONLY && 
            (airtype->Ownable & ourhelipad->Ownable) > 0 && airtype->Level <= house->Control.TechLevel && (i == 0 || // Super special dumb hack for the Orca so it's available from airfields
            reinterpret_cast<HouseClassExt*>(house)->_AI_Has_Prerequisites(airtype, owned_buildings, owned_buildings.Count()))) {

            if (airtype->BuildLimit > 0 && house->AQuantity.Value(airtype->HeapID) >= airtype->BuildLimit)
                continue;

            int value = teamext->AdvAI_Get_Object_Value_For_Team(airtype, debugprint);

            if (debugprint) {
                DEBUG_INFO("    %s: %d\n", airtype->IniName.c_str(), value);
            }

            if (value > highestvalue) {
                mostvaluable = airtype->HeapID;
                highestvalue = value;
            }
        }
    }

    if (mostvaluable != AIRCRAFT_NONE) {
        if (debugprint) {
            DEBUG_INFO("    Selected: %s\n", AircraftTypes[mostvaluable]->IniName.c_str());
        }

        house->BuildAircraft = mostvaluable;
    }

    return TICKS_PER_SECOND;
}

int HouseClassExt::_AI_Aircraft(void)
{
    typedef AircraftType OBJTYPE;
    typedef AircraftClass CLASS;
    typedef AircraftTypeClass TYPECLASS;

    OBJTYPE& BUILD = BuildAircraft;
    const RTTIType OBJRTTI = RTTI_AIRCRAFTTYPE;
    const OBJTYPE OBJNONE = AIRCRAFT_NONE;
    const DynamicVectorClass<CLASS*>& CLASSVECTOR = Aircrafts;
    const DynamicVectorClass<TYPECLASS*>& TYPECLASSVECTOR = AircraftTypes;

    if (BUILD != OBJNONE) return(TICKS_PER_SECOND);

    if (RuleExtension->AdvancedAIUnitProduction) {
        return AdvancedAI_AI_Aircraft(this);
    }

    int i;
    int counter[100];
    int value[std::size(counter)];
    memset(counter, 0x00, sizeof(counter));
    for (i = 0; i < std::size(value); i++) {
        value[i] = 0x7FFFFFFF;
    }

    /*
    **	Build a list of the maximum of each type we wish to produce. This will be
    **	twice the number required to fill all teams.
    */
    for (i = 0; i < Teams.Count(); i++) {
        TeamClass* tptr = Teams[i];
        if (tptr != NULL) {

            int val = tptr->field_40;

            if (((tptr->Class->IsReinforcable && !tptr->IsFullStrength) || (!tptr->IsForcedActive && !tptr->IsHasBeen)) && tptr->House == this) {
                DynamicVectorClass<const TechnoTypeClass*> _members;
                tptr->Team_Members(_members);

                for (int subindex = 0; subindex < _members.Count(); subindex++) {

                    TYPECLASS const* memtype = (TYPECLASS const*)_members[subindex];

                    if (memtype->RTTI == OBJRTTI) {
                        counter[memtype->HeapID]++;
                        if (val < value[memtype->HeapID]) {
                            value[memtype->HeapID] = val;
                        }
                    }
                }
            }
        }
    }

    /*
    **	Reduce the theoretical maximum by the actual number of objects currently
    **	in play.
    */
    for (int oindex = 0; oindex < CLASSVECTOR.Count(); oindex++) {
        CLASS* obj = CLASSVECTOR[oindex];
        if (obj != NULL && obj->Is_Recruitable(this) && counter[obj->Class->HeapID] > 0) {
            counter[obj->Class->HeapID]--;
        }
    }

    /*
    **	Pick to build the most needed object but don't consider those object that
    **	can't be built because of scenario restrictions or insufficient cash.
    */
    int bestval = -1;
    int bestcount = 0;
    OBJTYPE lasttype = OBJNONE;
    int lastval = 0x7FFFFFFF;
    OBJTYPE bestlist[std::size(counter)];
    for (OBJTYPE type = OBJTYPE(0); type < TYPECLASSVECTOR.Count(); type++) {
        if (counter[type] > 0 && Can_Build(TYPECLASSVECTOR[type], false, false) && TYPECLASSVECTOR[type]->Cost_Of(this) <= Available_Money()) {
            if (bestval == -1 || bestval < counter[type]) {
                bestval = counter[type];
                bestcount = 0;
            }
            bestlist[bestcount++] = type;

            if (lasttype == OBJNONE || value[type] < lastval) {
                lasttype = type;
                lastval = value[type];
            }
        }
    }

    if (Random_Pick2(0, 0x7FFFFFFE) < Rule->FillEarliestTeamProbability[Difficulty] / (100.0)) {
        BUILD = lasttype;
    }
    else {
        /*
        **	The object type to build is now known. Fetch a pointer to the techno type class.
        */
        if (bestcount) {
            BUILD = bestlist[Random_Pick(0, bestcount - 1)];
        }
    }

    return(TICKS_PER_SECOND);
}


/**
 *  Handles expert AI processing.
 *
 *  @author: 09/29/1995 JLB - Created.
 *           10/11/2024 ZivDero - Adjustments for Tiberian Sun
 */
int HouseClassExt::_Expert_AI()
{
    AdvAI_HouseClass_Expert_AI(this);

    /**
     *  If there is no enemy assigned to this house, then assign one now. The
     *  enemy that is closest is picked. However, don't pick an enemy if the
     *  base has not been established yet.
     */
    if (ExpertAITimer == 0) {
        if (Enemy == HOUSE_NONE && Session.Type != GAME_NORMAL && !Class->IsMultiplayPassive && Center != COORD_NONE) {
            int close = INT_MAX;
            HouseClass* enemy = nullptr;

            for (int i = 0; i < Houses.Count(); i++) {
                HouseClass* house = Houses[i];
                if (house != this && !house->Class->IsMultiplayPassive && !house->IsDefeated && !Is_Ally(house)) {

                    /**
                     *  Determine a priority value based on distance to the center of the
                     *  candidate base. The higher the value, the better the candidate house
                     *  is to becoming the preferred enemy for this house.
                     */
                    const int value = Distance(Center, house->Center);

                    /**
                     *  Compare the calculated value for this candidate house and if it is
                     *  greater than the previously recorded maximum, record this house as
                     *  the prime candidate for enemy.
                     */
                    if (value < close) {
                        close = value;
                        enemy = house;
                    }
                }
            }

            /**
             *  Record this closest enemy base as the first enemy to attack.
             */
            if (enemy) {
                Add_Anger(1, enemy);
            }
        }
    }

    /**
     *  If the current enemy no longer has a base or is defeated, then don't consider
     *  that house a threat anymore. Clear out the enemy record and then try
     *  to find a new enemy.
     */
    if (Enemy != HOUSE_NONE) {
        HouseClass* h = Houses[Enemy];

        if (h->IsDefeated || Is_Ally(h)) {
            Clear_Anger(h);
            Enemy = HOUSE_NONE;
        }
    }

    /**
     *  Use any ready super weapons.
     */
    if (Session.Type != GAME_NORMAL || IQ >= Rule->IQSuperWeapons) {
        AI_Super_Weapons();
    }

    /**
     *  House state transition check occurs here. Transitions that occur here are ones
     *  that relate to general base condition rather than specific combat events.
     *  Typically, this is limited to transitions between normal buildup mode and
     *  broke mode.
     */
    if (State == STATE_ENDGAME) {
        Fire_Sale();
        All_To_Hunt();
    } else {
        if (State == STATE_BUILDUP) {
            if (Available_Money() < 25) {
                State = STATE_BROKE;
            }
        }
        if (State == STATE_BROKE) {
            if (Available_Money() >= 25) {
                State = STATE_BUILDUP;
            }
        }
        if (State == STATE_ATTACKED && LATime + TICKS_PER_MINUTE < Frame) {
            State = STATE_BUILDUP;
        }
        if (State != STATE_ATTACKED && LATime + TICKS_PER_MINUTE > Frame) {
            State = STATE_ATTACKED;
        }
    }

    if (Session.Type != GAME_NORMAL && !ScenExtension->IsUseMPAIBaseNodes) {

        /**
         *  Records the urgency of all actions possible.
         */
        UrgencyType urgency[STRATEGY_COUNT];
        StrategyType strat;
        for (strat = STRATEGY_FIRST; strat < STRATEGY_COUNT; strat++) {
            urgency[strat] = URGENCY_NONE;

            switch (strat) {
            case STRATEGY_FIRE_SALE:
                urgency[strat] = Check_Fire_Sale();
                break;

            case STRATEGY_RAISE_MONEY:
                urgency[strat] = Check_Raise_Money();
                break;

            default:
                urgency[strat] = URGENCY_NONE;
                break;
            }
        }

        /**
         *  Performs the action required for each of the strategies that share
         *  the most urgent category. Stop processing if any strategy at the
         *  highest urgency performed any action. This is because higher urgency
         *  actions tend to greatly affect the lower urgency actions.
         */
        for (UrgencyType u = URGENCY_CRITICAL; u >= URGENCY_LOW; u--) {
            bool acted = false;

            for (strat = STRATEGY_FIRST; strat < STRATEGY_COUNT; strat++) {
                if (urgency[strat] == u) {
                    switch (strat) {
                    case STRATEGY_FIRE_SALE:
                        acted |= AI_Fire_Sale(u);
                        break;

                    case STRATEGY_RAISE_MONEY:
                        acted |= AI_Raise_Money(u);
                        break;

                    default:
                        break;
                    }
                }
            }
        }
    }

    return TICKS_PER_SECOND * 7 + Random_Pick(1, TICKS_PER_SECOND / 2);
}


/**
 *  Patch to optionally disable Tiberium storage.
 *
 *  @author: ZivDero, tomsons26, Rampastring
 */
void HouseClassExt::_Harvested(int tiberium, TiberiumType slot)
{
    PointTotal += tiberium * 5;

    if ((Session.Type != GAME_NORMAL && !IsHuman) || !RuleExtension->IsTiberiumStorage) {
        Credits += tiberium * Tiberiums[slot]->CreditValue;
    }
    else {
        long oldcap = Capacity;
        long oldtib = Tiberium.Get_Total_Amount();

        if (tiberium + Tiberium.Get_Total_Amount() > Capacity) {
            tiberium = Capacity - Tiberium.Get_Total_Amount();
        }

        for (int index = 0; index < Buildings.Count(); index++) {
            BuildingClass* b = Buildings[index];
            if (b && b->IsDown && b->House == this) {
                if (b->Class->Storage > 0) {
                    while (tiberium > 0 && b->Class->Storage > b->Storage.Get_Total_Amount()) {
                        b->Storage.Increase_Amount(1, slot);
                        Tiberium.Increase_Amount(1, slot);
                        tiberium--;
                    }
                }
            }
        }

        Silo_Redraw_Check(oldtib, oldcap);
    }
}


/**
 *  #issue-177
 *
 *  Checks if the AI house has the capability to make money. Adjusted to
 *  use the entire Build* and HarvesterUnit lists.
 *
 *  @author: ZivDero
 */
bool HouseClassExt::_Can_Make_Money()
{
    const int credits = Available_Money();
    const int ref_cost = Get_First_Ownable(Rule->BuildRefinery)->Cost_Of(this);
    const int harv_cost = Get_First_Ownable(Rule->HarvesterUnit)->Cost_Of(this);

    const int ref_count = Count_Owned(Rule->BuildRefinery);
    const int harv_count = Count_Owned(Rule->HarvesterUnit);

    /**
     *  If we don't have any refineries, building one is a priority.
     */
    if (ref_count == 0)
        return credits > ref_cost;

    /**
     *  If we have a refinery and a harvester, all's well.
     */
    if (harv_count)
        return true;

    const bool has_factory = Count_Owned(Rule->BuildWeapons) > 0;
    const int factory_cost = Get_First_Ownable(Rule->BuildWeapons)->Cost_Of(this);

    /**
     *  If we have a refinery, but not a harvester, see if
     *  we can build one if we have a factory.
     */
    if (has_factory && credits >= harv_cost)
        return true;

    /**
     *  And if we don't have a factory, see if we can build one.
     */
    if (credits >= harv_cost + factory_cost)
        return true;

    /**
     *  Worst case, see if we can build a new refinery to get a free harvester.
     */
    if (credits >= ref_cost)
        return true;

    return false;
}


/**
 *  #issue-177
 *
 *  Checks if the AI needs to urgently raise more money.
 *  Adjusted to use the entire Build* and HarvesterUnit lists.
 *
 *  @author: ZivDero
 */
UrgencyType HouseClassExt::_Check_Raise_Money()
{
    UrgencyType urgency = URGENCY_NONE;

    /**
     *  Human players don't need AI to raise money for them.
     */
    const bool human = Session.Type == GAME_NORMAL ? Is_Player_Control() : IsHuman;
    if (human)
        return urgency;

    /**
     *  If we can afford to have a harvester and a refinery, all is well.
     */
    if (Can_Make_Money())
        return urgency;

    /**
     *  See if we have a refinery.
     */
    if (Count_Owned(Rule->BuildRefinery))
    {
        /**
         *  Iterate all the buildings and check if we have a refinery under construction.
         *  If so, we don't need raise money, since we'll get a free harvester.
         */
        for (int i = 0; i < Buildings.Count(); i++)
        {
            BuildingClass* building = Buildings[i];
            if (building->House == this)
            {
                if (Rule->BuildRefinery.Is_Present(building->Class) && building->Get_Mission() == MISSION_CONSTRUCTION)
                    return urgency;

                urgency = URGENCY_NONE;
            }
        }

        /**
         *  Check if what we're currently building is a harvester.
         *  If it's not and we don't have enough money to build one,
         *  we've got minor issues.
         */
        const UnitTypeClass* harvester = Get_First_Ownable(Rule->HarvesterUnit);
        if (BuildUnit != harvester->HeapID)
        {
            if (Available_Money() < harvester->Cost_Of(this))
                urgency++;

            return urgency;
        }

        /**
         *  Check all the factories and find which is building our harvester.
         *  If we haven't got enough money to complete contruction, we've got issues.
         */
        for (int i = 0; i < Factories.Count(); i++)
        {
            const FactoryClass* factory = Factories[i];
            if (factory && factory->House == this)
            {
                ObjectClass* obj = factory->Get_Object();
                if (obj && obj->What_Am_I() == RTTI_UNIT
                    && Rule->HarvesterUnit.Is_Present((UnitTypeClass*)(obj->TClass)))
                {
                    if (Available_Money() < factory->Balance)
                        urgency++;

                    return urgency;

                }
            }
        }
    }
    else
    {
        /**
         *  Check if what we're currently building is a refinery.
         *  If it's not and we don't have enough money to build one,
         *  we've got minor issues.
         */
        const BuildingTypeClass* refinery = Get_First_Ownable(Rule->BuildRefinery);
        if (BuildStructure != refinery->HeapID)
        {
            if (Available_Money() < refinery->Cost_Of(this))
                urgency++;

            return urgency;
        }

        /**
         *  Check all the factories and find which is building our refinery.
         *  If we haven't got enough money to complete contruction, we've got issues.
         */
        for (int i = 0; i < Factories.Count(); i++)
        {
            const FactoryClass* factory = Factories[i];
            if (factory && factory->House == this)
            {
                ObjectClass* obj = factory->Get_Object();
                if (obj && obj->What_Am_I() == RTTI_BUILDING
                    && Rule->BuildRefinery.Is_Present((BuildingTypeClass*)(obj->TClass)))
                {
                    if (Available_Money() < factory->Balance)
                        urgency++;

                    return urgency;
                }
            }
        }
    }

    /**
     *  Something weird has happened, it's surely not good.
     */
    urgency++;
    return urgency;
}


/**
 *  A house is defeated in multiplayer.
 *
 *  @author: 05/25/1995 BRR - Created
 *           29/10/2024 ZivDero - Adjustments for Tiberian Sun
 *           19/07/2026 Rampastring - Correct local/team win and loss flagging
 */
void HouseClassExt::_MPlayer_Defeated()
{
    char txt[80];

    /**
     *  Set the defeat flag for this house
     */
    IsDefeated = true;

    /**
     *  If this is a computer controlled house, then all computer controlled
     *  houses become paranoid.
     */
    if (IQ == Rule->MaxIQ && !Is_Human_Player() && Rule->IsComputerParanoid) {
        Computer_Paranoid();
    }

    /**
     *  Remove this house's flag & flag home cell
     */
    if (Special.IsCaptureTheFlag) {
        if (FlagLocation) {
            Flag_Remove(FlagLocation, true);
        } else {
            if (FlagHome != CELL_NONE) {
                Flag_Remove(&Map[FlagHome], true);
            }
        }
    }

    /**
     *  If harvester truce is on, remove all of this player's harvesters.
     */
    if (Session.Type != GAME_NORMAL && Scen->Special.IsHarvesterImmune) {
        for (int i = 0; i < Units.Count(); i++) {
            if (Units[i]->House == this && Units[i]->IsActive) {
                Units[i]->Delete_Me();
            }
        }
    }

    if (!Class->IsMultiplayPassive && !Extension::Fetch(this)->IsObserver)
    {
        std::snprintf(txt, std::size(txt), Fetch_String(TXT_PLAYER_DEFEATED), IniName.c_str());
        Session.Messages.Add_Message(nullptr, 0, txt, Scheme, TPF_6PT_GRAD | TPF_USE_GRAD_PAL | TPF_FULLSHADOW, Rule->MessageDelay * TICKS_PER_MINUTE);

        if (PlayerPtr == this) {
            Speak(VOX_YOU_HAVE_LOST);
        } else {
            Speak(VOX_PLAYER_DEFEATED);
        }

        DEBUG_INFO("MPlayer_Defeated() - Player {} has been defeated\n", IniName);
    }

    /**
     *  If the local player is been defeated, check if they should be given OBIWAN mode.
     */
    if (PlayerPtr->IsDefeated && !Extension::Fetch(PlayerPtr)->IsObserver && !Session.ObiWan) {

        /**
         *  With the spawner active, if Coach mode is enabled, players don't get vision.
         */
        bool obiwan = true;
        if (SessionExtension->ExtOptions.IsCoachMode) {
            obiwan = false;
        }

        /**
         *  Now check if the player has any player allies remaining.
         */
        for (int i = 0; i < Houses.Count(); i++) {
            HouseClass* hptr = Houses[i];
            if (!hptr->IsDefeated && !hptr->Class->IsMultiplayPassive && (hptr->Is_Ally(PlayerPtr) || PlayerPtr->Is_Ally(hptr))) {
                obiwan = false;
                break;
            }
        }

        /**
         *  - Set MPlayerObiWan, so I can only send messages to all players, and
         *    not just one (so I can't be obnoxiously omnipotent)
         *  - Reveal the map
         */
        if (obiwan) {
            Session.ObiWan = true;
            Map.Reveal_The_Map();
            PlayerPtr->RecalcRadar = true;
            HiddenSurface->Fill(0);
            Map.Flag_To_Redraw();
            DEBUG_INFO("MPlayer_Defeated() - Player {} has no allies left (OBIWAN MODE)\n", IniName);
        }
    }

    /**
     *  Find out how many players are left alive.
     */
    int num_alive = 0;
    int num_humans = 0;
    for (int i = 0; i < Houses.Count(); i++) {
        HouseClass* hptr = Houses[i];
        if (hptr && !hptr->IsDefeated && !hptr->Class->IsMultiplayPassive) {
            if (hptr->Is_Human_Player()) {
                num_humans++;
            }
            num_alive++;
        }
    }
    DEBUG_INFO("MPlayer_Defeated() - Alive = {}, Humans = {}\n", num_alive, num_humans);

    /**
     *  If all the houses left alive are allied with each other, then in reality
     *  there's only one player left:
     */
    bool all_allies = true;
    for (int i = 0; i < Houses.Count(); i++) {

        /**
         *  Get a pointer to this house
         */
        HouseClass* hptr = Houses[i];
        if (!hptr || hptr->IsDefeated || hptr->Class->IsMultiplayPassive) continue;

        /**
         *  Loop through all houses; if there's one left alive that this house
         *  isn't allied with, then all_allies will be false
         */
        for (int j = 0; j < Houses.Count(); j++) {
            HouseClass* hptr2 = Houses[j];
            if (!hptr2) {
                continue;
            }

            if (!hptr2->IsDefeated && !hptr2->Class->IsMultiplayPassive && (!hptr->Is_Ally(hptr2) || !hptr2->Is_Ally(hptr))) {
                all_allies = false;
                break;
            }
        }
        if (!all_allies) {
            break;
        }
    }

    /**
     *  If all houses left are allies, set 'num_alive' to 1; game over.
     */
    if (all_allies) {
        Session.SawCompletion = true;
        DEBUG_INFO("Saw game completion due to player defeat\n");
        DEBUG_INFO("MPlayer_Defeated() - All remaining players are allied\n");
        num_alive = 1;
    }

    /**
     *  If there's only one human player left or no humans left, the game is over.
     */
    if (!Extension::Fetch(this)->IsObserver) {
        if (num_alive == 1 || (num_humans == 0 && !SessionExtension->ExtOptions.IsContinueWithoutHumans && (!Session.Singleplayer_Game() || !Extension::Fetch(PlayerPtr)->IsObserver))) {
            IsToDie = false;

            /**
             *  Consider the local player victorious if they are still alive, or if
             *  they have a surviving human ally in a multiplayer team game.
             */
            bool localplayerwon = !PlayerPtr->IsDefeated;

            if (!localplayerwon && Session.Type != GAME_SKIRMISH) {
                DEBUG_INFO("MPlayer_Defeated: Local player is defeated, looking for allies.\n");

                for (int i = 0; i < Houses.Count(); i++) {
                    HouseClass* hptr = Houses[i];
                    if (!hptr || hptr->IsDefeated || !hptr->IsHuman || hptr->Class->IsMultiplayPassive) continue;

                    if (PlayerPtr->Is_Ally(hptr)) {
                        localplayerwon = true;
                        break;
                    }
                }
            }

            if (localplayerwon) {
                DEBUG_INFO("MPlayer_Defeated: Flagging local player as victorious.\n");
                PlayerPtr->Flag_To_Win(false);
            } else {
                DEBUG_INFO("MPlayer_Defeated: Flagging local player as lost.\n");
                PlayerPtr->Flag_To_Lose(false);
            }
        }
    }
}


/**
 *  Make the specified house an ally.
 *
 *  @author: 05/08/1995 JLB - Created
 *           29/10/2024 ZivDero - Adjustments for Tiberian Sun
 *           11/07/2026 Rampastring - Use Shush's extended sight range logic when revealing allied objects,
 *                                    don't reveal MultiplayPassive house objects in multiplayer
 */
void HouseClassExt::_Make_Ally(HouseClass* house)
{
    if (Is_Allowed_To_Ally(house)) {

        Allies |= (1L << house->HeapID);

        /**
         *  Don't consider the newfound ally to be an enemy -- of course.
         */
        Recalc_Threat_Regions();
        Clear_Anger(house);

        if (Enemy == house->HeapID) {
            Enemy = HOUSE_NONE;
        }

        if (ScenarioInit) {
            Control.Allies |= (1L << house->HeapID);
        }

        if (Session.Type != GAME_NORMAL || !ScenarioInit) {

            if (!ScenarioInit) {

                /**
                 *  An alliance with another human player will cause the computer
                 *  players (if present) to become paranoid.
                 */
                if (Is_Human_Player() && Rule->IsComputerParanoid && !house->Class->IsMultiplayPassive) {
                    Computer_Paranoid();
                }

                /**
                 *  Sweep through all techno objects and perform a cheeseball tarcom clear to ensure
                 *  that fighting will most likely stop when the cease fire begins.
                 */
                for (int index = 0; index < Logic.Count(); index++) {
                    ObjectClass* object = Logic[index];

                    if (object != nullptr && object->Is_Techno() && !object->IsInLimbo && object->Owner() == HeapID) {
                        TargetClass target = static_cast<TechnoClass*>(object)->TarCom;
                        if (target.Is_Valid() && target.As_Techno() != nullptr) {
                            if (Is_Ally(target.As_Techno())) {
                                static_cast<TechnoClass*>(object)->Assign_Target(nullptr);
                            }
                        }
                    }
                }

                if (Is_Human_Player() && Session.Type != GAME_NORMAL && !house->Class->IsMultiplayPassive) {

                    char buffer[80];
                    std::snprintf(buffer, std::size(buffer), Fetch_String(TXT_HAS_ALLIED), IniName, house->IniName);
                    Session.Messages.Add_Message(nullptr, 0, buffer, Scheme, TPF_6PT_GRAD | TPF_USE_GRAD_PAL | TPF_FULLSHADOW, TICKS_PER_MINUTE * Rule->MessageDelay);

                    if (Is_Player_Control()) {
                        Speak(VOX_ALLIANCE_FORMED);
                    }
                }
            }

            /**
             *  Cause all technos to be revealed to the house that has been
             *  allied with.
             */
            if (Rule->IsAllyReveal && house == PlayerPtr) {
                for (int index = 0; index < Technos.Count(); index++) {
                    TechnoClass const* t = Technos[index];

                    /**
                     *  If in multiplayer, don't reveal objects owned by MultiplayPassive houses.
                     *  This matches the behaviour of TechnoClass::Look.
                     */
                    if (!t->IsInLimbo && t->House == this && (!Class->IsMultiplayPassive || Session.Type == GAME_NORMAL)) {
                        int sight_range = Extension::Fetch(t)->Get_Sight_Range();

                        Map.Sight_From(t->Center_Coord(), sight_range, PlayerPtr);
                    }
                }
            }

            Map.Flag_To_Redraw();
        }
    }
}


/**
 *  #issue-177
 * 
 *  Allow the game to check BaseUnit for all pertinent entries for "Short Game".
 * 
 *  #NOTE: The code before this patch already checks if the house has
 *         any buildings first.
 * 
 *  @author: CCHyper, ZivDero
 */
DEFINE_HOOK(0x004BCEE7, _HouseClass_AI_Short_Game_BaseUnit_Patch, 0)
{
    GET(HouseClass *, this_ptr, ESI);

    /**
     *  Count all MCVs we own to see if the player should explode.
     */
    const int count = this_ptr->Count_Owned(RuleExtension->BaseUnit);

    if (count) {
        return 0x004BCF6E;
    }

    /**
     *  Blows up the house, marking the house as defeated.
     */
    return 0x004BCF60;
}


/**
 *  Patch for InstantSuperRechargeCommandClass
 * 
 *  @author: CCHyper
 */
DEFINE_HOOK(0x004BD30B, _HouseClass_Super_Weapon_Handler_InstantRecharge_Patch, 0)
{
    GET(HouseClass *, this_ptr, EDI);
    GET(SuperClass *, special, ESI);

    bool is_player = false;
    if (this_ptr == PlayerPtr) {
        is_player = true;
    }

    if (Vinifera_DeveloperMode) {
        if (!special->IsReady) {

            /**
             *  If AIInstantBuild is toggled on, make sure this is a non-human AI house.
             */
            if (Vinifera_Developer_AIInstantSuperRecharge && !this_ptr->Is_Human_Player() && this_ptr != PlayerPtr) {
                special->Forced_Charge(is_player);

            /**
             *  If InstantBuild is toggled on, make sure the local player is a human house.
             */
            } else if (Vinifera_Developer_InstantSuperRecharge && this_ptr->Is_Human_Player() && this_ptr == PlayerPtr) {
                special->Forced_Charge(is_player);

            /**
             *  If the AI has taken control of the player house, it needs a special
             *  case to handle the "player" instant recharge mode.
             */
            } else if (Vinifera_Developer_InstantSuperRecharge) {
                if (Vinifera_Developer_AIControl && this_ptr == PlayerPtr) {
                    
                    special->Forced_Charge(is_player);
                }
            }

        }

    }

    /**
     *  Stolen bytes/code.
     */
    if (!special->AI(is_player)) {
        return 0x004BD332;
    }

    return 0x004BD320;
}


/**
 *  Patch for BuildCheatCommandClass
 * 
 *  @author: CCHyper
 */
DEFINE_HOOK(0x004BBD26, _HouseClass_Can_Build_BuildCheat_Patch, 8)
{
    GET(HouseClass *, this_ptr, EBP);
    GET_STACK(TechnoTypeClass *, objecttype, 0x30);

    if (Vinifera_DeveloperMode && Vinifera_Developer_BuildCheat) {

        /**
         *  AI houses have access to everything, so we can just
         *  filter to the human houses only.
         */
        if (this_ptr->IsHuman && this_ptr->IsPlayerControl) {

            /**
             *  Check that the object has this house set as one of its owners.
             *  if true, force this 
             */
            if ((1 << this_ptr->Class->HeapID & objecttype->Get_Ownable()) != 0) {
                //DEBUG_INFO("Forcing \"{}\" available.\n", objecttype->IniName);
                return 0x004BBD17;
            }
        }
    }

    return 0;
}


/**
 *  #issue-611, #issue-715
 *
 *  Gets the number of queued objects when determining whether a cameo
 *  should be disabled.
 *
 *  Author: Rampastring
 */
int _HouseClass_ShouldDisableCameo_Get_Queued_Count(FactoryClass* factory, TechnoTypeClass* technotype)
{
    int count = factory->Total_Queued(*technotype);
    TechnoClass* factoryobject = factory->Get_Object();

    if (factoryobject == nullptr || count == 0) {
        return 0;
    }

    /**
     *  Check that the factory is trying to create the object that the player is trying to queue
     *  If not, we don't need to mess with the count
     */
    if (factoryobject->TClass != technotype) {
        return count;
    }

    /**
     *  #issue-611
     *
     *  If the object has a build limit, then reduce count by 1.
     *  In this state, the object is taken into account twice: in the object trackers
     *  and in the factory, resulting in the player being able to queue one object less
     *  than BuildLimit allows.
     */
    if (technotype->BuildLimit > 0) {
        count--;
    }

    /**
    *  #issue-715
    *
    *  If the object can transform into another object through our special logic,
    *  then check that doing so doesn't allow circumventing build limits
    */
    if (technotype->RTTI == RTTI_UNITTYPE) {
        UnitTypeClass* unittype = reinterpret_cast<UnitTypeClass*>(technotype);
        UnitTypeClassExtension* unittypeext = Extension::Fetch(unittype);

        if (unittype->DeploysInto == nullptr && unittypeext->TransformsInto != nullptr) {
            count += factory->House->UQuantity.Value((UnitType)unittypeext->TransformsInto->Fetch_Heap_ID());
        }
    }

    return count;
}


/**
 *  #issue-611 #issue-715
 *
 *  Fixes the game allowing the player to queue one unit too few
 *  when a unit has BuildLimit > 1.
 *
 *  Also updates the build limit logic with unit queuing to
 *  take our unit transformation logic into account.
 */
DEFINE_HOOK(0x004CB777, _HouseClass_ShouldDisableCameo_BuildLimit_Fix, 0)
{
    GET(FactoryClass*, factory, ECX);
    GET(TechnoTypeClass*, technotype, ESI);

    int queuedcount = _HouseClass_ShouldDisableCameo_Get_Queued_Count(factory, technotype);
    R->EAX(queuedcount);

    return 0x004CB77D;
}


/**
 *  #issue-715
 *
 *  Take vehicles that can transform into other vehicles into acccount when
 *  determining whether a build limit has been met/exceeded.
 *  Otherwise these kinds of units could be used to bypass build limits
 *  (build a limited vehicle, transform it, now you can build another vehicle).
 *
 *  Author: Rampastring
 */
DEFINE_HOOK(0x004BC187, _HouseClass_Can_Build_BuildLimit_Handle_Vehicle_Transform, 0)
{
    GET(UnitTypeClass*, unittype, EDI);
    GET(HouseClass*, house, EBP);

    UnitTypeClassExtension* unittypeext = Extension::Fetch(unittype);

    /**
     *  Stolen bytes / code.
     */
    int objectcount = house->UQuantity.Value((UnitType)unittype->Fetch_Heap_ID());

    /**
     *  Check whether this unit can deploy into a building.
     *  If it can, increment the object count by the number of buildings.
     */
    if (unittype->DeploysInto != nullptr) {
        objectcount += house->BQuantity.Value((StructType)unittype->DeploysInto->Fetch_Heap_ID());
    }
    else if (unittypeext->TransformsInto != nullptr) {

        /**
         *  This unit can transform into another unit, increment the object count
         *  by the number of transformed units.
         */
        objectcount += house->UQuantity.Value((UnitType)unittypeext->TransformsInto->Fetch_Heap_ID());
    }

    R->ESI(objectcount);

    return 0x004BC1B9;
}


/**
 *  #issue-994
 *
 *  Fixes a bug where a superweapon was enabled in non-suspended mode
 *  when the scenario was started with a pre-placed powered-down superweapon
 *  building on the map.
 *
 *  Author: Rampastring
 */
DEFINE_HOOK(0x004CB6C1, _HouseClass_Enable_SWs_Check_For_Building_Power, 6)
{
    GET(int, quiet, EAX);
    GET(BuildingClass*, building, ESI);

    if (!building->IsOn)
    {
        /**
         *  Enable the superweapon in suspended mode.
         */
        R->EAX(true);
    }
    else
    {
        /**
         *  Enable the superweapon in non-suspended mode.
         */
        R->EAX(false);
    }

    /**
     *  Continue the SW enablement process.
     */
    return 0;
}


/**
 *  Checks if the TechnoType can be built by this house based on RequiredHouses and ForbiddenHouses, if set.
 *
 *  Author: ZivDero, Rampastring
 */
bool HouseClassExt::_Can_Build_Required_Forbidden_Houses(const TechnoTypeClass* techno_type)
{
    const auto technotypeext = Extension::Fetch(techno_type);

    if (technotypeext->RequiredHouses != -1 &&
        (technotypeext->RequiredHouses & 1 << ActLike) == 0)
    {
        return false;
    }

    if (technotypeext->ForbiddenHouses != -1 &&
        (technotypeext->ForbiddenHouses & 1 << ActLike) != 0)
    {
        return false;
    }

    return true;
}

/**
 *  Checks if the TechnoType can be built by this house based on Buildability.
 *
 *  Author: Rampastring
 */
bool HouseClassExt::_Can_Build_Buildability(const TechnoTypeClass* techno_type)
{
    const auto technotypeext = Extension::Fetch(techno_type);

    if (Is_Human_Player()) {
        if (technotypeext->Buildability == TechnoTypeBuildability::BUILDABILITY_AI_ONLY) {
            return false;
        }
    } else {
        if (technotypeext->Buildability == TechnoTypeBuildability::BUILDABILITY_HUMAN_ONLY) {
            return false;
        }
    }

    return true;
}


/**
 *  Reimplementation of HouseClass::Active_Remove.
 *
 *  @author: ZivDero
 */
void HouseClassExt::_Active_Remove(TechnoClass const* techno)
{
    if (techno->RTTI == RTTI_BUILDING) {
        int* fptr = Extension::Fetch(this)->Factory_Counter(((BuildingClass*)techno)->Class->ToBuild,
            Extension::Fetch(((BuildingClass*)techno)->Class)->IsNaval ? PRODFLAG_NAVAL : PRODFLAG_NONE);
        if (fptr != nullptr) {
            *fptr = *fptr - 1;
        }
    }
}


/**
 *  Reimplementation of HouseClass::Active_Add.
 *
 *  @author: ZivDero
 */
void HouseClassExt::_Active_Add(TechnoClass const* techno)
{
    if (techno->RTTI == RTTI_BUILDING) {
        int* fptr = Extension::Fetch(this)->Factory_Counter(((BuildingClass*)techno)->Class->ToBuild,
            Extension::Fetch(((BuildingClass*)techno)->Class)->IsNaval ? PRODFLAG_NAVAL : PRODFLAG_NONE);
        if (fptr != nullptr) {
            *fptr = *fptr + 1;
        }
    }
}


/**
 *  #issue-531
 *
 *  Interception of Find_Build_Location. This allows us to find a suitable building
 *  location for the specific buildings, such as the Naval Yard.
 *
 *  @author: CCHyper, Rampastring
 */
Cell HouseClassExt::_Find_Build_Location(BuildingTypeClass* btype, int(__fastcall* callback)(int, Cell&, int, int), int a3)
{
    // Rampastring: fix edge case crash where this function is called with null btype
    if (btype == nullptr) {
        return Cell(0, 0);
    }

    /**
     *  Fix an edge case crash where this function is called with a null btype.
     *  @author: Rampastring
     */
    if (btype == nullptr) {
        return Cell(0, 0);
    }

    /**
     *  Find the type class extension instance.
     */
    BuildingTypeClassExtension* buildingtypeext = Extension::Fetch(btype);
    if (buildingtypeext && buildingtypeext->IsNaval) {

        DEV_DEBUG_INFO("Find_Build_Location({}): Searching for Naval Yard \"{}\" build location...\n", IniName, btype->Name());

        Cell cell(0, 0);

        /**
         *  Get the cell footprint for the Naval Yard, then add a safety margin of 2.
         */
        int area_w = btype->Width() + 2;
        int area_h = btype->Height() + 2;

        /**
         *  find a nearby location from the center of the base that fits our naval yard.
         */
        Cell found_cell = Map.Nearby_Location(Center.As_Cell(), SPEED_FLOAT, -1, MZONE_NORMAL, false, Point2D(area_w, area_h));
        if (found_cell != CELL_NONE) {

            DEV_DEBUG_INFO("Find_Build_Location({}): Found possible Naval Yard location at {},{}...\n", IniName, found_cell.X, found_cell.Y);

            /**
             *  Iterate over all owned construction yards and find the first that is closest to our cell.
             */
            for (int i = 0; i < ConstructionYards.Count(); ++i) {
                BuildingClass* conyard = ConstructionYards[i];
                if (conyard) {

                    Coord conyard_coord = conyard->Center_Coord();
                    Coord found_coord = Map[found_cell].Center_Coord();

                    /**
                     *  Is this location close enough to the construction yard for us to use?
                     */
                    if (Distance(conyard_coord, found_coord) <= Cell_To_Lepton(RuleExtension->AINavalYardAdjacency)) {
                        DEV_DEBUG_INFO("Find_Build_Location({}): Using location {},{} for Naval Yard.\n", IniName, found_cell.X, found_cell.Y);
                        cell = found_cell;
                        break;
                    }
                }
            }
        }

        if (cell == CELL_NONE) {
            DEV_DEBUG_WARNING("Find_Build_Location({}): Failed to find suitable location for \"{}\"!\n", IniName, btype->Name());
        }

        return cell;

    }

    /**
     *  Call the original function to find a location for land buildings.
     */
    return HouseClass::Find_Build_Location(btype, callback, a3);
}

bool _Can_Build_Additional_Conditions_Helper(HouseClassExt* this_ptr, TechnoTypeClass* techno_type)
{
    // For some reason the compiler can use stack space without this method
    return this_ptr->_Can_Build_Required_Forbidden_Houses(techno_type) && this_ptr->_Can_Build_Buildability(techno_type);
}

/**
 *  Adds a check to Can_Build to check for RequiredHouses and ForbiddenHouses
 *
 *  Author: ZivDero
 */
DEFINE_HOOK(0x004BBC74, _Can_Build_Required_Forbidden_Houses_Patch, 9)
{
    GET(TechnoTypeClass*, techno_type, EDI);
    GET(HouseClassExt*, this_ptr, EBP);

    bool can_build = _Can_Build_Additional_Conditions_Helper(this_ptr, techno_type);
    if (!can_build) {
        //return false;
        return 0x004BBC9A;
    }

    // Continue Can_Build
    return 0;
}


/**
 *  Allow to skip the check for the MCV's ActLike.
 *
 *  Author: ZivDero
 */
DEFINE_HOOK(0x004BC0B7, _HouseClass_Can_Build_Multi_MCV_Patch, 6)
{
    if (RuleExtension->IsMultiMCV) {
        return 0x004BC102;
    }

    return 0;
}


/**
 *  Handy macro for the functions below.
 */
#define WARN_AND_EXIT(funcname) { \
    DEBUG_FATAL("The legacy version of " STRINGIZE(funcname) " has been called! If you see this, please notify the developers. The game will now exit.\n"); \
    DEBUG_FATAL("Return address: {}\n", _ReturnAddress()); \
    WWMessageBox().Process("The legacy version of " STRINGIZE(funcname) " has been called! If you see this, please notify the developers. The game will now exit.", 0, TXT_OK); \
    Emergency_Exit(0); } \


/**
 *  The below are dummies for the functions that have been completely supplanted by our extension functions.
 *  These ought not to be used.
 */
FactoryClass* HouseClassExt::_Fetch_Factory(RTTIType rtti)
{
    WARN_AND_EXIT(HouseClass::Fetch_Factory);
    return nullptr;
}

void HouseClassExt::_Set_Factory(RTTIType rtti, FactoryClass* factory)
{
    WARN_AND_EXIT(HouseClass::Set_Factory);
}

int* HouseClassExt::_Factory_Counter(RTTIType rtti)
{
    WARN_AND_EXIT(HouseClass::Factory_Counter);
    return nullptr;
}

int HouseClassExt::_Factory_Count(RTTIType rtti) const
{
    WARN_AND_EXIT(HouseClass::Factory_Count);
    return 0;
}

ProdFailType HouseClassExt::_Suspend_Production(RTTIType type)
{
    WARN_AND_EXIT(HouseClass::Suspend_Production);
    return ProdFailType();
}

ProdFailType HouseClassExt::_Begin_Production(RTTIType type, int id, bool resume)
{
    WARN_AND_EXIT(HouseClass::Begin_Production);
    return ProdFailType();
}

ProdFailType HouseClassExt::_Abandon_Production(RTTIType type, int id)
{
    WARN_AND_EXIT(HouseClass::Abandon_Production);
    return ProdFailType();
}

bool HouseClassExt::_Place_Object(RTTIType type, Cell const& cell)
{
    WARN_AND_EXIT(HouseClass::Place_Object);
    return false;
}

void HouseClassExt::_Update_Factories(RTTIType type)
{
    WARN_AND_EXIT(HouseClass::Update_Factories);
}

TechnoTypeClass const* HouseClassExt::_Suggest_New_Object(RTTIType objecttype, bool kennel) const
{
    WARN_AND_EXIT(HouseClass::Suggest_New_Object);
    return nullptr;
}


/**
 *  The patches below replace calls to various HouseClass functions that we've re-implemented
 *  with calls to our extended implementations.
 *
 *  @author: ZivDero
 */
DEFINE_HOOK(0x004CB73D, _HouseClass_Exhausted_Build_Limit_Fetch_Factory_Patch, 0)
{
    GET(HouseClass*, this_ptr, EBX);
    GET(TechnoTypeClass const*, ttype, ESI);

    FactoryClass* factory = Extension::Fetch(this_ptr)->Fetch_Factory(ttype->RTTI, TechnoTypeClassExtension::Get_Production_Flags(ttype));
    R->ECX(factory);

    return 0x004CB773;
}


static void Update_Factories_Helper(BuildingClass* building)
{
    if (building->Class->ToBuild != RTTI_NONE) {
        BuildingTypeClassExtension* type_ext = Extension::Fetch(building->Class);
        HouseClassExtension* house_ext = Extension::Fetch(building->House);
        house_ext->Update_Factories(building->Class->ToBuild, type_ext->IsNaval ? PRODFLAG_NAVAL : PRODFLAG_NONE);
    }
}


DEFINE_HOOK(0x0042AACF, _BuildingClass_Unlimbo_Update_Factories_Patch, 0)
{
    GET(BuildingClass*, this_ptr, ESI);
    Update_Factories_Helper(this_ptr);
    return 0x0042AAEB;
}


DEFINE_HOOK(0x0042DFBE, _BuildingClass_Limbo_Update_Factories_Patch, 0)
{
    GET(BuildingClass*, this_ptr, EDI);
    Update_Factories_Helper(this_ptr);
    return 0x0042DFDA;
}


DEFINE_HOOK(0x0042FCF8, _BuildingClass_Captured_Update_Factories_Patch, 0)
{
    GET(BuildingClass*, this_ptr, ESI);
    GET_STACK(HouseClass*, oldowner, 0x60);
    if (this_ptr->Class->ToBuild != RTTI_NONE) {
        BuildingTypeClassExtension* type_ext = Extension::Fetch(this_ptr->Class);

        HouseClassExtension* old_house_ext = Extension::Fetch(oldowner);
        old_house_ext->Update_Factories(this_ptr->Class->ToBuild, type_ext->IsNaval ? PRODFLAG_NAVAL : PRODFLAG_NONE);

        HouseClassExtension* new_house_ext = Extension::Fetch(oldowner);
        new_house_ext->Update_Factories(this_ptr->Class->ToBuild, type_ext->IsNaval ? PRODFLAG_NAVAL : PRODFLAG_NONE);
    }

    return 0x0042FD28;
}


DEFINE_HOOK(0x00434C78, _BuildingClass_Read_INI_Update_Factories_Patch, 0)
{
    GET(BuildingClass*, this_ptr, ESI);
    Update_Factories_Helper(this_ptr);
    return 0x00434C94;
}


DEFINE_HOOK(0x00436855, _BuildingClass_Turn_On_Update_Factories_Patch, 0)
{
    GET(BuildingClass*, this_ptr, ESI);
    Update_Factories_Helper(this_ptr);
    return 0x0043686B;
}


DEFINE_HOOK(0x00436911, _BuildingClass_Turn_Off_Update_Factories_Patch, 0)
{
    GET(BuildingClass*, this_ptr, ESI);
    Update_Factories_Helper(this_ptr);
    return 0x0043692D;
}


/**
 *  This patch is part of adding an extra naval queue for the AI.
 *
 *  @author: ZivDero
 */
DEFINE_HOOK(0x004C0F40, _HouseClass_Raise_Money_BuildNavalUnit_Patch, 0)
{
    GET(HouseClass*, this_ptr, ESI);
    GET(bool, needs_harvester, ECX);

    HouseClassExtension* house_ext = Extension::Fetch(this_ptr);

    // Stolen instructions
    this_ptr->BuildUnit = UNIT_NONE;
    this_ptr->BuildInfantry = INFANTRY_NONE;
    this_ptr->BuildAircraft = AIRCRAFT_NONE;
    this_ptr->BuildStructure = STRUCT_NONE;

    // Clear naval production target
    house_ext->BuildNavalUnit = UNIT_NONE;

    if (needs_harvester) {
        return 0x004C0F5F;
    } else {
        return 0x004C0F87;
    }
}


/**
 *  Reimplementation of part of HouseClass::AI related to production,
 *  patched for naval queues.
 *
 *  @author: ZivDero
 */
void HouseClassExt::_Production_Check()
{
    auto house_ext = Extension::Fetch(this);

    bool b = BuildUnit == UNIT_NONE && BuildInfantry == INFANTRY_NONE && BuildAircraft == AIRCRAFT_NONE && house_ext->BuildNavalUnit == UNIT_NONE;

    if (BuildUnit != UNIT_NONE && !UnitTypes[BuildUnit]->Who_Can_Build_Me(true, true, true, this)) {
        b = true;
    }
    if (BuildInfantry != INFANTRY_NONE && !InfantryTypes[BuildInfantry]->Who_Can_Build_Me(true, true, true, this)) {
        b = true;
    }
    if (BuildAircraft != AIRCRAFT_NONE && !AircraftTypes[BuildAircraft]->Who_Can_Build_Me(true, true, true, this)) {
        b = true;
    }
    if (house_ext->BuildNavalUnit != UNIT_NONE && !UnitTypes[house_ext->BuildNavalUnit]->Who_Can_Build_Me(true, true, true, this)) {
        b = true;
    }

    if (b) {
        AI_Building();
    }
}

DEFINE_HOOK(0x004BD0E5, _HouseClass_AI_BuildNavalUnit_Patch, 0)
{
    GET(HouseClassExt*, this_ptr, ESI);
    this_ptr->_Production_Check();
    return 0x004BD1A1;
}


/**
 *  Reimplementation of of HouseClass::AI_Has_Prerequisites
 *
 *  @author: ZivDero
 */
bool HouseClassExt::_AI_Has_Prerequisites(const TechnoTypeClass* type, DynamicVectorClass<const BuildingTypeClass*>& owned, int ownedcount) const
{
    HouseClassExtension* ext = Extension::Fetch(this);

    return ext->_AI_Has_Prerequisites(type, owned, ownedcount);
}


/**
 *  Fixes an edge case bug where HouseClass::AI_Raise_Money can corrupt
 *  the house's Base Node vector by writing to the vector at index -1.
 *
 *  Author: Rampastring
 */
DEFINE_HOOK(0x004C0F87, _HouseClass_AI_Raise_Money_Fix_Memory_Corruption, 0)
{
    GET(HouseClass*, this_ptr, ESI);
    GET(StructType, buildingtype, EAX);
    int buildable_index;

    buildable_index = this_ptr->Base.Next_Buildable_Index(buildingtype);

    // Stolen bytes / code. Do not insert element to Base Nodes vector
    // if buildable index is 0.
    if (buildable_index == 0) {
        return 0x004C10BC;
    }

    // Bugfix: also do not insert element if buildable index is -1. (or below 0)
    if (buildable_index < 0) {
        return 0x004C10BC;
    }

    // Apply node index variable and also save it in eax,
    // original game code expects this
    R->Stack(0x1C, buildable_index);
    R->EAX(buildable_index);
    return 0x004C0F9F;
}

/**
 *  Helper function. Returns the value of an object for super-weapon targeting.
 */
int SuperTargeting_Evaluate_Object(HouseClass* house, HouseClass* enemy, TechnoClass* techno)
{
    int threat = -1;

    if (techno->Owner_HouseClass() == enemy) {

        if (!techno->TClass->IsLegalTarget) {
            return -1;
        }

        if (techno->Cloak == CLOAKED || (techno->RTTI == RTTI_BUILDING && reinterpret_cast<BuildingClass*>(techno)->TranslucencyLevel == 0xF)) {
            threat = Scen->RandomNumber(0, 100);
        }
        else {
            Cell targetcell = techno->Center_Coord().As_Cell();
            threat = Map.Cell_Threat(targetcell, house);
        }
    }

    return threat;
}


/**
 *  #issue-700
 *
 *  Custom implementation of the multi-missile super weapon AI targeting.
 *  This is functionally identical to the original game's function
 *  at 0x004CA4A0 aside from also considering vehicles as potential targets.
 *  The original function only evaluated buildings.
 *
 *  Author: Rampastring
 */
bool Vinifera_HouseClass_AI_Target_MultiMissile(HouseClass* this_ptr, SuperClass* super)
{
    if (this_ptr->Enemy == HOUSE_NONE) {
        return false;
    }

    ObjectClass* besttarget = nullptr;
    int          highestthreat = -1;
    HouseClass* enemyhouse = Houses[this_ptr->Enemy];

    for (int i = 0; i < Buildings.Count(); i++) {
        BuildingClass* target = Buildings[i];
        int threat = SuperTargeting_Evaluate_Object(this_ptr, enemyhouse, target);

        if (threat > highestthreat) {
            highestthreat = threat;
            besttarget = target;
        }
    }

    // AI improvement: also go through enemy units and infantry
    // This was originally not tied to Advanced AI, so it's always done if
    // Advanced AI is disabled

    if (!RuleExtension->AdvancedAINukeTargeting || Percent_Chance(50))
    {
        for (int i = 0; i < Units.Count(); i++) {
            UnitClass* target = Units[i];
            int threat = SuperTargeting_Evaluate_Object(this_ptr, enemyhouse, target);

            if (threat > highestthreat) {
                highestthreat = threat;
                besttarget = target;
            }
        }

        for (int i = 0; i < Infantry.Count(); i++) {
            InfantryClass* target = Infantry[i];
            int threat = SuperTargeting_Evaluate_Object(this_ptr, enemyhouse, target);

            if (threat > highestthreat) {
                highestthreat = threat;
                besttarget = target;
            }
        }
    }

    if (besttarget) {
        Coord center = besttarget->Center_Coord();
        Cell targetcell = center.As_Cell();
        int superid = this_ptr->SuperWeapon.ID(super);
        bool result = this_ptr->SuperWeapon[superid]->Discharged(this_ptr == PlayerPtr, targetcell);
        return result;
    }

    return false;
}


bool HouseClassExt::_AI_Target_MultiMissile(SuperClass* super)
{
    return Vinifera_HouseClass_AI_Target_MultiMissile(this, super);
}


bool Passes_Additional_Validity_Checks(TechnoTypeClass* technotype, HouseClass* house)
{
    if (technotype == nullptr) {
        // TechnoType is null, nothingness cannot be built.
        return false;
    }

    if (house->Is_Human_Player() &&
        (technotype->Level < 0 || technotype->Level > house->Control.TechLevel)) {
        // TechnoType cannot be built by this human player.
        return false;
    }

    return true;
}


/**
 *  Fixes a cheat in the original game where players were able to build
 *  unbuildable objects. Also fixes a bug where players could crash the game
 *  for everyone by attempting to build a nonexistent object.
 *
 *  Author: Rampastring
 */
DEFINE_HOOK(0x004BE218, _HouseClass_Begin_Production_Check_For_Unallowed_Buildables, 0)
{
    GET(TechnoTypeClass*, technotype, EAX);
    GET(HouseClass*, this_ptr, EBP);

    // Restore stolen code / bytes.
    R->EDI(technotype);

    if (!Passes_Additional_Validity_Checks(technotype, this_ptr)) {
        // Additional production validity check failed, exit from function.
        return 0x004BE28D;
    }

    BuildingClass* factorybuilding = technotype->Who_Can_Build_Me(false, true, true, this_ptr);

    // Continue original game code.
    R->EAX(factorybuilding);
    return 0x004BE22B;
}


DEFINE_HOOK(0x004BC9D4, _HouseClass_AI_AdvAI_Team_Production, 0)
{
    GET(HouseClass*, this_ptr, ESI);

    if (RuleExtension->AdvancedAIUnitProduction) {
        // Skip TeamDelay processing for Advanced AI.
        return 0x004BCAA0;
    }

    // Stolen bytes / code.
    if (this_ptr->TeamTime.Expired()) {
        return 0x004BC9FD;
    }

    return 0x004BCAA0;
}

/**
 *  Custom AI Super Weapon handler for DTA.
 *
 *  Author: ZivDero for original code, Rampastring for adjustments
 */
void HouseClassExt::_AI_Super_Weapons(void)
{
    if (!Is_Human_Player()) {
        for (int i = 0; i < SuperWeapon.Count(); i++) {
            SuperClass* super = SuperWeapon[i];

            if (super != NULL && super->Is_Ready()) {
                switch (super->Class->Type) {

                case SUPER_MULTI_MISSILE:
                    // Chrono Vortex
                    if (super->Class->Action == ACTION_EMPULSE)
                        Super_Weapon_Chem_Missile(super);

                    Super_Weapon_Multi_Missile(super);
                    break;

                case SUPER_ION_CANNON:
                    _AI_Ion_Cannon(super);
                    break;

                case SUPER_HUNTER_SEEKER:
                    Super_Weapon_Hunter_Seeker(super);
                    break;

                case SUPER_CHEM_MISSILE:
                    if (!RuleExtension->AdvancedAINukeTargeting)
                        Super_Weapon_Chem_Missile(super);
                    else
                        Super_Weapon_Multi_Missile(super);
                    break;

#pragma inline_depth(0) // workaround, AI_Drop_Pods gets inlined otherwise
                case SUPER_DROP_PODS:
                    Super_Weapon_Drop_Pods(super);
                    break;
#pragma inline_depth()

                case SUPER_EM_PULSE:
                    Super_Weapon_Chem_Missile(super);
                    break;

                default:
                    break;
                }
            }
        }
    }
}

/**
 *  Ion Cannon targeting replacement for the Advanced AI.
 *
 *  Author: ZivDero for original code, Rampastring for AdvAI algorithm and fixing a bug where the AI often fired at cloaked units.
 */
void HouseClassExt::_AI_Ion_Cannon(SuperClass* super)
{
    if (Enemy == HOUSE_NONE) return;

    HouseClass* enemy = Houses[Enemy];

    DynamicVectorClass<AbstractClass*> targets;
    int best = 0;

    if (!RuleExtension->AdvancedAIIonCannonTargeting)
    {
        for (int i = 0; i < Technos.Count(); i++) {
            int value = 0;
            bool valid = false;
            TechnoClass* techno = Technos[i];

            if (techno->House == enemy) {
                value = 1;
                if (techno->In_Which_Layer() == LAYER_GROUND && techno->IsActive && !techno->IsInLimbo) {
                    valid = true;
                }
                else if (Difficulty == DIFF_EASY) {
                    for (int j = 0; j < Factories.Count(); j++) {
                        FactoryClass* factory = Factories[j];
                        if (factory->Get_Object() == techno && factory->Fetch_Rate() != 0 && !factory->IsSuspended) {
                            valid = true;
                        }
                    }
                }

                switch (techno->Fetch_RTTI()) {
                case RTTI_INFANTRY:
                    if (techno->Strength <= Rule->IonCannonDamage) {
                        InfantryTypeClass const* inftype = ((InfantryClass*)techno)->Class;
                        if (inftype->IsEngineer) {
                            value = Rule->AIIonCannonEngineerValue[Difficulty];
                        }
                        else if (inftype->IsVehicleThief) {
                            value = Rule->AIIonCannonThiefValue[Difficulty];
                        }
                        else {
                            value = 2;
                        }
                    }
                    break;

                case RTTI_BUILDING:
                    value = 3;
                    if (techno->Strength <= Rule->IonCannonDamage) {
                        BuildingTypeClass const* builtype = ((BuildingClass*)techno)->Class;
                        if (builtype->ToBuild == RTTI_BUILDINGTYPE) {
                            value = Rule->AIIonCannonConYardValue[Difficulty];
                        }
                        else if (builtype->ToBuild == RTTI_UNITTYPE) {
                            value = Rule->AIIonCannonWarFactoryValue[Difficulty];
                        }
                        else if (builtype->Power > builtype->Drain) {
                            value = Rule->AIIonCannonPowerValue[Difficulty];
                        }
                        else if (builtype->IsBaseDefense) {
                            value = Rule->AIIonCannonBaseDefenseValue[Difficulty];
                        }
                        else if (builtype->IsPlug) {
                            value = Rule->AIIonCannonPlugValue[Difficulty];
                        }
                        else if (builtype->IsTemple) {
                            value = Rule->AIIonCannonTempleValue[Difficulty];
                        }
                        else if (builtype->IsHoverPad) {
                            value = Rule->AIIonCannonHelipadValue[Difficulty];
                        }
                        else {
                            value = 4;
                        }
                    }
                    break;

                case RTTI_UNIT:
                    if (techno->Strength <= Rule->IonCannonDamage) {
                        UnitTypeClass const* unittype = ((UnitClass*)techno)->Class;
                        if (unittype->IsToHarvest) {
                            value = Rule->AIIonCannonHarvesterValue[Difficulty];
                        }
                        else if (unittype->DeploysInto == Rule->BuildConst[0]) {
                            value = Rule->AIIonCannonMCVValue[Difficulty];
                        }
                        else if (unittype->MaxPassengers > 0) {
                            value = Rule->AIIonCannonAPCValue[Difficulty];
                        }
                        else {
                            value = 2;
                        }
                    }
                    break;
                }
            }

            if (techno->Cloak == CLOAKED || techno->RTTI == RTTI_BUILDING && ((BuildingClass*)techno)->TranslucencyLevel == 15) {
                value = Random_Pick(0, std::max(best - 1, 1));
            }

            if (valid) {
                if (value > best) {
                    targets.Clear();
                    targets.Add(techno);
                    best = value;
                }
                else if (value == best) {
                    targets.Add(techno);
                }
            }
        }
    }
    else
    {
        for (int i = 0; i < Technos.Count(); i++) {
            
            TechnoClass* techno = Technos[i];

            if (!techno->IsActive || techno->IsInLimbo || !techno->IsDown || techno->House != enemy || !techno->TClass->IsLegalTarget) {
                continue;
            }

            int icdamage = Rule->IonCannonDamage * Verses::Get_Modifier(techno->TClass->Armor, Rule->IonCannonWarhead);

            int value = techno->TClass->Cost;

            if (techno->TClass->Storage > 0) {
                value += techno->Storage.Get_Total_Value();
            }

            if ((techno->Cloak == CLOAKED || techno->RTTI == RTTI_BUILDING && ((BuildingClass*)techno)->TranslucencyLevel == 15)
                && !Map[techno->Center_Coord()].Sensed_By(HeapID)) 
            {
                value = 1;
            }
            else
            {
                int technostrength = (int)(techno->Strength * techno->ArmorBias);

                int modifier = 1;
                if (icdamage >= technostrength) {
                    value *= 200;
                }   
                else {
                    value *= (icdamage * 100) / technostrength;
                }

                if (techno->Crew.Is_Veteran())
                    value = (value * 3) / 2;
                else if (techno->Crew.Is_Elite())
                    value = value * 2;
            }

            int randomfactor = RuleExtension->AdvancedAIIonCannonRandomizationFactors[Difficulty];
            if (randomfactor > 1)
                value = Random_Pick(value / randomfactor, value * randomfactor);

            if (value > 0) {
                if (value > best) {
                    targets.Clear();
                    targets.Add(techno);
                    best = value;
                }
                else if (value == best) {
                    targets.Add(techno);
                }
            }
        }
    }

    if (targets.Count() > 0) {
        AbstractClass* target = targets[Random_Pick(0, targets.Count() - 1)];
        if (target != NULL) {
            Cell cell = target->Center_Coord().As_Cell();
            if (cell != CELL_NONE) {
                Place_Special_Blast((SuperWeaponType)SuperWeapon.ID(super), cell);
            }
        }
    }
}

/**
 *  Patches HouseClass::Recalc_Radar_Availability to allow Free Radar to still function when the player is in low power.
 *  This requires 'FreeRadarOnLowPower=yes' to be set under [General].
 *  Note that Ion Storms still turns off the radar regardless of this flag.
 *
 *  @author: JoyfulShush
 */
DEFINE_HOOK(0x004C958D, _HouseClass_Recalc_Radar_Availability_Free_Radar_Low_Power_Patch, 6)
{
    if (Scen->IsFreeRadar && RuleExtension->IsFreeRadarOnLowPower) {
        return 0x004C966A;
    }

    return 0;
}

/**
 *  #issue-177
 *
 *  Patches the check for if a house owns a Construction Yard to check the entire BuildConst list.
 *
 *  @author: ZivDero
 */
DEFINE_HOOK(0x004BCD5D, _HouseClass_AI_BuildConst_Patch, 0)
{
    GET(HouseClass*, this_ptr, ESI);

    if (this_ptr->Count_Owned(Rule->BuildConst) > 0) {
        return 0x004BCD85;
    }

    return 0x004BCE0B;
}


/**
 *  #issue-177
 *
 *  Patches the check for if a house owns a harvester to check the entire HarvesterUnit list.
 *
 *  @author: ZivDero
 */
DEFINE_HOOK(0x004BCF3A, _HouseClass_AI_Count_HarvesterUnit_Patch, 0)
{
    GET(HouseClass*, this_ptr, ESI);
    const int harv_count = this_ptr->Count_Owned(Rule->HarvesterUnit);

    R->EAX(harv_count);
    return 0x004BCF5A;
}


/**
 *  #issue-177
 *
 *  Patches the check for if a house is building a harvester to check the entire HarvesterUnit list.
 *
 *  @author: ZivDero
 */
DEFINE_HOOK(0x004BD0BC, _HouseClass_AI_Is_Building_Harvester_Unit_Patch, 0)
{
    GET(HouseClass*, this_ptr, ESI);

    if (this_ptr->BuildUnit != UNIT_NONE && Rule->HarvesterUnit.Is_Present(UnitTypes[this_ptr->BuildUnit])) {
        return 0x004BD0E5;
    }

    return 0x004BD0D7;
}


/**
 *  #issue-177
 *
 *  Patches the AI to correctly consider all refineries, weapons factories and harvesters.
 *
 *  @author: ZivDero
 */
DEFINE_HOOK(0x004C0D0C, _HouseClass_AI_Raise_Money_HarvRef1, 0)
{
    GET(HouseClass*, this_ptr, ESI);

    bool build_harv;
    int object_cost;

    /**
     *  If we have a refinery and a weapons factory, build a harvester, otherwise - a refinery.
     */
    if (this_ptr->Count_Owned(Rule->BuildRefinery) > 0 && this_ptr->Count_Owned(Rule->BuildWeapons) > 0) {
        build_harv = true;
        object_cost = this_ptr->Get_First_Ownable(Rule->HarvesterUnit)->Cost_Of(this_ptr);
    } else {
        build_harv = false;
        object_cost = this_ptr->Get_First_Ownable(Rule->BuildRefinery)->Cost_Of(this_ptr);
    }

    R->Stack8(0x13, build_harv);
    R->EAX(object_cost);
    return 0x004C0D94;
}


/**
 *  #issue-177
 *
 *  Patches the AI to correctly construct its own faction's harvester.
 *
 *  @author: ZivDero
 */
DEFINE_HOOK(0x004C0F5F, _HouseClass_AI_Raise_Money_HarvRef2, 0)
{
    GET(HouseClass*, this_ptr, ESI);

    UnitType harv = this_ptr->Get_First_Ownable(Rule->HarvesterUnit)->HeapID;

    R->EAX(harv);
    return 0x004C0F72;
}


/**
 *  #issue-177
 *
 *  Patches the AI to correctly construct its own faction's refinery.
 *
 *  @author: ZivDero
 */
DEFINE_HOOK(0x004C0FAB, _HouseClass_AI_Raise_Money_HarvRef3, 0)
{
    GET(HouseClass*, this_ptr, ESI);

    static BuildingTypeClass* refinery_ptr;
    refinery_ptr = this_ptr->Get_First_Ownable(Rule->BuildRefinery);

    // The instructions here are messy, so we hijack when the game
    // is accessing the vector and substitute our pointer
    R->EDX(&refinery_ptr);
    return 0x004C0FBB;
}


/**
 *  #issue-177
 *
 *  Patches the AI to correctly construct its own faction's refinery.
 *
 *  @author: ZivDero
 */
DEFINE_HOOK(0x004C1051, _HouseClass_AI_Raise_Money_HarvRef4, 0)
{
    GET(HouseClass*, this_ptr, ESI);

    BuildingTypeClass* refinery = this_ptr->Get_First_Ownable(Rule->BuildRefinery);

    R->EAX(refinery);
    return 0x004C105E;
}


/**
 *  #issue-177
 *
 *  Patches the AI to correctly count all harvesters and refineries.
 *
 *  @author: ZivDero
 */
DEFINE_HOOK(0x004C166D, _HouseClass_AI_Unit_HarvRef1, 0)
{
    GET(HouseClass*, this_ptr, EBP);
    const int harv_count = this_ptr->Count_Owned(Rule->HarvesterUnit);
    const int ref_count = this_ptr->Count_Owned(Rule->BuildRefinery);

    R->ESI(harv_count);
    R->EAX(ref_count);
    return 0x004C16AE;
}


/**
 *  #issue-177
 *
 *  Patches the AI to correctly building its own faction's harvester.
 *
 *  @author: ZivDero
 */
DEFINE_HOOK(0x004C1710, _HouseClass_AI_Unit_HarvRef2, 0)
{
    GET(HouseClass*, this_ptr, EBP);
    UnitTypeClass* harvester = this_ptr->Get_First_Ownable(Rule->HarvesterUnit);

    R->EAX(harvester);
    return 0x004C1718;
}


/**
 *  #issue-177
 *
 *  Patches the AI to correctly consider all Construction Yards from the list in prerequisite checks.
 *
 *  @author: ZivDero
 */
DEFINE_HOOK(0x004C5977, _HouseClass_Has_Prerequisites_BuildConst, 0)
{
    GET(BuildingTypeClass*, building, ECX);

    if (!Rule->BuildConst.Is_Present(building)) {
        return 0x004C5985;
    }

    return 0x004C5B62;
}


/**
 *  #issue-177
 *
 *  Patches the AI to correctly consider all Construction Yards from the list.
 *
 *  @author: ZivDero
 */
DEFINE_HOOK(0x004C5E20, _HouseClass_GenerateAIBuildList_4C5BB0_BuildConst, 0)
{
    GET_STACK(HouseClass*, this_ptr, 0x14);
    BuildingTypeClass* conyard = this_ptr->Get_First_Ownable(Rule->BuildConst);

    R->ESI(conyard);
    return 0x004C5E28;
}


/**
 *  #issue-177
 *
 *  Patches the AI to correctly consider all Construction Yards from the list as targets for the Ion Cannon.
 *
 *  @author: ZivDero
 */
DEFINE_HOOK(0x004CA222, _HouseClass_AI_Use_Super_Ion_Cannon_BuildConst, 0)
{
    GET(UnitTypeClass*, unittype, ECX);

    if (Rule->BuildConst.Is_Present(unittype->DeploysInto)) {
        return 0x004CA232;
    }

    return 0x004CA240;
}


/**
 *  #issue-177
 *
 *  Patches the AI to correctly consider all Construction Yards from the list when the AI takes over a player's house.
 *
 *  @author: ZivDero
 */
DEFINE_HOOK(0x004CA9A1, _HouseClass_AI_Takeover_BuildConst, 0)
{
    GET(BuildingTypeClass*, buildingtype, ECX);

    if (Rule->BuildConst.Is_Present(buildingtype)) {
        return 0x004CA9A9;
    }

    return 0x004CA9B7;
}


/**
 *  #issue-177
 *
 *  Fix a vanilla bug where vehicles thieves were able to target harvesters even when HarvesterTruce was on.
 *
 *  @author: ZivDero
 */
DEFINE_HOOK(0x004D7284, _InfantryClass_What_Action_Harvester_Thief, 0)
{
    GET(UnitClass*, target, ESI);

    if (target->RTTI == RTTI_UNIT && Rule->HarvesterUnit.Is_Present(target->Class)) {
        return 0x004D7258;
    }

    return 0x004D72A8;
}


/**
 *  Patch to enable base nodes for the AI when UseMPAIBaseNodes=yes is set in the scenario.
 *
 *  @author: ZivDero, Rampastring
 */
DEFINE_HOOK(0x004CB9DE, _HouseClass_Can_Build_Here_MP_AI_BaseNodes_Patch, 5)
{
    /**
     *  Also ignore AIBaseSpacing if it was requested by the client.
     */
    if (ScenExtension->IsUseMPAIBaseNodes) {
        return 0x004CB9D2;
    }

    /**
     *  Continue with AIBaseSpacing.
     */
    return 0;
}


/**
 *  Replacement to Assign_Handicap to read from our new difficulty settings.
 *
 *  @author: Rampastring
 */
ExtDiffType HouseClassExt::_Assign_Handicap(ExtDiffType handicap)
{
    ExtDiffType old = (ExtDiffType)Difficulty;

    /**
     *  We have not fully replaced the original difficulty logic yet, so
     *  we'll have to limit the "actual house difficulty" to vanilla
     *  levels or it'll read out of bounds.
     */
    Difficulty = (DiffType)(handicap >= DIFF_COUNT ? 0 : handicap);

    DEBUG_INFO("Assigning handicap {} to house {}\n", (int)handicap, (int)HeapID);

    if (handicap >= EXT_DIFF_COUNT) {
        DEBUG_ERROR("Invalid value supplied to HouseClassExt::_Assign_Handicap! {}", (int)handicap);
        Emergency_Exit(0);
        return old;
    }

    DifficultyClass* diff = &RuleExtension->Diff[handicap];
    if (handicap == DIFF_NORMAL && Is_Human_Player() && RuleExtension->IsHasPlayerNormal) {
        diff = &RuleExtension->PlayerNormal;
    }

    if (Session.Type != GAME_NORMAL) {
        HouseTypeClass const* hptr = Class;
        FirepowerBias = hptr->FirepowerBias * diff->FirepowerBias;
        GroundspeedBias = hptr->GroundspeedBias * diff->GroundspeedBias * Rule->GameSpeedBias;
        AirspeedBias = hptr->AirspeedBias * diff->AirspeedBias * Rule->GameSpeedBias;
        ArmorBias = hptr->ArmorBias * diff->ArmorBias;
        ROFBias = hptr->ROFBias * diff->ROFBias;
        CostBias = hptr->CostBias * diff->CostBias;
        RepairDelay = diff->RepairDelay;
        BuildDelay = diff->BuildDelay;
        BuildSpeedBias = hptr->BuildSpeedBias * diff->BuildSpeedBias * Rule->GameSpeedBias;
    } else {
        FirepowerBias = diff->FirepowerBias;
        GroundspeedBias = diff->GroundspeedBias * Rule->GameSpeedBias;
        AirspeedBias = diff->AirspeedBias * Rule->GameSpeedBias;
        ArmorBias = diff->ArmorBias;
        ROFBias = diff->ROFBias;
        CostBias = diff->CostBias;
        RepairDelay = diff->RepairDelay;
        BuildDelay = diff->BuildDelay;
        BuildSpeedBias = diff->BuildSpeedBias * Rule->GameSpeedBias;
    }

    TeamTime = 30 * HeapID + Rule->TeamDelays[Difficulty];

    return old;
}


/**
 *  Patches HouseClass::Updated_Spied_By to take sight range bonuses into account when revealing the area
 *  around technos that were revealed by spying a radar
 *
 *  @author: JoyfulShush
 */
DEFINE_HOOK(0x004C9937, HouseClass_Updated_Spied_By_Sight_Range_Patch, 0)
{
    GET(TechnoClass*, techno, ESI);
    GET(HouseClass*, this_ptr, EDI);

    auto techno_ext = Extension::Fetch(techno);

    int sight_range = techno_ext->Get_Sight_Range();
    Coord coord = techno->Center_Coord();

    Map.Sight_From(coord, sight_range, this_ptr);

    return 0x004C996B;
}


/**
 *  Main function for patching the hooks.
 */
void HouseClassExtension_Hooks()
{
    /**
     *  Initialises the extended class.
     */
    HouseClassExtension_Init();

    Patch_Jump(0x004BAED0, &HouseClassExt::_Can_Make_Money);
    Patch_Jump(0x004C0A40, &HouseClassExt::_Check_Raise_Money);
    Patch_Jump(0x004BDB50, &HouseClassExt::_Make_Ally);

    Patch_Jump(0x004C10E0, &HouseClassExt::_AI_Building);
    Patch_Jump(0x004C1650, &HouseClassExt::_AI_Unit);
    Patch_Jump(0x004C1A30, &HouseClassExt::_AI_Infantry);
    Patch_Jump(0x004C1D40, &HouseClassExt::_AI_Aircraft);
    Patch_Jump(0x004C0630, &HouseClassExt::_Expert_AI);

    Patch_Jump(0x004BAC2C, 0x004BAC39); // Patch a jump in the constructor to always allocate unit trackers

    Patch_Jump(0x004C23B0, &HouseClassExt::_Active_Remove);
    Patch_Jump(0x004C2450, &HouseClassExt::_Active_Add);

    Patch_Call(0x0042D460, &HouseClassExt::_Find_Build_Location);
    Patch_Call(0x0042D53C, &HouseClassExt::_Find_Build_Location);
    Patch_Call(0x004C8104, &HouseClassExt::_Find_Build_Location);

    Patch_Jump(0x004C5920, &HouseClassExt::_AI_Has_Prerequisites);

    Patch_Jump(0x004C2CA0, &HouseClassExt::_Fetch_Factory);
    Patch_Jump(0x004C2D20, &HouseClassExt::_Set_Factory);
    Patch_Jump(0x004C2330, &HouseClassExt::_Factory_Counter);
    Patch_Jump(0x004C2DB0, &HouseClassExt::_Factory_Count);
    Patch_Jump(0x004BE5D0, &HouseClassExt::_Suspend_Production);
    Patch_Jump(0x004BE200, &HouseClassExt::_Begin_Production);
    Patch_Jump(0x004BE6A0, &HouseClassExt::_Abandon_Production);
    Patch_Jump(0x004BEA10, &HouseClassExt::_Place_Object);
    Patch_Jump(0x004BF180, &HouseClassExt::_Suggest_New_Object);
    Patch_Jump(0x004BD590, &HouseClassExt::_Harvested);

    Patch_Jump(0x004BC077, 0x004BC082); // HouseClass::Can_Build, always check for ConYard of required Owner

    Patch_Jump(0x004BF4C0, &HouseClassExt::_MPlayer_Defeated);
    Patch_Jump(0x004C4730, &HouseClassExtension::House_From_HousesType);

    /**
     *  Patch away a few checks for GAME_INTERNET to enable statistics collection.
     */
    Patch_Jump(0x004C220B, 0x004C2218); // HouseClass::Add_Tracking
    Patch_Jump(0x004C2255, 0x004C2262); // HouseClass::Add_Tracking
    Patch_Jump(0x004C229F, 0x004C22A8); // HouseClass::Add_Tracking
    Patch_Jump(0x004C22E5, 0x004C22EE); // HouseClass::Add_Tracking

    Patch_Jump(0x004BB460, &HouseClassExt::_Assign_Handicap);
    Patch_Jump(0x004CA4A0, &HouseClassExt::_AI_Target_MultiMissile);
    Patch_Jump(0x004C9EA0, &HouseClassExt::_AI_Super_Weapons);
}

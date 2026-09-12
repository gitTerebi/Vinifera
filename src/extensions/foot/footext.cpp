/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Extended FootClass class.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "footext.h"

#include "attackmove.h"


/**
 *  Class constructor.
 *
 *  @author: CCHyper
 */
FootClassExtension::FootClassExtension(const FootClass *this_ptr) :
    WFStuckFrame(0),
    IsAttackMove(false),
    AttackMoveCell(0, 0),
    AttackMoveStatus(0),
    AttackMoveScanDelay(0),
    TechnoClassExtension(this_ptr)
{
}


/**
 *  Class no-init constructor.
 *  
 *  @author: CCHyper
 */
FootClassExtension::FootClassExtension(const NoInitClass &noinit) :
    TechnoClassExtension(noinit)
{
}


/**
 *  Class destructor.
 *  
 *  @author: CCHyper
 */
FootClassExtension::~FootClassExtension()
{
    if (IsAttackMove) {
        IsAttackMove = false;
        Vinifera_AttackMove_Uncount();
    }
}


/**
 *  Initializes an object from the stream where it was saved previously.
 *  
 *  @author: CCHyper
 */
HRESULT FootClassExtension::Load(IStream *pStm)
{
    HRESULT hr = TechnoClassExtension::Load(pStm);
    if (FAILED(hr)) {
        return E_FAIL;
    }
    
    return hr;
}


/**
 *  Saves an object to the specified stream.
 *  
 *  @author: CCHyper
 */
HRESULT FootClassExtension::Save(IStream *pStm, BOOL fClearDirty)
{
    HRESULT hr = TechnoClassExtension::Save(pStm, fClearDirty);
    if (FAILED(hr)) {
        return hr;
    }

    return hr;
}




/**
 *  Compute a unique crc value for this instance.
 *  
 *  @author: CCHyper
 */
void FootClassExtension::Object_CRC(CRCEngine &crc) const
{
    TechnoClassExtension::Object_CRC(crc);

    if (IsAttackMove) {
        crc(AttackMoveCell.X);
        crc(AttackMoveCell.Y);
        crc(AttackMoveStatus);
        crc(AttackMoveScanDelay);
    }
}


/**
 *  Sets the last known flight cell of this object.
 *
 *  @author: ZivDero
 */
void FootClassExtension::Set_Last_Flight_Cell(Cell cell)
{
    LastFlightCell = cell;
}


/**
 *  Gets the last known flight cell of this object.
 *
 *  @author: ZivDero
 */
Cell FootClassExtension::Get_Last_Flight_Cell() const
{
    return LastFlightCell;
}

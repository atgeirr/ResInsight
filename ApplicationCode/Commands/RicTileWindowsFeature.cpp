/////////////////////////////////////////////////////////////////////////////////
//
//  Copyright (C) 2015-     Statoil ASA
//  Copyright (C) 2015-     Ceetron Solutions AS
// 
//  ResInsight is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
// 
//  ResInsight is distributed in the hope that it will be useful, but WITHOUT ANY
//  WARRANTY; without even the implied warranty of MERCHANTABILITY or
//  FITNESS FOR A PARTICULAR PURPOSE.
// 
//  See the GNU General Public License at <http://www.gnu.org/licenses/gpl.html> 
//  for more details.
//
/////////////////////////////////////////////////////////////////////////////////

#include "RicTileWindowsFeature.h"

#include "RiuMainWindow.h"

#include <QAction>

CAF_CMD_SOURCE_INIT(RicTileWindowsFeature, "RicTileWindowsFeature");

//--------------------------------------------------------------------------------------------------
/// 
//--------------------------------------------------------------------------------------------------
bool RicTileWindowsFeature::isCommandEnabled()
{
    return RiuMainWindow::instance()->isAnyMdiSubWindowVisible();
}

//--------------------------------------------------------------------------------------------------
/// 
//--------------------------------------------------------------------------------------------------
void RicTileWindowsFeature::onActionTriggered(bool isChecked)
{
    RiuMainWindow::instance()->tileWindows();
}

//--------------------------------------------------------------------------------------------------
/// 
//--------------------------------------------------------------------------------------------------
void RicTileWindowsFeature::setupActionLook(QAction* actionToSetup)
{
    actionToSetup->setText("Tile Windows");
    actionToSetup->setIcon(QIcon(":/view-page-multi-24.png"));
}

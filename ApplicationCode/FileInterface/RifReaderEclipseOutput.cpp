/////////////////////////////////////////////////////////////////////////////////
//
//  Copyright (C) 2011-2012 Statoil ASA, Ceetron AS
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

#include "cvfBase.h"

#include "RigMainGrid.h"
#include "RigReservoir.h"
#include "RigReservoirCellResults.h"

#include "RifReaderEclipseOutput.h"
#include "RifEclipseOutputFileTools.h"
#include "RifEclipseUnifiedRestartFileAccess.h"
#include "RifEclipseRestartFilesetAccess.h"

#include <iostream>

#ifdef USE_ECL_LIB
#include "ecl_grid.h"
#include "well_state.h"
#endif //USE_ECL_LIB
#include "cafProgressInfo.h"

//--------------------------------------------------------------------------------------------------
///     ECLIPSE cell numbering layout:
///        Lower layer:   Upper layer
///
///       2---3           6---7
///       |   |           |   |
///       0---1           4---5
///
///
///
//--------------------------------------------------------------------------------------------------

// The indexing conventions for vertices in ECLIPSE
//
//      6-------------7
//     /|            /|
//    / |           / |
//   /  |          /  |
//  4-------------5   |
//  |   |         |   |
//  |   2---------|---3
//  |  /          |  /
//  | /           | /
//  |/            |/
//  0-------------1
//  vertex indices
//
// The indexing conventions for vertices in ResInsight
//
//      7-------------6
//     /|            /|
//    / |           / |
//   /  |          /  |
//  4-------------5   |
//  |   |         |   |
//  |   3---------|---2
//  |  /          |  /
//  | /           | /
//  |/            |/
//  0-------------1
//  vertex indices
//

static const size_t cellMappingECLRi[8] = { 0, 1, 3, 2, 4, 5, 7, 6 };


//**************************************************************************************************
// Static functions
//**************************************************************************************************

#ifdef USE_ECL_LIB
bool transferGridCellData(RigMainGrid* mainGrid, RigGridBase* localGrid, const ecl_grid_type* localEclGrid, size_t activeStartIndex)
{
    int cellCount = ecl_grid_get_global_size(localEclGrid);
    size_t cellStartIndex = mainGrid->cells().size();
    size_t nodeStartIndex = mainGrid->nodes().size();

    RigCell defaultCell;
    defaultCell.setHostGrid(localGrid);
    mainGrid->cells().resize(cellStartIndex + cellCount, defaultCell);

    mainGrid->nodes().resize(nodeStartIndex + cellCount*8, cvf::Vec3d(0,0,0));

    caf::ProgressInfo progInfo(cellCount, "");
    size_t computedCellCount = 0;
    // Loop over cells and fill them with data

    #pragma omp parallel for
    for (int gIdx = 0; gIdx < cellCount; ++gIdx)
    {
        RigCell& cell = mainGrid->cells()[cellStartIndex + gIdx];

        // The invalid (tainted) cell concept in ecl was not correct at all,
        // so this is disabeled.
        //bool invalid = ecl_grid_cell_invalid1(localEclGrid, gIdx);
        //cell.setInvalid(invalid);

        cell.setCellIndex(gIdx);
        bool active = ecl_grid_cell_active1(localEclGrid, gIdx);
        cell.setActive(active);
        cell.setGlobalActiveIndex(active ? activeStartIndex + ecl_grid_get_active_index1(localEclGrid, gIdx) : cvf::UNDEFINED_SIZE_T);

        int parentCellIndex = ecl_grid_get_parent_cell1(localEclGrid, gIdx);
        if (parentCellIndex == -1)
        {
            cell.setParentCellIndex(cvf::UNDEFINED_SIZE_T);
        }
        else
        {
            cell.setParentCellIndex(parentCellIndex);
        }
    
        // Corner coordinates
        int cIdx;
        for (cIdx = 0; cIdx < 8; ++cIdx)
        {
            double * point = mainGrid->nodes()[nodeStartIndex + gIdx * 8 + cellMappingECLRi[cIdx]].ptr();
            ecl_grid_get_corner_xyz1(localEclGrid, gIdx, cIdx, &(point[0]), &(point[1]), &(point[2]));
            point[2] = -point[2];
            cell.cornerIndices()[cIdx] = nodeStartIndex + gIdx*8 + cIdx;
        }

        // Sub grid in cell
        const ecl_grid_type* subGrid = ecl_grid_get_cell_lgr1(localEclGrid, gIdx);
        if (subGrid != NULL)
        {
            int subGridFileIndex = ecl_grid_get_grid_nr(subGrid);
            CVF_ASSERT(subGridFileIndex > 0);
            cell.setSubGrid(static_cast<RigLocalGrid*>(mainGrid->gridByIndex(subGridFileIndex)));
        }

#pragma omp atomic
        computedCellCount++;

        if (computedCellCount%5000 < 20) 
            progInfo.setProgress(computedCellCount);
    }
    return true;
}
#endif

//==================================================================================================
//
// Class RigReaderInterfaceECL
//
//==================================================================================================

//--------------------------------------------------------------------------------------------------
/// Constructor
//--------------------------------------------------------------------------------------------------
RifReaderEclipseOutput::RifReaderEclipseOutput()
{
    ground();
}

//--------------------------------------------------------------------------------------------------
/// Destructor
//--------------------------------------------------------------------------------------------------
RifReaderEclipseOutput::~RifReaderEclipseOutput()
{
}

//--------------------------------------------------------------------------------------------------
/// Ground members
//--------------------------------------------------------------------------------------------------
void RifReaderEclipseOutput::ground()
{
    m_fileName.clear();
    m_fileSet.clear();

    m_timeSteps.clear();

}

//--------------------------------------------------------------------------------------------------
/// Close interface (for now, no files are kept open after calling methods, so just clear members)
//--------------------------------------------------------------------------------------------------
void RifReaderEclipseOutput::close()
{
    m_staticResultsAccess     = NULL;
    m_dynamicResultsAccess    = NULL;

    ground();
}

//--------------------------------------------------------------------------------------------------
/// Read geometry from file given by name into given reservoir object
//--------------------------------------------------------------------------------------------------
bool RifReaderEclipseOutput::transferGeometry(const ecl_grid_type* mainEclGrid, RigReservoir* reservoir)
{
    CVF_ASSERT(reservoir);

#ifdef USE_ECL_LIB
    
    if (!mainEclGrid)
    {
        // Some error
        return false;
    }

    RigMainGrid* mainGrid = reservoir->mainGrid();
    {
        cvf::Vec3st  gridPointDim(0,0,0);
        gridPointDim.x() = ecl_grid_get_nx(mainEclGrid) + 1;
        gridPointDim.y() = ecl_grid_get_ny(mainEclGrid) + 1;
        gridPointDim.z() = ecl_grid_get_nz(mainEclGrid) + 1;
        mainGrid->setGridPointDimensions(gridPointDim);
    }

    // Get and set grid and lgr metadata

    size_t totalCellCount = static_cast<size_t>(ecl_grid_get_global_size(mainEclGrid));

    int numLGRs = ecl_grid_get_num_lgr(mainEclGrid);
    int lgrIdx;
    for (lgrIdx = 0; lgrIdx < numLGRs; ++lgrIdx)
    {
        ecl_grid_type* localEclGrid = ecl_grid_iget_lgr(mainEclGrid, lgrIdx);

        std::string lgrName = ecl_grid_get_name(localEclGrid);
        cvf::Vec3st  gridPointDim(0,0,0);
        gridPointDim.x() = ecl_grid_get_nx(localEclGrid) + 1;
        gridPointDim.y() = ecl_grid_get_ny(localEclGrid) + 1;
        gridPointDim.z() = ecl_grid_get_nz(localEclGrid) + 1;

        RigLocalGrid* localGrid = new RigLocalGrid(mainGrid);
        mainGrid->addLocalGrid(localGrid);

        localGrid->setIndexToStartOfCells(totalCellCount);
        localGrid->setGridName(lgrName);
        localGrid->setGridPointDimensions(gridPointDim);

        totalCellCount += ecl_grid_get_global_size(localEclGrid);
    }

    // Reserve room for the cells and nodes and fill them with data

    mainGrid->cells().reserve(totalCellCount);
    mainGrid->nodes().reserve(8*totalCellCount);

    caf::ProgressInfo progInfo(1 + numLGRs, "");
    progInfo.setProgressDescription("Main Grid");

    transferGridCellData(mainGrid, mainGrid, mainEclGrid, 0);

    progInfo.setProgress(1);

    size_t globalActiveSize = ecl_grid_get_active_size(mainEclGrid);

    for (lgrIdx = 0; lgrIdx < numLGRs; ++lgrIdx)
    {
        progInfo.setProgressDescription("LGR number " + QString::number(lgrIdx+1));

        ecl_grid_type* localEclGrid = ecl_grid_iget_lgr(mainEclGrid, lgrIdx);
        transferGridCellData(mainGrid, static_cast<RigLocalGrid*>(mainGrid->gridByIndex(lgrIdx+1)), localEclGrid, globalActiveSize);
        globalActiveSize += ecl_grid_get_active_size(localEclGrid);
        progInfo.setProgress(1 + lgrIdx);
    }

#endif

    return true;

}

//--------------------------------------------------------------------------------------------------
/// Open file and read geometry into given reservoir object
//--------------------------------------------------------------------------------------------------
bool RifReaderEclipseOutput::open(const QString& fileName, RigReservoir* reservoir)
{
    CVF_ASSERT(reservoir);
    caf::ProgressInfo progInfo(4, "");

    progInfo.setProgressDescription("Reading Grid");

    // Make sure everything's closed
    close();

    // Get set of files
    QStringList fileSet;
    if (!RifEclipseOutputFileTools::fileSet(fileName, &fileSet)) return false;


    // Keep the set of files of interest
    m_fileSet = fileSet;

    // Read geometry
#ifdef USE_ECL_LIB
    ecl_grid_type * mainEclGrid = ecl_grid_alloc( fileName.toAscii().data() );

    progInfo.setProgress(1);
    progInfo.setProgressDescription("Transferring grid geometry");

    if (!transferGeometry(mainEclGrid, reservoir)) return false;

    ecl_grid_free( mainEclGrid );

    progInfo.setProgress(2);
    progInfo.setProgressDescription("Reading Result index");

#endif

    // Build results meta data
    if (!buildMetaData(reservoir)) return false;

    progInfo.setProgress(3);
    progInfo.setProgressDescription("Reading Well information");
    
    readWellCells(reservoir);
    
    return true;
}

//--------------------------------------------------------------------------------------------------
/// Build meta data - get states and results info
//--------------------------------------------------------------------------------------------------
bool RifReaderEclipseOutput::buildMetaData(RigReservoir* reservoir)
{
#ifdef USE_ECL_LIB
    CVF_ASSERT(reservoir);
    CVF_ASSERT(m_fileSet.size() > 0);

    caf::ProgressInfo progInfo(2,"");

    // Get the number of active cells in the grid
    size_t numActiveCells = reservoir->mainGrid()->numActiveCells();
    size_t numGrids = reservoir->mainGrid()->gridCount();

    // Create access object for dynamic results
    m_dynamicResultsAccess = dynamicResultsAccess(m_fileSet, numGrids, numActiveCells);

    RigReservoirCellResults* resCellResults = reservoir->mainGrid()->results();

    if (m_dynamicResultsAccess.notNull())
    {
        // Get time steps 
        m_timeSteps = m_dynamicResultsAccess->timeSteps();

        // Get the names of the dynamic results
        QStringList dynamicResultNames = m_dynamicResultsAccess->resultNames();

        for (size_t i = 0; i < static_cast<size_t>(dynamicResultNames.size()); ++i)
        {
            size_t resIndex = resCellResults->addEmptyScalarResult(RimDefines::DYNAMIC_NATIVE, dynamicResultNames[i]);
            resCellResults->setTimeStepDates(resIndex, m_timeSteps);
        }
    }

    progInfo.setProgress(1);

    QString initFileName = RifEclipseOutputFileTools::fileNameByType(m_fileSet, ECL_INIT_FILE);
    if (initFileName.size() > 0)
    {
        // Open init file
        cvf::ref<RifEclipseOutputFileTools> initFile = new RifEclipseOutputFileTools;
        if (!initFile->open(initFileName))
        {
            return false;
        }

        // Get the names of the static results
        QStringList staticResults;
        initFile->keywordsOnFile(&staticResults, numActiveCells);
        QStringList staticResultNames = staticResults;

        QList<QDateTime> staticDate;
        staticDate.push_back(m_timeSteps.front());
        for (size_t i = 0; i < static_cast<size_t>(staticResultNames.size()); ++i)
        {
            size_t resIndex = resCellResults->addEmptyScalarResult(RimDefines::STATIC_NATIVE, staticResultNames[i]);
            resCellResults->setTimeStepDates(resIndex, staticDate);
        }

        m_staticResultsAccess = initFile;
    }

    return true;
#else
    return false;
#endif //USE_ECL_LIB
}

//--------------------------------------------------------------------------------------------------
/// Create results access object (.UNRST or .X0001 ... .XNNNN)
//--------------------------------------------------------------------------------------------------
RifEclipseRestartDataAccess* RifReaderEclipseOutput::dynamicResultsAccess(const QStringList& fileSet, size_t numGrids, size_t numActiveCells)
{
    RifEclipseRestartDataAccess* resultsAccess = NULL;

#ifdef USE_ECL_LIB
    // Look for unified restart file
    QString unrstFileName = RifEclipseOutputFileTools::fileNameByType(fileSet, ECL_UNIFIED_RESTART_FILE);
    if (unrstFileName.size() > 0)
    {
        resultsAccess = new RifEclipseUnifiedRestartFileAccess(numGrids, numActiveCells);
        if (!resultsAccess->open(QStringList(unrstFileName)))
        {
            delete resultsAccess;
            return NULL;
        }
    }
    else
    {
        // Look for set of restart files (one file per time step)
        QStringList restartFiles = RifEclipseOutputFileTools::fileNamesByType(fileSet, ECL_RESTART_FILE);
        if (restartFiles.size() > 0)
        {
            resultsAccess = new RifEclipseRestartFilesetAccess(numGrids, numActiveCells);
            if (!resultsAccess->open(restartFiles))
            {
                delete resultsAccess;
                return NULL;
            }
        }
    }
#endif //USE_ECL_LIB

    // !! could add support for formatted result files
    // !! consider priorities in case multiple types exist (.UNRST, .XNNNN, ...)

    return resultsAccess;
}

//--------------------------------------------------------------------------------------------------
/// Get all values of a given static result as doubles
//--------------------------------------------------------------------------------------------------
bool RifReaderEclipseOutput::staticResult(const QString& result, std::vector<double>* values)
{
    CVF_ASSERT(values);
    CVF_ASSERT(m_staticResultsAccess.notNull());

    size_t numOccurrences = m_staticResultsAccess->numOccurrences(result);
    size_t i;
    for (i = 0; i < numOccurrences; i++)
    {
        std::vector<double> partValues;
        if (!m_staticResultsAccess->keywordData(result, i, &partValues)) return false;
        values->insert(values->end(), partValues.begin(), partValues.end());
    }

    return true;
}

//--------------------------------------------------------------------------------------------------
/// Get dynamic result at given step index. Will concatenate values for the main grid and all sub grids.
//--------------------------------------------------------------------------------------------------
bool RifReaderEclipseOutput::dynamicResult(const QString& result, size_t stepIndex, std::vector<double>* values)
{
    CVF_ASSERT(m_dynamicResultsAccess.notNull());
    return m_dynamicResultsAccess->results(result, stepIndex, values);
}

//--------------------------------------------------------------------------------------------------
/// 
//--------------------------------------------------------------------------------------------------
const QList<QDateTime>& RifReaderEclipseOutput::timeSteps() const
{
    return m_timeSteps;
}

//--------------------------------------------------------------------------------------------------
///
//--------------------------------------------------------------------------------------------------
void RifReaderEclipseOutput::readWellCells(RigReservoir* reservoir)
{
    CVF_ASSERT(reservoir);

    if (m_dynamicResultsAccess.isNull()) return;

#ifdef USE_ECL_LIB
    well_info_type* ert_well_info = well_info_alloc(NULL);
    if (!ert_well_info) return;

    m_dynamicResultsAccess->readWellData(ert_well_info);

    RigMainGrid* mainGrid = reservoir->mainGrid();
    std::vector<RigGridBase*> grids;
    reservoir->allGrids(&grids);

    cvf::Collection<RigWellResults> wells;

    int wellIdx;
    for (wellIdx = 0; wellIdx < well_info_get_num_wells(ert_well_info); wellIdx++)
    {
        const char* wellName = well_info_iget_well_name(ert_well_info, wellIdx);
        CVF_ASSERT(wellName);

        cvf::ref<RigWellResults> wellResults = new RigWellResults;
        wellResults->m_wellName = wellName;

        well_ts_type* ert_well_time_series = well_info_get_ts(ert_well_info , wellName);
        int timeStepCount = well_ts_get_size(ert_well_time_series);

        wellResults->m_wellCellsTimeSteps.resize(timeStepCount);

        int timeIdx;
        for (timeIdx = 0; timeIdx < timeStepCount; timeIdx++)
        {
            well_state_type* ert_well_state = well_ts_iget_state(ert_well_time_series, timeIdx);

            RigWellResultFrame& wellResFrame = wellResults->m_wellCellsTimeSteps[timeIdx];

            // Build timestamp for well
            // Also see RifEclipseOutputFileAccess::timeStepsText for accessing time_t structures
            {
                time_t stepTime = well_state_get_sim_time(ert_well_state);
                wellResFrame.m_timestamp = QDateTime::fromTime_t(stepTime);
            }

            // Production type
            well_type_enum ert_well_type = well_state_get_type(ert_well_state);
            if (ert_well_type == PRODUCER)
            {
                wellResFrame.m_productionType = RigWellResultFrame::PRODUCER;
            }
            else if (ert_well_type == WATER_INJECTOR)
            {
                wellResFrame.m_productionType = RigWellResultFrame::WATER_INJECTOR;
            }
            else if (ert_well_type == GAS_INJECTOR)
            {
                wellResFrame.m_productionType = RigWellResultFrame::GAS_INJECTOR;
            }
            else if (ert_well_type == OIL_INJECTOR)
            {
                wellResFrame.m_productionType = RigWellResultFrame::OIL_INJECTOR;
            }
            else
            {
                wellResFrame.m_productionType = RigWellResultFrame::UNDEFINED_PRODUCTION_TYPE;
            }

            wellResFrame.m_isOpen = well_state_is_open( ert_well_state );




            // Loop over all the grids in the model. If we have connections in one, we will discard
            // the maingrid connections as they are "duplicates"

            bool hasWellConnectionsInLGR = false;
            for (size_t gridNr = 1; gridNr < grids.size(); ++gridNr)
            {
                int branchCount = well_state_iget_lgr_num_branches(ert_well_state, gridNr);
                if (branchCount > 0)
                {
                    hasWellConnectionsInLGR = true;
                    break;
                }
            }
            size_t gridNr = hasWellConnectionsInLGR ? 1 : 0;
            for (; gridNr < grids.size(); ++gridNr)
            {

                // Wellhead. If several grids have a wellhead definition for this well, we use tha last one. (Possibly the innermost LGR)
                const well_conn_type* ert_wellhead = well_state_iget_wellhead(ert_well_state, gridNr);
                if (ert_wellhead)
                {
                    wellResFrame.m_wellHead.m_gridIndex = gridNr;
                    int gridK = CVF_MAX(0, well_conn_get_k(ert_wellhead)); // Why this ?
                    wellResFrame.m_wellHead.m_gridCellIndex = grids[gridNr]->cellIndexFromIJK(well_conn_get_i(ert_wellhead), well_conn_get_j(ert_wellhead), gridK);
                }

                int branchCount = well_state_iget_lgr_num_branches(ert_well_state, gridNr);
                if (branchCount > 0)
                {
                    if (static_cast<int>(wellResFrame.m_wellResultBranches.size()) < branchCount) wellResFrame.m_wellResultBranches.resize(branchCount);

                    for (int branchIdx = 0; branchIdx < branchCount; ++branchIdx )
                    {
                        // Connections
                        int connectionCount = well_state_iget_num_lgr_connections(ert_well_state, gridNr, branchIdx);
                        if (connectionCount > 0)
                        {

                            RigWellResultBranch& wellSegment = wellResFrame.m_wellResultBranches[branchIdx]; // Is this completely right? Is the branch index actually the same between lgrs ?
                            wellSegment.m_branchNumber = branchIdx;
                            size_t existingConnCount = wellSegment.m_wellCells.size();
                            wellSegment.m_wellCells.resize(existingConnCount + connectionCount);

                            int connIdx;
                            for (connIdx = 0; connIdx < connectionCount; connIdx++)
                            {
                                const well_conn_type* ert_connection = well_state_iget_lgr_connections( ert_well_state, gridNr, branchIdx)[connIdx];
                                CVF_ASSERT(ert_connection);

                                RigWellResultCell& data = wellSegment.m_wellCells[existingConnCount + connIdx];
                                data.m_gridIndex = gridNr;
                                data.m_gridCellIndex = grids[gridNr]->cellIndexFromIJK(well_conn_get_i(ert_wellhead), well_conn_get_j(ert_wellhead), well_conn_get_k(ert_connection));
                                data.m_isOpen    = well_conn_open(ert_connection);
                                data.m_branchId  = well_conn_get_branch(ert_connection);
                                data.m_segmentId = well_conn_get_segment(ert_connection);
                            }
                        }
                    }
                }
            }
        }

        wellResults->computeMappingFromResultTimeIndicesToWellTimeIndices(m_timeSteps);

        wells.push_back(wellResults.p());
    }

    well_info_free(ert_well_info);

    reservoir->setWellResults(wells);


#endif
}


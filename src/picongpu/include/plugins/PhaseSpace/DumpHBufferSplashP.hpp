/**
 * Copyright 2013-2014 Axel Huebl
 *
 * This file is part of PIConGPU.
 *
 * PIConGPU is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * PIConGPU is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with PIConGPU.
 * If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <mpi.h>
#include <splash/splash.h>

#include "simulation_defines.hpp"
#include "communication/manager_common.h"
#include "mappings/simulation/GridController.hpp"
#include "mappings/simulation/SubGrid.hpp"
#include "simulationControl/DomainInformation.hpp"
#include "dimensions/DataSpace.hpp"
#include "cuSTL/container/HostBuffer.hpp"
#include "math/vector/Int.hpp"

#include <string>
#include <fstream>
#include <sstream>
#include <utility>
#include <cassert>

namespace picongpu
{
    class DumpHBuffer
    {
    public:
        /** Dump the PhaseSpace host Buffer
         *
         * \tparam Type the HBuffers element type
         * \tparam int the HBuffers dimension
         * \param hBuffer const reference to the hBuffer
         * \param axis_element plot to create: e.g. x, py from element_coordinate/momentum
         * \param unit sim unit of the buffer
         * \param currentStep current time step
         * \param mpiComm communicator of the participating ranks
         */
        template<typename T_Type, int T_bufDim>
        void operator()( const PMacc::container::HostBuffer<T_Type, T_bufDim>& hBuffer,
                         const std::pair<uint32_t, uint32_t> axis_element,
                         const std::pair<float_X, float_X> axis_p_range,
                         const float_64 pRange_unit,
                         const float_64 unit,
                         const uint32_t currentStep,
                         MPI_Comm& mpiComm ) const
        {
            using namespace splash;
            typedef T_Type Type;
            const int bufDim = T_bufDim;

            /** file name *****************************************************
             *    phaseSpace/PhaseSpace_xpy_timestep.h5                       */
            std::string fCoords("xyz");
            std::ostringstream filename;
            filename << "phaseSpace/PhaseSpace_"
                     << fCoords.at(axis_element.first)
                     << "p" << fCoords.at(axis_element.second);

            /** get size of the fileWriter communicator ***********************/
            int size;
            MPI_CHECK(MPI_Comm_size( mpiComm, &size ));

            /** create parallel domain collector ******************************/
            ParallelDomainCollector pdc(
                mpiComm, MPI_INFO_NULL, Dimensions(size, 1, 1), 10 );

            PMacc::GridController<simDim>& gc =
                PMacc::Environment<simDim>::get().GridController();
            DataCollector::FileCreationAttr fAttr;
            Dimensions mpiPosition( gc.getPosition()[axis_element.first], 0, 0 );
            fAttr.mpiPosition.set( mpiPosition );

            DataCollector::initFileCreationAttr(fAttr);

            pdc.open( filename.str().c_str(), fAttr );

            /** calculate local and global size of the phase space ***********/
            const uint32_t numSlides = MovingWindow::getInstance().getSlideCounter(currentStep);
            const DomainInformation domInfo;
            const int rLocalOffset = domInfo.localDomain.offset[axis_element.first];
            const int rLocalSize = domInfo.localDomain.size[axis_element.first];
            assert( (int)hBuffer.size().x() == rLocalSize );

            /* globalDomain of the phase space */
            splash::Dimensions globalPhaseSpace_size( domInfo.globalDomain.size[axis_element.first],
                                                      hBuffer.size().y(), 1 );

            /* global moving window meta information */
            splash::Dimensions globalPhaseSpace_offset( 0, 0, 0 );
            int globalMovingWindowOffset = 0;
            int globalMovingWindowSize   = domInfo.globalDomain.size[axis_element.first];
            if( axis_element.first == 1 ) /* spatial axis == y */
            {
                globalPhaseSpace_offset.set( numSlides * domInfo.localDomain.size.y(), 0, 0 );
                Window window = MovingWindow::getInstance( ).getWindow( currentStep );
                globalMovingWindowOffset = window.globalDimensions.offset[axis_element.first];
                globalMovingWindowSize = window.globalDimensions.size[axis_element.first];
            }

            /* localDomain: offset of it in the globalDomain and size */
            splash::Dimensions localPhaseSpace_offset( rLocalOffset, 0, 0 );
            splash::Dimensions localPhaseSpace_size( rLocalSize,
                                                     hBuffer.size().y(),
                                                     1 );

            /** Dataset Name **************************************************/
            std::ostringstream dataSetName;
            /* xpx or ypz or ... */
            dataSetName << fCoords.at(axis_element.first)
                        << "p" << fCoords.at(axis_element.second);

            /** write local domain ********************************************/
            typename PICToSplash<Type>::type ctPhaseSpace;

            pdc.writeDomain( currentStep,
                             /* global domain and my local offset within it */
                             globalPhaseSpace_size,
                             localPhaseSpace_offset,
                             /* */
                             ctPhaseSpace,
                             bufDim,
                             /* local data set dimensions */
                             splash::Selection(localPhaseSpace_size),
                             /* data set name */
                             dataSetName.str().c_str(),
                             /* global domain */
                             splash::Domain(
                                    globalPhaseSpace_offset,
                                    globalPhaseSpace_size
                             ),
                             /* dataClass, buffer */
                             DomainCollector::GridType,
                             &(*hBuffer.origin()) );

            /** meta attributes for the data set: unit, range, moving window **/
            typedef PICToSplash<float_X>::type  SplashFloatXType;
            typedef PICToSplash<float_64>::type SplashFloat64Type;
            ColTypeInt ctInt;
            SplashFloat64Type ctFloat64;
            SplashFloatXType  ctFloatX;

            pdc.writeAttribute( currentStep, ctFloat64, dataSetName.str().c_str(),
                                "sim_unit", &unit );
            pdc.writeAttribute( currentStep, ctFloat64, dataSetName.str().c_str(),
                                "p_unit", &pRange_unit );
            pdc.writeAttribute( currentStep, ctFloatX, dataSetName.str().c_str(),
                                "p_min", &(axis_p_range.first) );
            pdc.writeAttribute( currentStep, ctFloatX, dataSetName.str().c_str(),
                                "p_max", &(axis_p_range.second) );
            pdc.writeAttribute( currentStep, ctInt, dataSetName.str().c_str(),
                                "movingWindowOffset", &globalMovingWindowOffset );
            pdc.writeAttribute( currentStep, ctInt, dataSetName.str().c_str(),
                                "movingWindowSize", &globalMovingWindowSize );

            pdc.writeAttribute( currentStep, ctFloatX, dataSetName.str().c_str(),
                                "dr", &(cellSize[axis_element.first]) );
            pdc.writeAttribute( currentStep, ctFloatX, dataSetName.str().c_str(),
                                "dV", &CELL_VOLUME );
            pdc.writeAttribute( currentStep, ctFloat64, dataSetName.str().c_str(),
                                "dr_unit", &UNIT_LENGTH );
            pdc.writeAttribute( currentStep, ctFloatX, dataSetName.str().c_str(),
                                "dt", &DELTA_T );
            pdc.writeAttribute( currentStep, ctFloat64, dataSetName.str().c_str(),
                                "dt_unit", &UNIT_TIME );

            /** close file ****************************************************/
            pdc.finalize();
            pdc.close();
        }
    };

} /* namespace picongpu */

// EnergyPlus, Copyright (c) 1996-2017, The Board of Trustees of the University of Illinois and
// The Regents of the University of California, through Lawrence Berkeley National Laboratory
// (subject to receipt of any required approvals from the U.S. Dept. of Energy). All rights
// reserved.
//
// NOTICE: This Software was developed under funding from the U.S. Department of Energy and the
// U.S. Government consequently retains certain rights. As such, the U.S. Government has been
// granted for itself and others acting on its behalf a paid-up, nonexclusive, irrevocable,
// worldwide license in the Software to reproduce, distribute copies to the public, prepare
// derivative works, and perform publicly and display publicly, and to permit others to do so.
//
// Redistribution and use in source and binary forms, with or without modification, are permitted
// provided that the following conditions are met:
//
// (1) Redistributions of source code must retain the above copyright notice, this list of
//     conditions and the following disclaimer.
//
// (2) Redistributions in binary form must reproduce the above copyright notice, this list of
//     conditions and the following disclaimer in the documentation and/or other materials
//     provided with the distribution.
//
// (3) Neither the name of the University of California, Lawrence Berkeley National Laboratory,
//     the University of Illinois, U.S. Dept. of Energy nor the names of its contributors may be
//     used to endorse or promote products derived from this software without specific prior
//     written permission.
//
// (4) Use of EnergyPlus(TM) Name. If Licensee (i) distributes the software in stand-alone form
//     without changes from the version obtained under this License, or (ii) Licensee makes a
//     reference solely to the software portion of its product, Licensee must refer to the
//     software as "EnergyPlus version X" software, where "X" is the version number Licensee
//     obtained under this License and may not use a different name for the software. Except as
//     specifically required in this Section (4), Licensee shall not use in a company name, a
//     product name, in advertising, publicity, or other promotional activities any name, trade
//     name, trademark, logo, or other designation of "EnergyPlus", "E+", "e+" or confusingly
//     similar designation, without the U.S. Department of Energy's prior written consent.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
// IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
// AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
// OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

// C++ Headers

// ObjexxFCL Headers
#include <ObjexxFCL/gio.hh>

#ifdef GROUND_PLOT
#include <libgroundplot/GroundPlot.hpp>
#endif

// EnergyPlus Headers
#include <HeatBalanceKivaManager.hh>
#include <DataEnvironment.hh>
#include <DataHeatBalance.hh>
#include <DataHeatBalFanSys.hh>
#include <DataHeatBalSurface.hh>
#include <DataGlobals.hh>
#include <DataSurfaces.hh>
#include <DataStringGlobals.hh>
#include <DataSystemVariables.hh>
#include <DataVectorTypes.hh>
#include <DisplayRoutines.hh>
#include <General.hh>
#include <InputProcessor.hh>
#include <SurfaceGeometry.hh>
#include <UtilityRoutines.hh>
#include <WeatherManager.hh>

namespace EnergyPlus {
namespace HeatBalanceKivaManager {

KivaInstanceMap::KivaInstanceMap(
	Kiva::Foundation& foundation,
	std::map<Kiva::Surface::SurfaceType, std::vector<Kiva::GroundOutput::OutputType>> oM,
	int floorSurface,
	std::vector< int > wallSurfaces,
	int zoneNum,
	Real64 weightedPerimeter,
	int constructionNum
) :
	outputMap(oM),
	ground(foundation, outputMap),
	floorSurface(floorSurface),
	wallSurfaces(wallSurfaces),
	zoneNum(zoneNum),
	weightedPerimeter(weightedPerimeter),
	constructionNum(constructionNum)
{}

void KivaInstanceMap::initGround()
{
	// TODO Future accelerated initialization
	// Figure out what times (and corresponding weather data) to read
	// timestep = 168 hours
	// number of timesteps = 12

	ground.foundation.numericalScheme = Kiva::Foundation::NS_STEADY_STATE;
	setBoundaryConditions();
	ground.calculate( bcs );
	ground.calculateSurfaceAverages();
	ground.foundation.numericalScheme = Kiva::Foundation::NS_ADI;
}

void KivaInstanceMap::setBoundaryConditions()
{
	bcs.indoorTemp = DataHeatBalFanSys::MAT( zoneNum ) + DataGlobals::KelvinConv;
	bcs.outdoorTemp = DataEnvironment::OutDryBulbTemp + DataGlobals::KelvinConv;
	bcs.localWindSpeed = DataEnvironment::WindSpeedAt( ground.foundation.surfaceRoughness );
	bcs.solarAzimuth = std::atan2( DataEnvironment::SOLCOS( 1 ), DataEnvironment::SOLCOS( 2 ) );
	bcs.solarAltitude = DataGlobals::PiOvr2 - std::acos( DataEnvironment::SOLCOS( 3 ) );
	bcs.directNormalFlux = DataEnvironment::BeamSolarRad;
	bcs.diffuseHorizontalFlux = DataEnvironment::DifSolarRad;
	bcs.skyEmissivity = pow4( DataEnvironment::SkyTempKelvin )/pow4( bcs.outdoorTemp );

	bcs.slabAbsRadiation =
		DataHeatBalSurface::NetLWRadToSurf( floorSurface ) +
		DataHeatBalSurface::QRadSWInAbs( floorSurface ) +
		DataHeatBalFanSys::QHTRadSysSurf( floorSurface ) +
		DataHeatBalFanSys::QHWBaseboardSurf( floorSurface ) +
		DataHeatBalFanSys::QSteamBaseboardSurf( floorSurface ) +
		DataHeatBalFanSys::QElecBaseboardSurf( floorSurface ) +
		DataHeatBalance::QRadThermInAbs( floorSurface );

	// Calculate area weighted average for walls
	Real64 QAtotal = 0.0;
	Real64 Atotal = 0.0;
	for (auto& wl : wallSurfaces) {
		Real64 Q =
			DataHeatBalSurface::NetLWRadToSurf( wl ) +
			DataHeatBalSurface::QRadSWInAbs( wl ) +
			DataHeatBalFanSys::QHTRadSysSurf( wl ) +
			DataHeatBalFanSys::QHWBaseboardSurf( wl ) +
			DataHeatBalFanSys::QSteamBaseboardSurf( wl ) +
			DataHeatBalFanSys::QElecBaseboardSurf( wl ) +
			DataHeatBalance::QRadThermInAbs( wl );

		Real64& A = DataSurfaces::Surface( wl ).Area;

		QAtotal += Q*A;
		Atotal += A;

	}

	if ( Atotal > 0.0 ) {
		bcs.wallAbsRadiation = QAtotal/Atotal;
	}

}

void KivaInstanceMap::reportKivaSurfaces()
{
	// Calculate inside face values
	Real64 const qFloor = -( bcs.slabAbsRadiation + DataHeatBalance::HConvIn( floorSurface ) * ( DataHeatBalFanSys::MAT( zoneNum ) - DataHeatBalSurface::TempSurfIn( floorSurface )));

	DataHeatBalSurface::OpaqSurfInsFaceConductionFlux( floorSurface ) = qFloor;
	DataHeatBalSurface::OpaqSurfInsFaceConduction( floorSurface ) = qFloor * DataSurfaces::Surface( floorSurface ).Area;

	for ( auto& wl : wallSurfaces ) {
		Real64 Qrad =
			DataHeatBalSurface::NetLWRadToSurf( wl ) +
			DataHeatBalSurface::QRadSWInAbs( wl ) +
			DataHeatBalFanSys::QHTRadSysSurf( wl ) +
			DataHeatBalFanSys::QHWBaseboardSurf( wl ) +
			DataHeatBalFanSys::QSteamBaseboardSurf( wl ) +
			DataHeatBalFanSys::QElecBaseboardSurf( wl ) +
			DataHeatBalance::QRadThermInAbs( wl );

		Real64 const qWall = -( Qrad + DataHeatBalance::HConvIn( wl ) * ( DataHeatBalFanSys::MAT( zoneNum ) - DataHeatBalSurface::TempSurfIn( wl )));

		DataHeatBalSurface::OpaqSurfInsFaceConductionFlux( wl ) = qWall;
		DataHeatBalSurface::OpaqSurfInsFaceConduction( wl ) = qWall * DataSurfaces::Surface( wl ).Area;

	}

}

KivaManager::Settings::Settings() :
	soilK(0.864),
	soilRho(1510),
	soilCp(1260),
	groundSolarAbs(0.9),
	groundThermalAbs(0.9),
	groundRoughness(0.9),
	farFieldWidth(40.0),
	deepGroundBoundary(AUTO),
	deepGroundDepth(40.0),
	minCellDim(0.02),
	maxGrowthCoeff(1.5),
	timestepType(HOURLY)
{}

KivaManager::WallGroup::WallGroup(
	Real64 exposedPerimeter,
	std::vector<int> wallIDs
) :
	exposedPerimeter(exposedPerimeter),
	wallIDs(wallIDs)
{}

KivaManager::WallGroup::WallGroup() :
	exposedPerimeter(0.0)
{}


KivaManager::KivaManager() :
	defaultSet(false),
	defaultIndex(0)
{

	// default
	defaultSet = false;
	defaultIndex = 0.0;
}

KivaManager::~KivaManager()
{}

void KivaManager::readWeatherData()
{
	// Below from OpenEPlusWeatherFile
	int kivaWeatherFileUnitNumber = GetNewUnitNumber();
	{
		IOFlags flags; flags.ACTION( "read" ); gio::open( kivaWeatherFileUnitNumber, DataStringGlobals::inputWeatherFileName, flags );
		if ( flags.err() )
			ShowFatalError( "Kiva::ReadWeatherFile: Could not OPEN EPW Weather File" );
	}

	// Read in Header Information
	static Array1D_string const Header( 8, { "LOCATION", "DESIGN CONDITIONS", "TYPICAL/EXTREME PERIODS", "GROUND TEMPERATURES", "HOLIDAYS/DAYLIGHT SAVING", "COMMENTS 1", "COMMENTS 2", "DATA PERIODS" } );

	std::string Line;
	int HdLine = 1; // Look for first Header
	bool StillLooking = true;
	while ( StillLooking ) {
		{
			IOFlags flags; gio::read( kivaWeatherFileUnitNumber, "(A)", flags ) >> Line;
			if ( flags.end() )
				ShowFatalError( "Kiva::ReadWeatherFile: Unexpected End-of-File on EPW Weather file, while reading header information, looking for header=" + Header( HdLine ) );
		}
		/* Use headers to know how to read data to memory (e.g., number of periods, number of intervals)
		int endcol = len( Line );
		if ( endcol > 0 ) {
			if ( int( Line[ endcol - 1 ] ) == DataSystemVariables::iUnicode_end ) {
				ShowSevereError( "OpenWeatherFile: EPW Weather File appears to be a Unicode or binary file." );
				ShowContinueError( "...This file cannot be read by this program. Please save as PC or Unix file and try again" );
				ShowFatalError( "Program terminates due to previous condition." );
			}
		}
		std::string::size_type Pos = FindNonSpace( Line );
		std::string::size_type const HdPos = index( Line, Header( HdLine ) );
		if ( Pos != HdPos ) continue;
		Pos = index( Line, ',' );

		// Below borrowed from ProcessEPWHeader

		if ( ( Pos == std::string::npos ) && ( ! has_prefixi( Header( HdLine ), "COMMENTS" ) ) ) {
			ShowSevereError( "Invalid Header line in in.epw -- no commas" );
			ShowContinueError( "Line=" + Line );
			ShowFatalError( "Previous conditions cause termination." );
		}
		if ( Pos != std::string::npos ) Line.erase( 0, Pos + 1 );

		{ auto const SELECT_CASE_var( InputProcessor::MakeUPPERCase( Header( HdLine ) ) );

		if ( SELECT_CASE_var == "DATA PERIODS" ) {
			bool IOStatus;
			uppercase( Line );
			int NumHdArgs = 2;
			int Count = 1;
			while ( Count <= NumHdArgs ) {
				strip( Line );
				Pos = index( Line, ',' );
				if ( Pos == std::string::npos ) {
					if ( len( Line ) == 0 ) {
						while ( Pos == std::string::npos ) {
							gio::read( kivaWeatherFileUnitNumber, "(A)" ) >> Line;
							strip( Line );
							uppercase( Line );
							Pos = index( Line, ',' );
						}
					} else {
						Pos = len( Line );
					}
				}

				{ auto const SELECT_CASE_var1( Count );

				if ( SELECT_CASE_var1 == 1 ) {
					 int NumDataPeriods = InputProcessor::ProcessNumber( Line.substr( 0, Pos ), IOStatus );
					 NumHdArgs += 4 * NumDataPeriods;
				} else if ( SELECT_CASE_var1 == 2 ) {
					int NumIntervalsPerHour = InputProcessor::ProcessNumber( Line.substr( 0, Pos ), IOStatus );
				}}
				Line.erase( 0, Pos + 1 );
				++Count;
			}
		}}*/
		++HdLine;
		if ( HdLine == 9 ) StillLooking = false;
	}

	int ReadStatus = 0;
	bool ErrorFound = false;
	int WYear;
	int WMonth;
	int WDay;
	int WHour;
	int WMinute;
	Real64 DryBulb;
	Real64 DewPoint;
	Real64 RelHum;
	Real64 AtmPress;
	Real64 ETHoriz;
	Real64 ETDirect;
	Real64 IRHoriz;
	Real64 GLBHoriz;
	Real64 DirectRad;
	Real64 DiffuseRad;
	Real64 GLBHorizIllum;
	Real64 DirectNrmIllum;
	Real64 DiffuseHorizIllum;
	Real64 ZenLum;
	Real64 WindDir;
	Real64 WindSpeed;
	Real64 TotalSkyCover;
	Real64 OpaqueSkyCover;
	Real64 Visibility;
	Real64 CeilHeight;
	Real64 PrecipWater;
	Real64 AerosolOptDepth;
	Real64 SnowDepth;
	Real64 DaysSinceLastSnow;
	Real64 Albedo;
	Real64 LiquidPrecip;
	int PresWeathObs;
	Array1D_int PresWeathConds( 9 );

	Real64 totalDB = 0.0;
	int count = 0;
	std::string WeatherDataLine;

	while (! ReadStatus) {
		{ IOFlags flags; gio::read( kivaWeatherFileUnitNumber, "(A)", flags ) >> WeatherDataLine; ReadStatus = flags.ios(); }
		if ( ReadStatus < 0 ) {
			break;
		}
		WeatherManager::InterpretWeatherDataLine( WeatherDataLine, ErrorFound, WYear, WMonth, WDay, WHour, WMinute, DryBulb, DewPoint, RelHum, AtmPress, ETHoriz, ETDirect, IRHoriz, GLBHoriz, DirectRad, DiffuseRad, GLBHorizIllum, DirectNrmIllum, DiffuseHorizIllum, ZenLum, WindDir, WindSpeed, TotalSkyCover, OpaqueSkyCover, Visibility, CeilHeight, PresWeathObs, PresWeathConds, PrecipWater, AerosolOptDepth, SnowDepth, DaysSinceLastSnow, Albedo, LiquidPrecip );
		++count;
		totalDB += DryBulb;

	}

	// Annual averages
	kivaWeather.annualAverageDrybulbTemp = totalDB/count;

	gio::close( kivaWeatherFileUnitNumber );
}

bool KivaManager::setupKivaInstances()
{
	bool ErrorsFound = false;

	readWeatherData();

	auto& Surfaces = DataSurfaces::Surface;
	auto& Constructs = DataHeatBalance::Construct;
	auto& Materials = DataHeatBalance::Material;

	int inst = 0;
	int surfNum = 1;

	for ( auto& surface : Surfaces ) {
		if ( surface.ExtBoundCond == DataSurfaces::KivaFoundation && surface.Class == DataSurfaces::SurfaceClass_Floor ) {

			// Find other surfaces associated with the same floor
			std::vector< int > wallSurfaces;

			for ( auto& wl : foundationInputs[surface.OSCPtr].surfaces ) {
				if ( Surfaces( wl ).Zone == surface.Zone && wl != surfNum ) {
					if ( Surfaces( wl ).Class != DataSurfaces::SurfaceClass_Wall ) {
						if ( Surfaces( wl ).Class == DataSurfaces::SurfaceClass_Floor ) {
							ErrorsFound = true;
							ShowSevereError( "Foundation:Kiva=\"" + foundationInputs[surface.OSCPtr].name + "\", only one floor per Foundation:Kiva Object allowed." );
						} else {
							ErrorsFound = true;
							ShowSevereError( "Foundation:Kiva=\"" + foundationInputs[surface.OSCPtr].name + "\", only floor and wall surfaces are allowed to reference Foundation Outside Boundary Conditions." );
							ShowContinueError( "Surface=\"" + Surfaces( wl ).Name + "\", is not a floor or wall." );
						}
					} else {
						wallSurfaces.push_back( wl );
					}
				}
			}

			// Get combinations of wall constructions and wall heights -- each different
			// combination gets its own Kiva instance. Combination map points each set
			// of construction and wall height to the associated exposed perimeter and
			// list of wall surface numbers.
			std::map<std::pair<int, Real64>,WallGroup> combinationMap;

			if ( wallSurfaces.size() != 0 ) {
				for ( auto& wl : wallSurfaces ) {

					auto&v = Surfaces( wl ).Vertex;
					// Enforce quadrilateralism
					if ( v.size() != 4) {
						ErrorsFound = true;
						ShowSevereError( "Foundation:Kiva=\"" + foundationInputs[surface.OSCPtr].name + "\", only quadrilateral wall surfaces are allowed to reference Foundation Outside Boundary Conditions." );
						ShowContinueError( "Surface=\"" + Surfaces( wl ).Name + "\", has " + General::TrimSigDigits( v.size() ) + " vertices." );
					}

					// sort vertices by Z-value
					std::vector<int> zs = {0, 1, 2, 3};
					sort( zs.begin(),zs.end(),[v]( int a, int b ){return v[a].z < v[b].z;} );

					Real64 perimeter = distance( v[zs[0]], v[zs[1]] );

					Real64 surfHeight = ( v[zs[2]].z + v[zs[2]].z )/2.0 - ( v[zs[0]].z + v[zs[1]].z )/2.0;
					// round to avoid numerical precision differences
					surfHeight = std::round( ( surfHeight ) * 1000.0 ) / 1000.0;

					if ( combinationMap.count( {Surfaces( wl ).Construction, surfHeight}) == 0) {
						// create new combination
						std::vector<int> walls = {wl};
						combinationMap[{Surfaces( wl ).Construction, surfHeight}] = WallGroup( perimeter, walls );
					}
					else {
						// add to existing combination
						combinationMap[{Surfaces( wl ).Construction, surfHeight}].exposedPerimeter += perimeter;
						combinationMap[{Surfaces( wl ).Construction, surfHeight}].wallIDs.push_back( wl );
					}
				}
			}

			// Calculate total exposed perimeter attributes
			std::vector<bool> isExposedPerimeter;

			bool userSetExposedPerimeter;
			bool useDetailedExposedPerimeter;
			Real64 exposedFraction;

			auto& expPerimMap = SurfaceGeometry::exposedFoundationPerimeter.surfaceMap;
			if ( expPerimMap.count( surfNum ) == 1 ) {
				userSetExposedPerimeter = true;
				useDetailedExposedPerimeter = expPerimMap[surfNum].useDetailedExposedPerimeter;
				if ( useDetailedExposedPerimeter ) {
					for ( auto s : expPerimMap[surfNum].isExposedPerimeter ) {
						isExposedPerimeter.push_back( s );
					}
				} else {
					exposedFraction = expPerimMap[surfNum].exposedFraction;
				}
			} else {
				userSetExposedPerimeter = false;
				useDetailedExposedPerimeter = true;
			}

			Kiva::Polygon floorPolygon;
			if ( DataSurfaces::CCW ) {
				for ( std::size_t i = 0; i < surface.Vertex.size(); ++i ) {
					auto& v = surface.Vertex[i];
					floorPolygon.outer().push_back( Kiva::Point( v.x,v.y) );
					if ( !userSetExposedPerimeter ) {
						isExposedPerimeter.push_back( true );
					}
				}
			} else {
				for ( auto i = surface.Vertex.size() - 1; i <= 0; --i ) {
					auto& v = surface.Vertex[i];
					floorPolygon.outer().push_back( Kiva::Point( v.x,v.y ) );
					if ( !userSetExposedPerimeter ) {
						isExposedPerimeter.push_back( true );
					}
				}
			}

			Real64 totalPerimeter = 0.0;
			for ( std::size_t i = 0; i < surface.Vertex.size(); ++i ) {
				std::size_t iNext;
				if ( i == surface.Vertex.size() -1 ) {
					iNext = 0;
				} else {
					iNext = i+1;
				}
				auto& v = surface.Vertex[i];
				auto& vNext = surface.Vertex[iNext];
				totalPerimeter += distance( v,vNext );
			}


			if ( useDetailedExposedPerimeter ) {
				Real64 total2DPerimeter = 0.0;
				Real64 exposed2DPerimeter = 0.0;
				for ( std::size_t i = 0; i < floorPolygon.outer().size(); ++i ) {
					std::size_t iNext;
					if ( i == floorPolygon.outer().size() -1 ) {
						iNext = 0;
					} else {
						iNext = i+1;
					}
					auto& p = floorPolygon.outer()[i];
					auto& pNext = floorPolygon.outer()[iNext];
					Real64 perim = Kiva::getDistance( p, pNext );
					total2DPerimeter += perim;
					if ( isExposedPerimeter[i] ) {
						exposed2DPerimeter += perim;
					}
					else {
						exposed2DPerimeter += 0.0;
					}
				}
				exposedFraction = std::min( exposed2DPerimeter/total2DPerimeter, 1.0 );
			}

			Real64 totalExposedPerimeter = exposedFraction*totalPerimeter;

			// Remaining exposed perimeter will be alloted to each instance as appropriate
			Real64 remainingExposedPerimeter = totalExposedPerimeter;

			// setup map to point floor surface to all related kiva instances
			std::vector<std::pair<int, Kiva::Surface::SurfaceType>> floorSurfaceMaps;


			// Loop through combinations and assign instances until there is no remaining exposed pereimeter
			bool assignKivaInstances = true;
			auto comb = combinationMap.begin();
			while ( assignKivaInstances ) {
				int constructionNum;
				Real64 wallHeight;
				Real64 perimeter;
				std::vector<int> wallIDs;
				if ( comb != combinationMap.end() ){
					// Loop through wall combinations first
					constructionNum = comb->first.first;
					wallHeight = comb->first.second;
					perimeter = comb->second.exposedPerimeter;
					wallIDs = comb->second.wallIDs;
				}
				else {
					// Assign the remaining exposed perimeter to a slab instance
					constructionNum = foundationInputs[surface.OSCPtr].wallConstructionIndex;
					wallHeight = 0.0;
					perimeter = remainingExposedPerimeter;
				}

				Real64 weightedPerimeter;

				if ( totalExposedPerimeter > 0.001 ) {
					weightedPerimeter = perimeter/totalExposedPerimeter;
				} else {
					weightedPerimeter = 1.0;
				}

				// Copy foundation input for this instance
				Kiva::Foundation fnd = foundationInputs[surface.OSCPtr].foundation;

				// Exposed Perimeter
				fnd.useDetailedExposedPerimeter = useDetailedExposedPerimeter;
				fnd.isExposedPerimeter = isExposedPerimeter;
				fnd.exposedFraction = exposedFraction;


				if ( foundationInputs[surface.OSCPtr].wallConstructionIndex > 0 ) {
					auto& c = Constructs( foundationInputs[surface.OSCPtr].wallConstructionIndex );

					// Clear layers
					fnd.wall.layers.clear();

					// Push back construction's layers
					for ( int layer = 1; layer <= c.TotLayers; layer++ ) {
						auto& mat = Materials( c.LayerPoint( layer ) );
						if ( mat.ROnly ) {
							ErrorsFound = true;
							ShowSevereError( "Construction=\""+ c.Name + "\", constructions referenced by surfaces with a");
							ShowContinueError( "\"Foundation\" Outside Boundary Condition must use only regular material objects");
							ShowContinueError( "Material=\"" + mat.Name + "\", is not a regular material object" );
							return ErrorsFound;
						}

						Kiva::Layer tempLayer;

						tempLayer.material = Kiva::Material( mat.Conductivity, mat.Density, mat.SpecHeat );
						tempLayer.thickness = mat.Thickness;

						fnd.wall.layers.push_back(tempLayer);
					}
				}

				// Set slab construction
				for ( int i = 0; i < Constructs( surface.Construction ).TotLayers; ++i ) {
					auto& mat = Materials(Constructs( surface.Construction ).LayerPoint[i]);
					if ( mat.ROnly ) {
						ErrorsFound = true;
						ShowSevereError( "Construction=\""+ Constructs( surface.Construction ).Name + "\", constructions referenced by surfaces with a");
						ShowContinueError( "\"Foundation\" Outside Boundary Condition must use only regular material objects");
						ShowContinueError( "Material=\"" + mat.Name + "\", is not a regular material object" );
						return ErrorsFound;
					}

					Kiva::Layer tempLayer;

					tempLayer.material = Kiva::Material( mat.Conductivity, mat.Density, mat.SpecHeat );
					tempLayer.thickness = mat.Thickness;

					fnd.slab.layers.push_back( tempLayer );
				}

				fnd.slab.emissivity = 0.0; // Long wave included in rad BC. Materials(Constructs( surface.Construction ).LayerPoint(Constructs( surface.Construction ).TotLayers)).AbsorpThermal;

				fnd.foundationDepth = wallHeight;

				fnd.hasPerimeterSurface = false;
				fnd.perimeterSurfaceWidth = 0.0;

				// Add blocks
				auto intHIns = foundationInputs[surface.OSCPtr].intHIns;
				auto intVIns = foundationInputs[surface.OSCPtr].intVIns;
				auto extHIns = foundationInputs[surface.OSCPtr].extHIns;
				auto extVIns = foundationInputs[surface.OSCPtr].extVIns;
				auto footing = foundationInputs[surface.OSCPtr].footing;

				if ( std::abs( intHIns.width ) > 0.0 ) {
					intHIns.z += fnd.foundationDepth + fnd.slab.totalWidth();
					fnd.inputBlocks.push_back( intHIns );
				}
				if ( std::abs( intVIns.width ) > 0.0) {
					fnd.inputBlocks.push_back( intVIns );
				}
				if ( std::abs( extHIns.width ) > 0.0) {
					extHIns.z += fnd.wall.heightAboveGrade;
					extHIns.x = fnd.wall.totalWidth();
					fnd.inputBlocks.push_back( extHIns );
				}
				if ( std::abs( extVIns.width ) > 0.0) {
					extVIns.x = fnd.wall.totalWidth();
					fnd.inputBlocks.push_back( extVIns );
				}
				if ( std::abs( footing.width ) > 0.0) {
					footing.z = fnd.foundationDepth + fnd.slab.totalWidth() + fnd.wall.depthBelowSlab;
					footing.x = fnd.wall.totalWidth()/2.0 - footing.width/2.0;
					fnd.inputBlocks.push_back( footing );
				}

				for ( auto& block : fnd.inputBlocks ) {
					if ( block.depth == 0.0 ) {
						block.depth = fnd.foundationDepth;
					}
				}

				// polygon

				fnd.polygon = floorPolygon;

				// add new foundation instance to list of all instances
				foundationInstances[inst] = fnd;


				// create output map for ground instance. Calculate average temperature, flux, and convection for each surface
				std::map<Kiva::Surface::SurfaceType, std::vector<Kiva::GroundOutput::OutputType>> outputMap;

				outputMap[Kiva::Surface::ST_SLAB_CORE] = {
					Kiva::GroundOutput::OT_FLUX,
					Kiva::GroundOutput::OT_TEMP,
					Kiva::GroundOutput::OT_CONV
				};

				if ( fnd.hasPerimeterSurface ) {
					outputMap[Kiva::Surface::ST_SLAB_PERIM] = {
						Kiva::GroundOutput::OT_FLUX,
						Kiva::GroundOutput::OT_TEMP,
						Kiva::GroundOutput::OT_CONV
					};
				}

				if ( fnd.foundationDepth > 0.0 ) {
					outputMap[Kiva::Surface::ST_WALL_INT] = {
						Kiva::GroundOutput::OT_FLUX,
						Kiva::GroundOutput::OT_TEMP,
						Kiva::GroundOutput::OT_CONV
					};
				}

				// point surface to associated ground intance(s)
				kivaInstances.emplace_back(
					foundationInstances[inst],
					outputMap,surfNum,wallIDs,
					surface.Zone,
					weightedPerimeter,
					constructionNum
				);

				// Floors can point to any number of foundaiton surfaces
				floorSurfaceMaps.emplace_back(
					inst,
					Kiva::Surface::ST_SLAB_CORE
				);

				// Walls can only have one associated ground instance
				for ( auto& wl : wallIDs ) {
					surfaceMap[wl] = {{inst, Kiva::Surface::ST_WALL_INT}};
				}

				// Increment instnace counter
				inst++;

				// Increment wall combinations iterator
				if ( comb != combinationMap.end() ){
					comb++;
				}

				remainingExposedPerimeter -= perimeter;

				if ( remainingExposedPerimeter < 0.001 ) {
					assignKivaInstances = false;
				}

			}

			surfaceMap[surfNum] = floorSurfaceMaps;

		}

		surfNum++;
	}

	gio::write( DataGlobals::OutputFileInits, "(A)" ) << "! <Kiva Foundation Name>, Horizontal Cells, Vertical Cells, Total Cells, Total Exposed Perimeter, Perimeter Fraction, Wall Height, Wall Construction, Floor Surface, Wall Surface(s)";
	std::string fmt = "(A,',',I0',',I0',',I0',',A',',A',',A',',A',',A,A)";
	for ( auto& kv : kivaInstances ) {
		auto& grnd = kv.ground;

		if ( !kv.ground.foundation.useDetailedExposedPerimeter || !Kiva::isConvex( kv.ground.foundation.polygon ) ){
			if ( kv.ground.foundation.reductionStrategy == Kiva::Foundation::RS_BOUNDARY ) {
				kv.ground.foundation.reductionStrategy = Kiva::Foundation::RS_AP;
			}
		}

		if ( kv.ground.foundation.reductionStrategy == Kiva::Foundation::RS_BOUNDARY )
		{
			// Adjust for concave features using Boundary Layer Adjustment method
			grnd.calculateBoundaryLayer();
			grnd.setNewBoundaryGeometry();
		}

		grnd.buildDomain();

		std::string constructionName;
		if ( kv.constructionNum == 0 ) {
			constructionName = "<Default Footing Wall Construction>";
		} else {
			constructionName = DataHeatBalance::Construct( kv.constructionNum ).Name;
		}

		std::string wallSurfaceString = "";
		for ( auto& wl : kv.wallSurfaces ) {
			wallSurfaceString += "," + DataSurfaces::Surface( wl ).Name;
		}
		gio::write( DataGlobals::OutputFileInits, fmt )
			<< foundationInputs[DataSurfaces::Surface( kv.floorSurface ).OSCPtr].name
			<< grnd.nX << grnd.nZ << grnd.nX*grnd.nZ
			<< General::RoundSigDigits( grnd.foundation.netPerimeter, 2 )
			<< General::RoundSigDigits( kv.weightedPerimeter, 2 )
			<< General::RoundSigDigits( grnd.foundation.foundationDepth, 2 )
			<< constructionName
			<< DataSurfaces::Surface( kv.floorSurface ).Name
			<< wallSurfaceString;

	}

	return ErrorsFound;

}

void KivaManager::initKivaInstances()
{

	// initialize temperatures at the beginning of run environment
	if ( DataGlobals::BeginEnvrnFlag ) {
		for ( auto& kv : kivaInstances ) {
			// Start with steady-state solution
			kv.initGround();
		}
	}
}

void KivaManager::calcKivaInstances()
{
	// calculate heat transfer through ground
	for ( auto& kv : kivaInstances ) {
		auto& grnd = kv.ground;
		kv.setBoundaryConditions();
		grnd.calculate( kv.bcs,DataGlobals::MinutesPerTimeStep*60. );
		grnd.calculateSurfaceAverages();
		kv.reportKivaSurfaces();
		if ( DataEnvironment::Month == 1 && DataEnvironment::DayOfMonth == 1 && DataGlobals::HourOfDay == 1 && DataGlobals::TimeStep == 1 ) {
			kv.plotDomain();
		}
	}
}

void KivaInstanceMap::plotDomain()
{

	#ifdef GROUND_PLOT

	std::string constructionName;
	if ( constructionNum == 0 ) {
		constructionName = "Default Footing Wall Construction";
	} else {
		constructionName = DataHeatBalance::Construct( constructionNum ).Name;
	}


	Kiva::SnapshotSettings ss;
	ss.dir = DataStringGlobals::outDirPathName + "/"
		+ DataSurfaces::Surface( floorSurface ).Name + " "
		+ General::RoundSigDigits( ground.foundation.foundationDepth, 2 ) + " "
		+ constructionName;
	double& l = ground.foundation.reductionLength2;
	const double width = 6.0;
	const double depth = ground.foundation.foundationDepth + width/2.0;
	const double range = max( width, depth );
	ss.xRange = {l - range/2.0, l + range/2.0};
	ss.yRange = {0.5,0.5};
	ss.zRange = {-range, ground.foundation.wall.heightAboveGrade};

	Kiva::GroundPlot gp( ss, ground.domain, ground.foundation );

	std::size_t nI =  gp.iMax - gp.iMin + 1;
	std::size_t nJ = gp.jMax - gp.jMin + 1;

	for ( size_t k = gp.kMin; k <= gp.kMax; k++ ) {
		for ( size_t j = gp.jMin; j <= gp.jMax; j++ ) {
			for ( size_t i = gp.iMin; i <= gp.iMax; i++ ) {
				std::size_t index = ( i - gp.iMin ) + nI * ( j - gp.jMin ) + nI * nJ * ( k - gp.kMin );
				if ( ss.plotType == Kiva::SnapshotSettings::P_TEMP ) {
					if ( ss.outputUnits == Kiva::SnapshotSettings::IP ) {
						gp.TDat.a[index] = ( ground.TNew[i][j][k] - 273.15 ) * 9 / 5 + 32.0;
					} else {
						gp.TDat.a[index] = ground.TNew[i][j][k] - 273.15;
					}
				}
				else
				{
					double& du = gp.distanceUnitConversion;
					std::vector<double> Qflux = ground.calculateHeatFlux( i, j, k );
					double& Qx = Qflux[0];
					double& Qy = Qflux[1];
					double& Qz = Qflux[2];
					double Qmag = sqrt( Qx * Qx + Qy * Qy + Qz * Qz );

					if ( ss.fluxDir == Kiva::SnapshotSettings::D_M )
						gp.TDat.a[index] = Qmag / ( du * du );
					else if ( ss.fluxDir == Kiva::SnapshotSettings::D_X )
						gp.TDat.a[index] = Qx / ( du * du );
					else if ( ss.fluxDir == Kiva::SnapshotSettings::D_Y )
						gp.TDat.a[index] = Qy / ( du * du );
					else if ( ss.fluxDir == Kiva::SnapshotSettings::D_Z )
						gp.TDat.a[index] = Qz / ( du * du );
				}
			}
		}
	}

	gp.createFrame( std::to_string( DataEnvironment::Month ) + "/" + std::to_string( DataEnvironment::DayOfMonth ) + " " + std::to_string( DataGlobals::HourOfDay ) + ":00" );

	#endif

}

Real64 KivaManager::getValue(
	int surfNum,
	Kiva::GroundOutput::OutputType oT )
{
	Real64 h = 0.0;
	Real64 q = 0.0;
	Real64 Tz = DataHeatBalFanSys::MAT( DataSurfaces::Surface( surfNum ).Zone ) + DataGlobals::KelvinConv;
	for ( auto& i : surfaceMap[surfNum] ) {
		auto& kI = kivaInstances[i.first];
		auto& st = i.second;
		auto& p = kI.weightedPerimeter;
		auto hi = kI.ground.getSurfaceAverageValue( {st, Kiva::GroundOutput::OT_CONV} );
		auto Ts = kI.ground.getSurfaceAverageValue( {st, Kiva::GroundOutput::OT_TEMP} );

		q += p * hi * ( Tz - Ts );
		h += p * hi;
	}

	if (oT == Kiva::GroundOutput::OT_CONV){
		return h;
	} else {//if (oT == Kiva::GroundOutput::OT_TEMP)
		return Tz - q / h;
	}
}

Real64 KivaManager::getTemp( int surfNum )
{
	return getValue( surfNum, Kiva::GroundOutput::OT_TEMP ) - DataGlobals::KelvinConv;
}

Real64 KivaManager::getConv( int surfNum )
{
	auto conv = getValue( surfNum, Kiva::GroundOutput::OT_CONV );
	assert( conv >= 0.0 );
	return conv;

}

void KivaManager::defineDefaultFoundation()
{

	Kiva::Foundation defFnd;

	// From settings
	defFnd.soil = Kiva::Material( settings.soilK, settings.soilRho, settings.soilCp );
	defFnd.soilAbsorptivity = settings.groundSolarAbs;
	defFnd.soilEmissivity = settings.groundThermalAbs;
	defFnd.surfaceRoughness = settings.groundRoughness;
	defFnd.farFieldWidth = settings.farFieldWidth;

	Real64 waterTableDepth = 0.1022*DataEnvironment::Elevation;

	if ( settings.deepGroundBoundary == Settings::AUTO ) {
		if ( waterTableDepth <= 40. ) {
			defFnd.deepGroundDepth = waterTableDepth;
			defFnd.deepGroundBoundary = Kiva::Foundation::DGB_CONSTANT_TEMPERATURE;
			defFnd.deepGroundTemperature = kivaWeather.annualAverageDrybulbTemp + DataGlobals::KelvinConv;
		} else {
			defFnd.deepGroundDepth = 40.;
			defFnd.deepGroundBoundary = Kiva::Foundation::DGB_ZERO_FLUX;
		}
	} else if ( settings.deepGroundBoundary == Settings::ZERO_FLUX ) {
		defFnd.deepGroundDepth = settings.deepGroundDepth;
		defFnd.deepGroundBoundary = Kiva::Foundation::DGB_ZERO_FLUX;
	} else /* if (settings.deepGroundBoundary == Settings::GROUNDWATER) */ {
		defFnd.deepGroundDepth = settings.deepGroundDepth;
		defFnd.deepGroundBoundary = Kiva::Foundation::DGB_CONSTANT_TEMPERATURE;
		defFnd.deepGroundTemperature = kivaWeather.annualAverageDrybulbTemp + DataGlobals::KelvinConv;
	}

	defFnd.wall.heightAboveGrade = 0.2; // m

	Kiva::Material concrete;
	concrete.conductivity = 1.95;  // W/m-K
	concrete.density = 2400;  // kg/m3
	concrete.specificHeat = 900;  // J/kg-K

	Kiva::Layer defaultFoundationWall;
	defaultFoundationWall.thickness = 0.3; // m
	defaultFoundationWall.material = concrete;

	defFnd.wall.layers.push_back( defaultFoundationWall );

	defFnd.wall.interiorEmissivity = 0.9;
	defFnd.wall.exteriorEmissivity = 0.9;
	defFnd.wall.exteriorAbsorptivity = 0.9;

	defFnd.wall.depthBelowSlab = 0.0;

	defFnd.mesh.minCellDim = settings.minCellDim;
	defFnd.mesh.maxNearGrowthCoeff = settings.maxGrowthCoeff;
	defFnd.mesh.maxDepthGrowthCoeff = settings.maxGrowthCoeff;
	defFnd.mesh.maxInteriorGrowthCoeff = settings.maxGrowthCoeff;
	defFnd.mesh.maxExteriorGrowthCoeff = settings.maxGrowthCoeff;


	defaultFoundation.foundation = defFnd;
	defaultFoundation.name = "<Default Foundation>";
}

void KivaManager::addDefaultFoundation()
{
	foundationInputs.push_back( defaultFoundation );
	defaultIndex = foundationInputs.size() - 1;
	defaultSet = true;
}

int KivaManager::findFoundation( std::string const& name )
{
	int fndNum = 0;
	for ( auto& fnd : foundationInputs ) {
		// Check if foundation exists
		if ( fnd.name == name) {
			return fndNum;
		}
		fndNum++;
	}
	return (int)foundationInputs.size();
}

} // HeatBalanceKivaManager
} // EnergyPlus
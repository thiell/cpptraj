#ifdef BINTRAJ
// This file contains a collection of routines designed for reading
// netcdf trajectory files used with amber.
// Dan Roe 10-2008
// Original implementation of netcdf in Amber by John Mongan.
#include <netcdf.h>
#include "Traj_AmberNetcdf.h"
#include "CpptrajStdio.h"

// CONSTRUCTOR
Traj_AmberNetcdf::Traj_AmberNetcdf() :
  Coord_(0),
  eptotVID_(-1),
  binsVID_(-1),
  useVelAsCoords_(false),
  readAccess_(false),
  outputTemp_(false),
  outputVel_(false),
  outputFrc_(false)
{ }

// DESTRUCTOR
Traj_AmberNetcdf::~Traj_AmberNetcdf() {
  //fprintf(stderr,"Amber Netcdf Destructor\n");
  this->closeTraj();
  if (Coord_!=0) delete[] Coord_;
  // NOTE: Need to close file?
}

bool Traj_AmberNetcdf::ID_TrajFormat(CpptrajFile& fileIn) {
  if ( GetNetcdfConventions( fileIn.Filename().full() ) == NC_AMBERTRAJ ) return true;
  return false;
} 

// Traj_AmberNetcdf::close()
/** Close netcdf file. Set ncid to -1 since it can change between open
  * and close calls.
  */
void Traj_AmberNetcdf::closeTraj() {
  NC_close();
}

// Traj_AmberNetcdf::openTrajin()
int Traj_AmberNetcdf::openTrajin() {
  // If already open, return
  if (Ncid()!=-1) return 0;
  if ( NC_openRead( filename_.Full() ) != 0 ) {
    mprinterr("Error: Opening Netcdf file %s for reading.\n", filename_.base()); 
    return 1;
  }
  return 0;
}

void Traj_AmberNetcdf::ReadHelp() {
  mprintf("\tusevelascoords: Use velocities instead of coordinates if present.\n");
}

int Traj_AmberNetcdf::processReadArgs(ArgList& argIn) {
  useVelAsCoords_ = argIn.hasKey("usevelascoords");
  return 0;
}

// Traj_AmberNetcdf::setupTrajin()
/* * Open the netcdf file, read all dimension and variable IDs, close.
  * Return the number of frames in the file. 
  */
int Traj_AmberNetcdf::setupTrajin(FileName const& fname, Topology* trajParm)
{
  filename_ = fname;
  if (openTrajin()) return TRAJIN_ERR;
  readAccess_ = true;
  // Sanity check - Make sure this is a Netcdf trajectory
  if ( GetNetcdfConventions() != NC_AMBERTRAJ ) {
    mprinterr("Error: Netcdf file %s conventions do not include \"AMBER\"\n",filename_.base());
    return TRAJIN_ERR;
  }
  // Get global attributes
  std::string attrText = GetAttrText("ConventionVersion");
  if ( attrText != "1.0") 
    mprintf("Warning: Netcdf file %s has ConventionVersion that is not 1.0 (%s)\n",
            filename_.base(), attrText.c_str());
  // Get title
  SetTitle( GetAttrText("title") );
  // Get Frame info
  if ( SetupFrameDim()!=0 ) return TRAJIN_ERR;
  if ( Ncframe() < 1 ) {
    mprinterr("Error: Netcdf file is empty.\n");
    return TRAJIN_ERR;
  }
  // Setup Coordinates/Velocities
  if ( SetupCoordsVelo( useVelAsCoords_ )!=0 ) return TRAJIN_ERR;
  // Check that specified number of atoms matches expected number.
  if (Ncatom() != trajParm->Natom()) {
    mprinterr("Error: Number of atoms in NetCDF file %s (%i) does not\n"
              "Error:   match number in associated parmtop (%i)!\n", 
              filename_.base(), Ncatom(), trajParm->Natom());
    return TRAJIN_ERR;
  }
  // Setup Time - FIXME: Allowed to fail silently
  SetupTime();
  // Box info
  double boxcrd[6];
  if (SetupBox(boxcrd, NC_AMBERTRAJ) == 1) // 1 indicates an error
    return TRAJIN_ERR;
  // Replica Temperatures - FIXME: Allowed to fail silently
  SetupTemperature();
  // Replica Dimensions
  ReplicaDimArray remdDim;
  if ( SetupMultiD(remdDim) == -1 ) return TRAJIN_ERR;
  SetCoordInfo( CoordinateInfo(remdDim, Box(boxcrd), HasVelocities(),
                               HasTemperatures(), HasTimes(), HasForces()) ); 
  // NOTE: TO BE ADDED
  // labelDID;
  //int cell_spatialDID, cell_angularDID;
  //int spatialVID, cell_spatialVID, cell_angularVID;
  // Amber Netcdf coords are float. Allocate a float array for converting
  // float to/from double.
  if (Coord_ != 0) delete[] Coord_;
  Coord_ = new float[ Ncatom3() ];
  if (debug_>1) NetcdfDebug();
  closeTraj();
  return Ncframe();
}

void Traj_AmberNetcdf::WriteHelp() {
  mprintf("\tremdtraj: Write temperature to trajectory (makes REMD trajectory).\n"
          "\tvelocity: Write velocities to trajectory.\n"
          "\tforce: Write forces to trajectory.\n");
}

// Traj_AmberNetcdf::processWriteArgs()
int Traj_AmberNetcdf::processWriteArgs(ArgList& argIn) {
  outputTemp_ = argIn.hasKey("remdtraj");
  outputVel_ = argIn.hasKey("velocity");
  outputFrc_ = argIn.hasKey("force");
  return 0;
}

// Traj_AmberNetcdf::setupTrajout()
/** Create Netcdf file specified by filename and set up dimension and
  * variable IDs. 
  */
int Traj_AmberNetcdf::setupTrajout(FileName const& fname, Topology* trajParm,
                                   CoordinateInfo const& cInfoIn, 
                                   int NframesToWrite, bool append)
{
  readAccess_ = false;
  if (!append) {
    CoordinateInfo cInfo = cInfoIn;
    // Deal with output options
    // For backwards compatibility always write temperature if remdtraj is true.
    if (outputTemp_ && !cInfo.HasTemp()) cInfo.SetTemperature(true);
    // Explicitly write velocity - initial frames may not have velocity info.
    if (outputVel_ && !cInfo.HasVel()) cInfo.SetVelocity(true);
    if (outputFrc_ && !cInfo.HasForce()) cInfo.SetForce(true);
    SetCoordInfo( cInfo );
    filename_ = fname;
    // Set up title
    if (Title().empty())
      SetTitle("Cpptraj Generated trajectory");
    // Create NetCDF file.
    if (NC_create( filename_.Full(), NC_AMBERTRAJ, trajParm->Natom(), CoordInfo(), Title() ))
      return 1;
    if (debug_>1) NetcdfDebug();
    // Close Netcdf file. It will be reopened write.
    NC_close();
    // Allocate memory
    if (Coord_!=0) delete[] Coord_;
    Coord_ = new float[ Ncatom3() ];
  } else { // NOTE: File existence is checked for in Trajout
    // Call setupTrajin to set input parameters. This will also allocate
    // memory for coords.
    if (setupTrajin(fname, trajParm) == TRAJIN_ERR) return 1;
    // Check output options.
    if (outputTemp_ && !CoordInfo().HasTemp())
      mprintf("Warning: Cannot append temperature data to NetCDF file '%s'; no temperature dimension.\n",
              filename_.base());
    if (outputVel_ && !CoordInfo().HasVel())
      mprintf("Warning: Cannot append velocity data to NetCDF file '%s'; no velocity dimension.\n",
              filename_.base());
    if (outputFrc_ && !CoordInfo().HasForce())
      mprintf("Warning: Cannot append force data to NetCDF file '%s'; no force dimension.\n",
              filename_.base());
    if (debug_ > 0)
      mprintf("\tNetCDF: Appending %s starting at frame %i\n", filename_.base(), Ncframe()); 
  }
  // Open file
  if ( NC_openWrite( filename_.Full() ) != 0 ) {
    mprinterr("Error: Opening Netcdf file %s for Write.\n", filename_.base());
    return 1;
  }
  return 0;
}

// Traj_AmberNetcdf::readFrame()
/** Get the specified frame from amber netcdf file
  * Coords are a 1 dimensional array of format X1,Y1,Z1,X2,Y2,Z2,...
  */
int Traj_AmberNetcdf::readFrame(int set, Frame& frameIn) {
  start_[0] = set;
  start_[1] = 0;
  start_[2] = 0;
  count_[0] = 1;
  count_[1] = Ncatom();
  count_[2] = 3;

  // Get temperature
  if (TempVID_!=-1) {
    if ( checkNCerr(nc_get_vara_double(ncid_, TempVID_, start_, count_, frameIn.tAddress())) ) {
      mprinterr("Error: Getting replica temperature for frame %i.\n", set+1); 
      return 1;
    }
    //fprintf(stderr,"DEBUG: Replica Temperature %lf\n",F->T);
  }

  // Get time
  if (timeVID_!=-1) {
    float time;
    if (checkNCerr(nc_get_vara_float(ncid_, timeVID_, start_, count_, &time))) {
      mprinterr("Error: Getting time for frame %i.\n", set + 1);
      return 1;
    }
    frameIn.SetTime( (double)time );
  }

  // Read Coords 
  if ( checkNCerr(nc_get_vara_float(ncid_, coordVID_, start_, count_, Coord_)) ) {
    mprinterr("Error: Getting coordinates for frame %i\n", set+1);
    return 1;
  }
  FloatToDouble(frameIn.xAddress(), Coord_);

  // Read Velocities
  if (velocityVID_ != -1) {
    if ( checkNCerr(nc_get_vara_float(ncid_, velocityVID_, start_, count_, Coord_)) ) {
      mprinterr("Error: Getting velocities for frame %i\n", set+1);
      return 1;
    }
    FloatToDouble(frameIn.vAddress(), Coord_);
  }

  // Read Forces
  if (frcVID_ != -1) {
    if ( checkNCerr(nc_get_vara_float(ncid_, frcVID_, start_, count_, Coord_)) ) {
      mprinterr("Error: Getting forces for frame %i\n", set+1);
      return 1;
    }
    FloatToDouble(frameIn.fAddress(), Coord_);
  }

  // Read indices. Input array must be allocated to be size remd_dimension.
  if (indicesVID_!=-1) {
    count_[1] = remd_dimension_;
    if ( checkNCerr(nc_get_vara_int(ncid_, indicesVID_, start_, count_, frameIn.iAddress())) ) {
      mprinterr("Error: Getting replica indices for frame %i.\n", set+1);
      return 1;
    }
    //mprintf("DEBUG:\tReplica indices:");
    //for (int dim=0; dim < remd_dimension_; dim++) mprintf(" %i",remd_indices[dim]);
    //mprintf("\n");
  }

  // Read box info 
  if (cellLengthVID_ != -1) {
    count_[1] = 3;
    count_[2] = 0;
    if (checkNCerr(nc_get_vara_double(ncid_, cellLengthVID_, start_, count_, frameIn.bAddress())))
    {
      mprinterr("Error: Getting cell lengths for frame %i.\n", set+1);
      return 1;
    }
    if (checkNCerr(nc_get_vara_double(ncid_, cellAngleVID_, start_, count_, frameIn.bAddress()+3)))
    {
      mprinterr("Error: Getting cell angles for frame %i.\n", set+1);
      return 1;
    }
  }

  return 0;
}

// Traj_AmberNetcdf::readVelocity()
int Traj_AmberNetcdf::readVelocity(int set, Frame& frameIn) {
  start_[0] = set;
  start_[1] = 0;
  start_[2] = 0;
  count_[0] = 1;
  count_[1] = Ncatom();
  count_[2] = 3;
  // Read Velocities
  if (velocityVID_ != -1) {
    if ( checkNCerr(nc_get_vara_float(ncid_, velocityVID_, start_, count_, Coord_)) ) {
      mprinterr("Error: Getting velocities for frame %i\n", set+1);
      return 1;
    }
    FloatToDouble(frameIn.vAddress(), Coord_);
  }
  return 0;
}

// Traj_AmberNetcdf::readForce()
int Traj_AmberNetcdf::readForce(int set, Frame& frameIn) {
  start_[0] = set;
  start_[1] = 0;
  start_[2] = 0;
  count_[0] = 1;
  count_[1] = Ncatom();
  count_[2] = 3;
  // Read forces
  if (frcVID_ != -1) {
    if ( checkNCerr(nc_get_vara_float(ncid_, frcVID_, start_, count_, Coord_)) ) {
      mprinterr("Error: Getting forces for frame %i\n", set+1);
      return 1;
    }
    FloatToDouble(frameIn.fAddress(), Coord_);
  }
  return 0;
}

// Traj_AmberNetcdf::writeFrame() 
int Traj_AmberNetcdf::writeFrame(int set, Frame const& frameOut) {
  DoubleToFloat(Coord_, frameOut.xAddress());

  // Write coords
  start_[0] = ncframe_;
  start_[1] = 0;
  start_[2] = 0;
  count_[0] = 1;
  count_[1] = Ncatom();
  count_[2] = 3;
  if (checkNCerr(nc_put_vara_float(ncid_,coordVID_,start_,count_,Coord_)) ) {
    mprinterr("Error: Netcdf Writing coords frame %i\n", set+1);
    return 1;
  }

  // Write velocity. FIXME: Should check in setup
  if (CoordInfo().HasVel() && frameOut.HasVelocity()) {
    DoubleToFloat(Coord_, frameOut.vAddress());
    if (checkNCerr(nc_put_vara_float(ncid_, velocityVID_, start_, count_, Coord_)) ) {
      mprinterr("Error: Netcdf writing velocity frame %i\n", set+1);
      return 1;
    }
  }

  // Write forces. FIXME: Should check in setup
  if (CoordInfo().HasForce() && frameOut.HasForce()) {
    DoubleToFloat(Coord_, frameOut.fAddress());
    if (checkNCerr(nc_put_vara_float(ncid_, frcVID_, start_, count_, Coord_)) ) {
      mprinterr("Error: Netcdf writing force frame %i\n", set+1);
      return 1;
    }
  }

  // Write box
  if (cellLengthVID_ != -1) {
    count_[1] = 3;
    count_[2] = 0;
    if (checkNCerr(nc_put_vara_double(ncid_,cellLengthVID_,start_,count_,frameOut.bAddress())) ) {
      mprinterr("Error: Writing cell lengths frame %i.\n", set+1);
      return 1;
    }
    if (checkNCerr(nc_put_vara_double(ncid_,cellAngleVID_,start_,count_, frameOut.bAddress()+3)) ) {
      mprinterr("Error: Writing cell angles frame %i.\n", set+1);
      return 1;
    }
  }

  // Write temperature
  if (TempVID_!=-1) {
    if ( checkNCerr( nc_put_vara_double(ncid_,TempVID_,start_,count_,frameOut.tAddress())) ) {
      mprinterr("Error: Writing temperature frame %i.\n", set+1);
      return 1;
    }
  }

  // Write time
  if (timeVID_ != -1) {
    float tVal = (float)frameOut.Time();
    if ( checkNCerr( nc_put_vara_float(ncid_,timeVID_,start_,count_,&tVal)) ) {
      mprinterr("Error: Writing time frame %i.\n", set+1);
      return 1;
    }
  }
    
  // Write indices
  if (indicesVID_ != -1) {
    count_[1] = remd_dimension_;
    if ( checkNCerr(nc_put_vara_int(ncid_,indicesVID_,start_,count_,frameOut.iAddress())) ) {
      mprinterr("Error: Writing indices frame %i.\n", set+1);
      return 1;
    }
  }

  nc_sync(ncid_); // Necessary after every write??

  ++ncframe_;

  return 0;
}  

// Traj_AmberNetcdf::writeReservoir() TODO: Make Frame const&
int Traj_AmberNetcdf::writeReservoir(int set, Frame const& frame, double energy, int bin) {
  start_[0] = ncframe_;
  start_[1] = 0;
  start_[2] = 0;
  count_[0] = 1;
  count_[1] = Ncatom();
  count_[2] = 3;
  // Coords
  DoubleToFloat(Coord_, frame.xAddress());
  if (checkNCerr(nc_put_vara_float(ncid_,coordVID_,start_,count_,Coord_)) ) {
    mprinterr("Error: Netcdf writing reservoir coords %i\n",set);
    return 1;
  }
  // Velo
  if (velocityVID_ != -1) {
    if (frame.vAddress() == 0) { // TODO: Make it so this can NEVER happen.
      mprinterr("Error: Reservoir expects velocities, but no velocities in frame.\n");
      return 1;
    }
    DoubleToFloat(Coord_, frame.vAddress());
    if (checkNCerr(nc_put_vara_float(ncid_,velocityVID_,start_,count_,Coord_)) ) {
      mprinterr("Error: Netcdf writing reservoir velocities %i\n",set);
      return 1;
    }
  }
  // Eptot, bins
  if ( checkNCerr( nc_put_vara_double(ncid_,eptotVID_,start_,count_,&energy)) ) {
    mprinterr("Error: Writing eptot.\n");
    return 1;
  }
  if (binsVID_ != -1) {
    if ( checkNCerr( nc_put_vara_int(ncid_,binsVID_,start_,count_,&bin)) ) {
      mprinterr("Error: Writing bins.\n");
      return 1;
    }
  }
  // Write box
  if (cellLengthVID_ != -1) {
    count_[1] = 3;
    count_[2] = 0;
    if (checkNCerr(nc_put_vara_double(ncid_,cellLengthVID_,start_,count_,frame.bAddress())) ) {
      mprinterr("Error: Writing cell lengths.\n");
      return 1;
    }
    if (checkNCerr(nc_put_vara_double(ncid_,cellAngleVID_,start_,count_, frame.bAddress()+3)) ) {
      mprinterr("Error: Writing cell angles.\n");
      return 1;
    }
  }
  nc_sync(ncid_); // Necessary after every write??
  ++ncframe_;
  return 0;
}
  
// Traj_AmberNetcdf::info()
void Traj_AmberNetcdf::Info() {
  mprintf("is a NetCDF AMBER trajectory");
  if (readAccess_ && !HasCoords()) mprintf(" (no coordinates)");
  if (CoordInfo().HasVel()) mprintf(" containing velocities");
  if (CoordInfo().HasForce()) mprintf(" containing forces");
  if (CoordInfo().HasTemp()) mprintf(" with replica temperatures");
  if (remd_dimension_ > 0) mprintf(", with %i dimensions", remd_dimension_);
}
#ifdef MPI
#ifdef HAS_PNETCDF
int Traj_AmberNetcdf::parallelOpenTrajin(Parallel::Comm const& commIn) {
  if (Ncid() != -1) return 0;
  int err = ncmpi_open(commIn.MPIcomm(), filename_.full(), NC_NOWRITE, MPI_INFO_NULL, &ncid_);
  if (err != 0) {
    mprinterr("Error: Opening NetCDF file %s for reading in parallel.\n", filename_.full());
    return 1;
  }
  return 0;
}

int Traj_AmberNetcdf::parallelOpenTrajout(Parallel::Comm const& commIn) {
  if (Ncid() != -1) return 0;
  int err = ncmpi_open(commIn.MPIcomm(), filename_.full(), NC_WRITE, MPI_INFO_NULL, &ncid_);
  if (err != 0) {
    mprinterr("Error: Opening NetCDF file '%s' for writing in parallel.\n", filename_.full());
    rprinterr("PNetCDF Error: %s\n", ncmpi_strerror(err));
    return 1;
  }
  err = ncmpi_begin_indep_data( ncid_ ); // Independent data mode
  return 0;
}

int Traj_AmberNetcdf::parallelSetupTrajout(FileName const& fname, Topology* trajParm,
                                           CoordinateInfo const& cInfoIn,
                                           int NframesToWrite, bool append,
                                           Parallel::Comm const& commIn)
{
  int err = 0;
  if (commIn.Master()) {
    err = setupTrajout(fname, trajParm, cInfoIn, NframesToWrite, append);
    NC_close();
  }
  commIn.MasterBcast(&err, 1, MPI_INT);
  if (err != 0) return 1;
  // Synchronize netcdf info on non-master threads.
  Sync();
  rprintf("coordVID= %i\n", coordVID_);
  if (!commIn.Master()) {
    // Non masters need filename and allocate Coord
    filename_ = fname;
    if (Coord_ != 0) delete[] Coord_;
    Coord_ = new float[ Ncatom3() ];
  }
  return 0;
}

int Traj_AmberNetcdf::parallelReadFrame(int set, Frame& frameIn) {
  MPI_Offset pstart_[3];
  MPI_Offset pcount_[3];
  pstart_[0] = set;
  pstart_[1] = 0;
  pstart_[2] = 0;
  pcount_[0] = 1;
  pcount_[1] = Ncatom();
  pcount_[2] = 3;
  // TODO check error better
  int err = ncmpi_get_vara_float_all(ncid_, coordVID_, pstart_, pcount_, Coord_);
  if (err != NC_NOERR) return Parallel::Abort(err);
  FloatToDouble(frameIn.xAddress(), Coord_);
  if (velocityVID_ != -1) {
    err = ncmpi_get_vara_float_all(ncid_, velocityVID_, pstart_, pcount_, Coord_);
    if (err != NC_NOERR) return Parallel::Abort(err);
    FloatToDouble(frameIn.vAddress(), Coord_);
  }
  // TODO force

  pcount_[2] = 0;
  if (cellLengthVID_ != -1) {
    pcount_[1] = 3;
    err = ncmpi_get_vara_double_all(ncid_, cellLengthVID_, pstart_, pcount_, frameIn.bAddress());
    if (err != NC_NOERR) return Parallel::Abort(err);
    err = ncmpi_get_vara_double_all(ncid_, cellAngleVID_, pstart_, pcount_, frameIn.bAddress()+3);
  }
  if (TempVID_ != -1) {
    err = ncmpi_get_vara_double_all(ncid_, TempVID_, pstart_, pcount_, frameIn.tAddress());
    if (err != NC_NOERR) return Parallel::Abort(err);
  }
  if (indicesVID_ != -1) {
    pcount_[1] = remd_dimension_;
    err = ncmpi_get_vara_int_all(ncid_, indicesVID_, pstart_, pcount_, frameIn.iAddress());
    if (err != NC_NOERR) return Parallel::Abort(err);
  }
  return 0;
}

int Traj_AmberNetcdf::parallelWriteFrame(int set, Frame const& frameOut) {
  MPI_Offset pstart_[3];
  MPI_Offset pcount_[3];
  pstart_[0] = set;
  pstart_[1] = 0;
  pstart_[2] = 0;
  pcount_[0] = 1;
  pcount_[1] = Ncatom();
  pcount_[2] = 3;
  // TODO check error better
  DoubleToFloat(Coord_, frameOut.xAddress());
  //int err = ncmpi_put_vara_float_all(ncid_, coordVID_, pstart_, pcount_, Coord_);
  int err = ncmpi_put_vara_float(ncid_, coordVID_, pstart_, pcount_, Coord_);
  if (err != NC_NOERR) return Parallel::Abort(err);
  if (velocityVID_ != -1) {
    DoubleToFloat(Coord_, frameOut.vAddress());
    //err = ncmpi_put_vara_float_all(ncid_, velocityVID_, pstart_, pcount_, Coord_);
    err = ncmpi_put_vara_float(ncid_, velocityVID_, pstart_, pcount_, Coord_);
    if (err != NC_NOERR) return Parallel::Abort(err);
  }
  // TODO force

  pcount_[2] = 0;
  if (cellLengthVID_ != -1) {
    pcount_[1] = 3;
    //err = ncmpi_put_vara_double_all(ncid_, cellLengthVID_, pstart_, pcount_, frameOut.bAddress());
    err = ncmpi_put_vara_double(ncid_, cellLengthVID_, pstart_, pcount_, frameOut.bAddress());
    if (err != NC_NOERR) return Parallel::Abort(err);
    //err = ncmpi_put_vara_double_all(ncid_, cellAngleVID_, pstart_, pcount_, frameOut.bAddress()+3);
    err = ncmpi_put_vara_double(ncid_, cellAngleVID_, pstart_, pcount_, frameOut.bAddress()+3);
  }
  if (TempVID_ != -1) {
    //err = ncmpi_put_vara_double_all(ncid_, TempVID_, pstart_, pcount_, frameOut.tAddress());
    err = ncmpi_put_vara_double(ncid_, TempVID_, pstart_, pcount_, frameOut.tAddress());
    if (err != NC_NOERR) return Parallel::Abort(err);
  }
  if (indicesVID_ != -1) {
    pcount_[1] = remd_dimension_;
    //err = ncmpi_put_vara_int_all(ncid_, indicesVID_, pstart_, pcount_, frameOut.iAddress());
    err = ncmpi_put_vara_int(ncid_, indicesVID_, pstart_, pcount_, frameOut.iAddress());
    if (err != NC_NOERR) return Parallel::Abort(err);
  }
  return 0;
}

void Traj_AmberNetcdf::parallelCloseTraj() {
  if (ncid_ == -1) return;
  ncmpi_close( ncid_ );
  ncid_ = -1;
}
#else /* HAS_PNETCDF */
int Traj_AmberNetcdf::parallelOpenTrajin(Parallel::Comm const& commIn) { return 1; }
int Traj_AmberNetcdf::parallelOpenTrajout(Parallel::Comm const& commIn) { return 1; } 
int Traj_AmberNetcdf::parallelReadFrame(int set, Frame& frameIn) { return 1; }
int Traj_AmberNetcdf::parallelWriteFrame(int set, Frame const& frameOut) { return 1; }
void Traj_AmberNetcdf::parallelCloseTraj() { return; }
int Traj_AmberNetcdf::parallelSetupTrajout(FileName const& fname, Topology* trajParm,
                                           CoordinateInfo const& cInfoIn,
                                           int NframesToWrite, bool append,
                                           Parallel::Comm const& commIn)
{ return 1; }
#endif /* HAS_PNETCDF */
#endif /* MPI */
#endif /* BINTRAJ */

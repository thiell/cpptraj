#ifndef INC_CPPTRAJ_H
#define INC_CPPTRAJ_H 
#include "TrajinList.h"
#include "TrajoutList.h"
#include "FrameList.h"
#include "TopologyList.h"
#include "DataSetList.h"
#include "DataFileList.h"
#include "ActionList.h"
#include "AnalysisList.h"
#include "ArgList.h"
// Class: Cpptraj:
/// Hold state information.
/** This is the main class for cpptraj. It holds all data and controls the 
 *  overall flow of the program. It exists in main.cpp.
 */
class Cpptraj {
  public:
    /// Set debug level for all components
    void SetGlobalDebug(int);
    void AddParm(char *);
    /// Function that decides where to send commands
    void Dispatch(char*);        

    Cpptraj();
    /// Controls main flow of the program.
    int Run();
  private:
    /// List of parameter files 
    TopologyList parmFileList;
    /// List of input trajectory files
    TrajinList trajinList;
    /// List of reference coordinate files
    FrameList refFrames; 
    /// List of output trajectory files 
    TrajoutList trajoutList;
    /// List of actions to be performed each frame
    ActionList actionList;    
    /// List of analyses to be performed on datasets
    AnalysisList analysisList;
    /// List of generated data sets
    DataSetList DSL;
    /// List of datafiles that data sets will be written to
    DataFileList DFL;
    /// The debug level
    int debug;
    /// If true the progress of reading input trajectories will be shown
    bool showProgress;
    /// If true cpptraj will exit if errors are encountered instead of trying to continue
    bool exitOnError;
};
#endif
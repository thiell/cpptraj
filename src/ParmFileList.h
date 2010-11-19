#ifndef INC_PARMFILELIST_H
#define INC_PARMFILELIST_H
/* Class: ParmFileList
 * Holds a list of parameter files. Can either add new parm files
 * by filename, or add existing files by address. Search for parm
 * files in a list by index or full/base filename.
 */
#include "AmberParm.h"

class ParmFileList {
    AmberParm **ParmList;
    int Nparm;
    int debug;
    int hasCopies;

  public:

    ParmFileList();
    ~ParmFileList();

    void SetDebug(int);
    int Add(char *);
    int Add(AmberParm *);
    AmberParm *GetParm(int);
    int GetParmIndex(char *);
    void Print();
};
#endif

// DataFileList
#include <cstdio>
//#include <cstdlib>
#include "DataFileList.h"
#include "PtrajMpi.h" // NOTE: Only needed for worldrank - move to DataSet?

// CONSTRUCTOR
DataFileList::DataFileList() {
//  fprintf(stderr,"DataFileList CONSTRUCTOR\n");
  debug=0;
}

// DESTRUCTOR
DataFileList::~DataFileList() {
//  fprintf(stderr,"DataFileList DESTRUCTOR\n");
  for (it = this->begin(); it != this->end(); it++)
    delete *it;
}

// Set DataFile debug level
void DataFileList::SetDebug(int debugIn) {
  debug=debugIn;
  if (debug>0)
    fprintf(stdout,"DataFileList DEBUG LEVEL SET TO %i\n",debug);
}

/*
 * DataFileList::GetDataFile()
 * Return DataFile specified by given file name if it exists in the list,
 * otherwise return null.
 */
DataFile *DataFileList::GetDataFile(char *nameIn) {
  DataFile *Current;

  Current=NULL;
  for (it = this->begin(); it != this->end(); it++) {
    if ( (*it)->NameIs(nameIn) ) {
      Current = *it;
      break;
    }
  }
  
  return Current;
}

/* 
 * DataFileList::Add()
 * Add dataset to datafile in list with given file name.
 */
int DataFileList::Add(char *nameIn, DataSet *D) {
  DataFile *Current;
  //char tempName[1024]; // DEBUG

  // If no filename, no output desired
  if (nameIn==NULL) return 0;

  // Append thread prefix to filename
  //sprintf(tempName,"%s.%03i",nameIn,worldrank); // DEBUG

  // Check if this filename already in use
  Current = this->GetDataFile(nameIn);
/*  Current=NULL;
  for (it = this->begin(); it != this->end(); it++) {
    if ( (*it)->NameIs(nameIn) ) {
      Current = *it;
      break;
    }
  }*/

  // If no DataFile associated with nameIn, create new datafile
  if (Current==NULL) {
    Current = new DataFile(nameIn); 
    this->push_back(Current);
  }

  // Add the dataset to the current DataFile
  Current->AddSet(D);

  // Set debug level
  Current->SetDebug(debug);

  // DEBUG
  //fprintf(stdout,"** ADDED DATASET %s TO FILE %s\n",D->Name(),Current->filename);

  return 0;
}

/*
 * DataFileList::Info()
 * Print information on what datasets are going to what datafiles
 */
void DataFileList::Info() {

  if (this->empty()) {
    fprintf(stdout,"NO DATASETS WILL BE OUTPUT\n");
    return;
  }

  fprintf(stdout,"DATAFILE OUTPUT:\n");
  for (it = this->begin(); it != this->end(); it++) {
    fprintf(stdout,"  %s: ",(*it)->filename);
    (*it)->DataSetNames();
    fprintf(stdout,"\n");
  }
}

/*
 * DataFileList::Write()
 * Call write for all datafiles in list.
 * Only master does data file writing.
 */
void DataFileList::Write(int maxFrames) {

  if (worldrank!=0) return; // DISABLE FOR DEBUG

  for (it = this->begin(); it != this->end(); it++)
    (*it)->Write(maxFrames,false);
}

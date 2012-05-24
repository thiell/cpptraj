// ---------- CSTDLIB includes -------------------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>

// ---------- Defines ----------------------------------------------------------
#define ACTION_MODULE
// ---------- PTRAJ includes ---------------------------------------------------
#include "ptraj_actions.h"
#include "ptraj_common.h" // scalarInfo
#include "ptraj_stack.h"
#include "ptraj_arg.h"
#include "ptraj_scalar.h"

// ---------- CPPTRAJ includes -------------------------------------------------
// Constants
#include "Constants.h"
// Distance routines
#include "DistRoutines.h"
// Torsion Routines
#include "TorsionRoutines.h"
// MPI worldrank and size
#include "MpiRoutines.h"

// ========== COMMON internal functions ========================================
#ifdef MPI
static void printError(char *actionName, char *fmt, ...) {
  va_list argp;
  va_start(argp, fmt);
//#ifdef MPI
  if (worldrank == 0) {
//#endif    
    printf("WARNING in ptraj(), %s: ", actionName);
    vprintf(fmt, argp);
//#ifdef MPI
  }
//#endif
  va_end(argp);
}
static void printParallelError(char *actionName) {
  printError(actionName, "Parallel implementation of action not supported.\nIgnoring command...\n");
}
#endif

#ifdef MPI
// printMPIerr()
/// Wrapper for MPI_Error string.
static void printMPIerr(int err, char *actionName) {
  int len,eclass,i;
  char buffer[BUFFER_SIZE];
  
  MPI_Error_string(err,buffer,&len);
  MPI_Error_class(err,&eclass);
  // Remove newlines from MPI error string
  for (i=0; i<len; i++) 
    if (buffer[i]=='\n') buffer[i]=':';
  fprintf(stdout,"[%i] MPI ERROR %d: %s: [%s]\n",worldrank,eclass,actionName,buffer);

  return;
}
#endif

// ========== STRING functions =================================================
/*
static char *toLowerCase( char *string_in ) {
  int i, length;

  length = strlen( string_in );
  for (i=0; i < length; i++ ) {
    string_in[i] = tolower( string_in[i] );
  }
  return string_in;
}
static int stringMatch(char *string, char *match) {
  char *lowerString, *lowerMatch;

  if (string == NULL || match == NULL) return 0;

  lowerString = safe_malloc( (size_t) sizeof(char) * (strlen(string)+1));
  strcpy(lowerString,string);
  lowerMatch  = safe_malloc( (size_t) sizeof(char) * (strlen(match)+1));
  strcpy(lowerMatch,match);
  lowerString = toLowerCase(lowerString);
  lowerMatch  = toLowerCase(lowerMatch);

   // this should be an exact, not partial match
   //    if (strncmp(lowerMatch, lowerString, strlen(lowerMatch)) == 0) {
  if (strcmp(lowerMatch, lowerString) == 0) {
    safe_free(lowerString);
    safe_free(lowerMatch);
    return 1;
  } else {
    safe_free(lowerString);
    safe_free(lowerMatch);
    return 0;
  }
}
*/
// ========== FILE IO functions ================================================
// =============================================================================

// -----------------------------------------------------------------------------
// ptrajCopyAction()
actionInformation *ptrajCopyAction(actionInformation **actioninp) {
  actionInformation *action, *actionin;
  /*
   *  Make a copy of the input action pointer.
   */
  actionin = *actioninp;
  action = (actionInformation*)safe_malloc(sizeof(actionInformation));
  INITIALIZE_actionInformation(action);
  action->fxn	= actionin->fxn;
  action->type 	= actionin->type;
  action->iarg1	= actionin->iarg1; 
  action->iarg2	= actionin->iarg2; 
  action->iarg3	= actionin->iarg3; 
  action->iarg4	= actionin->iarg4; 
  action->iarg5	= actionin->iarg5; 
  action->iarg6	= actionin->iarg6; 
  action->iarg7	= actionin->iarg7; 
  action->darg1	= actionin->darg1; 
  action->darg2	= actionin->darg2; 
  action->darg3	= actionin->darg3; 
  action->darg4	= actionin->darg4; 
  action->suppressProcessing = actionin->suppressProcessing;  
  action->performSecondPass	 = actionin->performSecondPass;
  /* The following members are pointers. Just copy from the old action. 
     So you need to allocate new space for the members. */
  action->state	= actionin->state;
  action->mask	= actionin->mask;
  action->carg1	= actionin->carg1; 
  action->carg2	= actionin->carg2; 
  action->carg3	= actionin->carg3; 
  action->carg4	= actionin->carg4; 
  action->carg5	= actionin->carg5; 
  action->carg6	= actionin->carg6; 
  action->carg7	= actionin->carg7; 
  return action;
}
// -----------------------------------------------------------------------------

/** ACTION ROUTINE *************************************************************
 *
 *  transformDiffusion()   --- calculate mean squared displacements vs. time
 *
 ******************************************************************************/
typedef struct _transformDiffusionInfo {
  double *dx;           /* reference coordinates: MUST BE SET */
  double *dy;           /*  TO NULL IN transformSetup         */
  double *dz;
  double timePerFrame;  /*  in picoseconds */
  double *prevx;        /*  the previous x for active atoms */
  double *prevy;        /*  the previous y                  */
  double *prevz;        /*  the previous z                  */
  double *distancex;    /*  the active atoms distances      */
  double *distancey;    /*  the active atoms distances      */
  double *distancez;    /*  the active atoms distances      */
  double *distance;     /*  the active atoms distances      */
  double *deltax;
  double *deltay;
  double *deltaz;
  int activeAtoms;      /*  the total number of active atoms in the mask */
  int elapsedFrames;    /*  number of frames so far... */
  char *outputFilenameRoot;
  FILE *outputx;
  FILE *outputy;
  FILE *outputz;
  FILE *outputr;
  FILE *outputa;
  FILE *outputxyz;
} transformDiffusionInfo;

#define INITIALIZE_transformDiffusionInfo(_p_) \
  _p_->dx             = NULL; \
  _p_->dy             = NULL; \
  _p_->dz             = NULL; \
  _p_->timePerFrame   = 0.0;  \
  _p_->prevx          = NULL; \
  _p_->prevy          = NULL; \
  _p_->prevz          = NULL; \
  _p_->distancex      = NULL; \
  _p_->distancey      = NULL; \
  _p_->distancez      = NULL; \
  _p_->distance       = NULL; \
  _p_->deltax         = NULL; \
  _p_->deltay         = NULL; \
  _p_->deltaz         = NULL; \
  _p_->activeAtoms    = 0; \
  _p_->elapsedFrames  = 0; \
  _p_->outputFilenameRoot = NULL; \
  _p_->outputx        = NULL; \
  _p_->outputy        = NULL; \
  _p_->outputz        = NULL; \
  _p_->outputr        = NULL; \
  _p_->outputxyz      = NULL

// transformDiffusion()
int transformDiffusion(actionInformation *action, 
		   double *x, double *y, double *z,
		   double *box, int mode)
{
  //char *name = "diffusion";
  argStackType **argumentStackPointer;
  char *buffer;
  ptrajState *state;
  transformDiffusionInfo *diffusionInfo;
  int i;
  int currentAtom;
  double delx, dely, delz;
  double xx, yy, zz;
  double avgx, avgy, avgz;
  double average;
  double time;

  /*
   *  USAGE:
   *
   *     diffusion mask [average] [time <time per frame>]
   *
   *  action argument usage:
   *
   *  mask:
   *    atoms for which the diffusion is calculated
   *  iarg1: 
   *    0 -- default, print out average diffusion and diffusion values for
   *         each of the active atoms
   *    1 -- only print out averages
   *  carg1:
   *    the transformDiffusionInfo structure 
   *
   */
  xx = 0; yy = 0; zz = 0;

  if (mode == PTRAJ_SETUP) {
    /*
     *  ACTION: PTRAJ_SETUP
     */

#ifdef MPI
    printParallelError(name);
    return -1;
#endif

    argumentStackPointer = (argStackType **) action->carg1;
    action->carg1 = NULL;

    diffusionInfo = safe_malloc(sizeof(transformDiffusionInfo));
    INITIALIZE_transformDiffusionInfo(diffusionInfo);

    buffer = getArgumentString(argumentStackPointer, NULL);
    action->mask = processAtomMask(buffer, action->state);
    safe_free(buffer);

    diffusionInfo->timePerFrame = getArgumentDouble(argumentStackPointer, 1.0);
    if (diffusionInfo->timePerFrame < 0) {
      error("ptraj()", "diffusion time per frame incorrectly specified\n");
    }

    action->iarg1 = argumentStackContains(argumentStackPointer, "average");

    diffusionInfo->outputFilenameRoot = getArgumentString(argumentStackPointer, "diffusion");
    action->carg1 = (void *) diffusionInfo;
    return 0;

  }


  diffusionInfo = (transformDiffusionInfo *) action->carg1;


  if (mode == PTRAJ_STATUS) {

    /*
     *  ACTION: PTRAJ_STATUS
     */

    fprintf(stdout, "  DIFFUSION\n");
    if (action->iarg1 == 1) {
      fprintf(stdout, "      Only the average results will ");
    } else {
      fprintf(stdout, "      The average and individual results will ");
    }
    fprintf(stdout, "be dumped to %s_?.xmgr\n", 
	    diffusionInfo->outputFilenameRoot);
    fprintf(stdout, "      The time between frames in psec is %5.3f.\n",
	    diffusionInfo->timePerFrame);
    fprintf(stdout, 
	    "      To calculated diffusion constants, calculate the slope of the lines(s)\n");
    fprintf(stdout, 
	    "      and multiply by 10.0/6.0; this will give units of 1x10**-5 cm**2/s\n");
    if (action->mask) {	    
      fprintf(stdout, "      The atoms in the calculation follow: ");
      printAtomMask(stdout, action->mask, action->state);
      fprintf(stdout, "\n");
    }
    return 0;

  } else if (mode == PTRAJ_CLEANUP) {

    /*
     *  ACTION: PTRAJ_CLEANUP
     */
    safe_free(action->mask); 
    safe_free(diffusionInfo->dx);
    safe_free(diffusionInfo->dy);
    safe_free(diffusionInfo->dz);
    safe_free(diffusionInfo->prevx);
    safe_free(diffusionInfo->prevy);
    safe_free(diffusionInfo->prevz);
    safe_free(diffusionInfo->distancex);
    safe_free(diffusionInfo->distancey);
    safe_free(diffusionInfo->distancez);
    safe_free(diffusionInfo->distance);
    safe_free(diffusionInfo->deltax);
    safe_free(diffusionInfo->deltay);
    safe_free(diffusionInfo->deltaz);
    safe_free(diffusionInfo->outputFilenameRoot);
    safe_fclose(diffusionInfo->outputx);
    safe_fclose(diffusionInfo->outputy);
    safe_fclose(diffusionInfo->outputz);
    safe_fclose(diffusionInfo->outputr);
    safe_fclose(diffusionInfo->outputa);
    INITIALIZE_transformDiffusionInfo(diffusionInfo);
    safe_free(diffusionInfo);

  }


  if (mode != PTRAJ_ACTION) return 0;

  /*
   *  ACTION: PTRAJ_ACTION
   */

  state = (ptrajState *) action->state;

  /*
   *  update local state information
   */
  for (i=0; i<6; i++)
    state->box[i] = box[i];

  diffusionInfo = (transformDiffusionInfo *) action->carg1;
  diffusionInfo->elapsedFrames++;

  /*
   *  load up initial frame if necessary
   */
  if ( diffusionInfo->dx == NULL ) {

    diffusionInfo->dx = safe_malloc(sizeof(double) * state->atoms);
    diffusionInfo->dy = safe_malloc(sizeof(double) * state->atoms);
    diffusionInfo->dz = safe_malloc(sizeof(double) * state->atoms);
  
    for (i=0; i < state->atoms; i++) {
      diffusionInfo->dx[i] = x[i];
      diffusionInfo->dy[i] = y[i];
      diffusionInfo->dz[i] = z[i];
    }
    for (i=0; i < state->atoms; i++) {
      if (action->mask == NULL || action->mask[i])
	diffusionInfo->activeAtoms++;
    }

    diffusionInfo->prevx = 
      safe_malloc(sizeof(double) * diffusionInfo->activeAtoms);
    diffusionInfo->prevy = 
      safe_malloc(sizeof(double) * diffusionInfo->activeAtoms);
    diffusionInfo->prevz = 
      safe_malloc(sizeof(double) * diffusionInfo->activeAtoms);

    currentAtom = 0;
    for (i=0; i < state->atoms; i++) {
      if (action->mask == NULL || action->mask[i]) {
	diffusionInfo->prevx[currentAtom] = x[i];
	diffusionInfo->prevy[currentAtom] = y[i];
	diffusionInfo->prevz[currentAtom] = z[i];
	currentAtom++;
      }
    }

    diffusionInfo->distancex = 
      safe_malloc(sizeof(double) * diffusionInfo->activeAtoms);
    diffusionInfo->distancey = 
      safe_malloc(sizeof(double) * diffusionInfo->activeAtoms);
    diffusionInfo->distancez = 
      safe_malloc(sizeof(double) * diffusionInfo->activeAtoms);
    diffusionInfo->distance = 
      safe_malloc(sizeof(double) * diffusionInfo->activeAtoms);
    
    diffusionInfo->elapsedFrames = 0;

    diffusionInfo->deltax = 
      safe_malloc(sizeof(double) * diffusionInfo->activeAtoms);
    diffusionInfo->deltay = 
      safe_malloc(sizeof(double) * diffusionInfo->activeAtoms);
    diffusionInfo->deltaz = 
      safe_malloc(sizeof(double) * diffusionInfo->activeAtoms);
    for (i=0; i < diffusionInfo->activeAtoms; i++) {
      diffusionInfo->deltax[i] = 0.0;
      diffusionInfo->deltay[i] = 0.0;
      diffusionInfo->deltaz[i] = 0.0;
    }

    buffer = (char *) safe_malloc(sizeof(char) * 
				  (strlen(diffusionInfo->outputFilenameRoot)+10));
    strcpy(buffer, diffusionInfo->outputFilenameRoot);
    strcat(buffer, "_x.xmgr");
    if ( ( diffusionInfo->outputx = fopen(buffer,"w") ) == NULL) {
      fprintf(stdout, "WARNING in ptraj(), diffusion: Cannot open diffusion output file\n");
    }

    strcpy(buffer, diffusionInfo->outputFilenameRoot);
    strcat(buffer, "_y.xmgr");
    if ( ( diffusionInfo->outputy = fopen(buffer,"w") ) == NULL) {
      fprintf(stdout, "WARNING in ptraj(), diffusion: Cannot open diffusion output file\n");
    }

    strcpy(buffer, diffusionInfo->outputFilenameRoot);
    strcat(buffer, "_z.xmgr");
    if ( ( diffusionInfo->outputz = fopen(buffer,"w") ) == NULL) {
      fprintf(stdout, "WARNING in ptraj(), diffusion: Cannot open diffusion output file\n");
    }

    strcpy(buffer, diffusionInfo->outputFilenameRoot);
    strcat(buffer, "_r.xmgr");
    if ( ( diffusionInfo->outputr = fopen(buffer,"w") ) == NULL) {
      fprintf(stdout, "WARNING in ptraj(), diffusion: Cannot open diffusion output file\n");
    }

    strcpy(buffer, diffusionInfo->outputFilenameRoot);
    strcat(buffer, "_a.xmgr");
    if ( ( diffusionInfo->outputa = fopen(buffer,"w") ) == NULL) {
      fprintf(stdout, "WARNING in ptraj(), diffusion: Cannot open diffusion output file\n");
    }
    if (prnlev > 2) {
      if ( ( diffusionInfo->outputxyz = fopen("diffusion_xyz.xmgr","w") ) == NULL) {
	fprintf(stdout, "WARNING in ptraj(), diffusion: Cannot open diffusion output file\n");
      }
    }
    safe_free(buffer);
    return 1;
  }


  currentAtom = 0;
  for (i=0; i < state->atoms; i++) {
    if ( action->mask == NULL || action->mask[i] == 1 ) {

      if ( currentAtom > diffusionInfo->activeAtoms )
	error("junk", "currentAtom out of bounds!\n");

      /*
       *  calculate distance to previous frames coordinates
       */
      delx = x[i] - diffusionInfo->prevx[currentAtom];
      dely = y[i] - diffusionInfo->prevy[currentAtom];
      delz = z[i] - diffusionInfo->prevz[currentAtom];

      /*
       *  if the particle moved more than half the box, assume
       *  it was imaged and adjust the distance of the total
       *  movement with respect to the original frame...
       */
      if ( state->box[0] > 0.0 ) {
	if ( delx > state->box[0]/2.0 )
	  diffusionInfo->deltax[currentAtom] -= state->box[0];
	else if ( delx < -state->box[0]/2.0 )
	  diffusionInfo->deltax[currentAtom] += state->box[0];
	if ( dely > state->box[1]/2.0 )
	  diffusionInfo->deltay[currentAtom] -= state->box[1];
	else if ( dely < -state->box[1]/2.0 )
	  diffusionInfo->deltay[currentAtom] += state->box[1];
	if ( delz > state->box[2]/2.0 ) 
	  diffusionInfo->deltaz[currentAtom] -= state->box[2];
	else if ( delz < -state->box[2]/2.0 )
	  diffusionInfo->deltaz[currentAtom] += state->box[2];
      }

      if (prnlev > 2) {
	fprintf(stdout, "ATOM: %5i %10.3f %10.3f %10.3f",
		i, x[i], delx,
		diffusionInfo->deltax[currentAtom]);
      }

      /*
       *  set the current x with reference to the un-imaged
       *  trajectory
       */
      xx = x[i] + diffusionInfo->deltax[currentAtom];
      yy = y[i] + diffusionInfo->deltay[currentAtom];
      zz = z[i] + diffusionInfo->deltaz[currentAtom];

      /*
       *  calculate the distance between this "fixed" coordinate
       *  and the reference (initial) frame
       */
      delx = xx - diffusionInfo->dx[i];
      dely = yy - diffusionInfo->dy[i];
      delz = zz - diffusionInfo->dz[i];

      if (prnlev > 2) 
	fprintf(stdout, " %10.3f\n", delx);


      /*
       *  store the distance for this atom
       */
      diffusionInfo->distancex[currentAtom] = delx*delx;
      diffusionInfo->distancey[currentAtom] = dely*dely;
      diffusionInfo->distancez[currentAtom] = delz*delz;
      diffusionInfo->distance[currentAtom] = delx*delx + dely*dely + delz*delz;

      /*
       *  update the previous coordinate set to match the
       *  current coordinates
       */
      diffusionInfo->prevx[currentAtom] = x[i];
      diffusionInfo->prevy[currentAtom] = y[i];
      diffusionInfo->prevz[currentAtom] = z[i];

      /*
       *  increment the current atom pointer
       */
      currentAtom++;
    }
  }

  /*
   *  accumulate averages
   */
  average = 0.0;
  avgx = 0.0;
  avgy = 0.0;
  avgz = 0.0;
  for (i=0; i < diffusionInfo->activeAtoms; i++) {
    average += diffusionInfo->distance[i];
    avgx += diffusionInfo->distancex[i];
    avgy += diffusionInfo->distancey[i];
    avgz += diffusionInfo->distancez[i];
  }
    
  average /= (double) diffusionInfo->activeAtoms;
  avgx /= (double) diffusionInfo->activeAtoms;
  avgy /= (double) diffusionInfo->activeAtoms;
  avgz /= (double) diffusionInfo->activeAtoms;


  /*
   *  dump output
   */
  time = diffusionInfo->elapsedFrames * diffusionInfo->timePerFrame;
  fprintf(diffusionInfo->outputx, "%8.3f  %8.3f", 
	  time,
	  avgx );
  fprintf(diffusionInfo->outputy, "%8.3f  %8.3f", 
	  time,
	  avgy );
  fprintf(diffusionInfo->outputz, "%8.3f  %8.3f", 
	  time,
	  avgz );
  fprintf(diffusionInfo->outputr, "%8.3f  %8.3f", 
	  time,
	  average);
  fprintf(diffusionInfo->outputa, "%8.3f  %8.3f", 
	  time,
	  sqrt(average));
  if (prnlev > 2)
    fprintf(diffusionInfo->outputxyz, "%8.3f  %8.3f  %8.3f  %8.3f", 
	    time, xx, yy, zz);

  /*
   *  dump individual values if requested
   */
  if (action->iarg1 == 0) {
    for (i = 0; i < diffusionInfo->activeAtoms; i++) {
      fprintf(diffusionInfo->outputx, "  %8.3f", 
	      diffusionInfo->distancex[i] );
      fprintf(diffusionInfo->outputy, "  %8.3f", 
	      diffusionInfo->distancey[i] );
      fprintf(diffusionInfo->outputz, "  %8.3f", 
	      diffusionInfo->distancez[i] );
      fprintf(diffusionInfo->outputr, "  %8.3f", 
	      diffusionInfo->distance[i] );
      fprintf(diffusionInfo->outputa, "  %8.3f", 
	      sqrt(diffusionInfo->distance[i]) );
    }
  }

  /*
   *  dump newlines 
   */
  fprintf(diffusionInfo->outputx, "\n");
  fprintf(diffusionInfo->outputy, "\n");
  fprintf(diffusionInfo->outputz, "\n");
  fprintf(diffusionInfo->outputr, "\n");
  fprintf(diffusionInfo->outputa, "\n");
  if (prnlev > 2)
    fprintf(diffusionInfo->outputxyz, "\n");

  fflush(diffusionInfo->outputx);
  fflush(diffusionInfo->outputy);
  fflush(diffusionInfo->outputz);
  fflush(diffusionInfo->outputr);
  fflush(diffusionInfo->outputa);
  return 1;
}

/** ACTION ROUTINE *************************************************************
 *
 *  transformDNAiontracker
 *
 ******************************************************************************/
// setupDistance()
static void setupDistance(double *X, double *Y, double *Z, double *x, double *y, double *z,
                          ptrajState *state, int *mask1, int *mask2, int atom1, int atom2)
{
  double total_mass1;
  double total_mass2;
  double atommass;
  int i;

  X[0] = 0.0;
  Y[0] = 0.0;
  Z[0] = 0.0;
  total_mass1 = 0.0;
  X[1] = 0.0;
  Y[1] = 0.0;
  Z[1] = 0.0;
  total_mass2 = 0.0;
  atommass=1.0;

  if (atom1 == -1) {

    for (i=0; i < state->atoms; i++) {
      if (mask1[i]) {
        if (state->masses[i]!=0.0) atommass=state->masses[i];
        X[0] += atommass * x[i];
        Y[0] += atommass * y[i];
        Z[0] += atommass * z[i];
        total_mass1 += atommass;
      }
    }
    X[0] /= total_mass1;
    Y[0] /= total_mass1;
    Z[0] /= total_mass1;

  } else {

    X[0] = x[atom1];
    Y[0] = y[atom1];
    Z[0] = z[atom1];

  }

  if (atom2 == -1) {

    for (i=0; i < state->atoms; i++) {
      if (mask2[i]) {
        if (state->masses[i]!=0.0) atommass=state->masses[i];
        X[1] += atommass * x[i];
        Y[1] += atommass * y[i];
        Z[1] += atommass * z[i];
        total_mass2 += atommass;
      }
    }
    X[1] /= total_mass2;
    Y[1] /= total_mass2;
    Z[1] /= total_mass2;

  } else {

    X[1] = x[atom2];
    Y[1] = y[atom2];
    Z[1] = z[atom2];
  }
}

// transformDNAiontracker()
int transformDNAiontracker(actionInformation *action, 
		       double *x, double *y, double *z, 
		       double *box, int mode)
{
  //char *name = "dnaiontracker";
  argStackType **argumentStackPointer;
  char *buffer;
  scalarInfo *distanceInfo;
  ptrajState *state;
  int i, mask1tot, mask2tot, mask3tot, mask4tot;
  double X[2], Y[2], Z[2], ucell[9], recip[9];
  double pp_centroidx, pp_centroidy, pp_centroidz;
  double d_p1ion, d_p2ion, d_baseion, d_cut;
  double d_min, d_tmp;
  double d_pp, poffset, d_pbase;
  int bound, boundLower, boundUpper;

  FILE *outFile;

  /*
   *  USAGE:
   *
   *    dnaiontracker name mask_p1 mask_p2 mask_base mask_ions \
   *      [poffset <value>] [out <filename>] [time <interval>] [noimage] [shortest | count]
   *
   *  action argument usage:
   *
   *  iarg1: 1 implies don't image
   *  iarg3: flag to determine if distance (shortest) or count is saved
   *  darg1: time interval in ps (for output)
   *  darg2: poffset (perpendicular offset)
   *  carg1:
   *     a scalarInfo structure
   */


  if (mode == PTRAJ_SETUP) {
    /*
     *  ACTION: PTRAJ_SETUP
     */

#ifdef MPI
    printParallelError(name);
    return -1;
#endif

    argumentStackPointer = (argStackType **) action->carg1;
    action->carg1 = NULL;

       /*
        *  set up the information necessary to place this on the scalarStack
        */
    distanceInfo = (scalarInfo *) safe_malloc(sizeof(scalarInfo));
    INITIALIZE_scalarInfo(distanceInfo);
    distanceInfo->mode = SCALAR_DISTANCE;
    distanceInfo->totalFrames = -1;

    distanceInfo->name = getArgumentString(argumentStackPointer, NULL);
    if (distanceInfo->name == NULL) {
      fprintf(stdout, "WARNING: ptraj(), dnaiontracker: It is necessary to specify a unique name\n");
      fprintf(stdout, "for each specified tracking.  Ignoring command...\n");
      safe_free(distanceInfo);
      return -1;
    } /*else if ( scalarStackGetName(&scalarStack, distanceInfo->name) != NULL ) {
      fprintf(stdout, "WARNING: ptraj(), dnaiontracker: The chosen name (%s) has already been used.\n",
	      distanceInfo->name);
      fprintf(stdout, "Ignoring command...\n");
      safe_free(distanceInfo);
      return -1;
    }*/
    distanceInfo->state = action->state;

       /*
        *  grab the filename
        */
    distanceInfo->filename = argumentStackKeyToString(argumentStackPointer, "out", NULL);

       /*
        *  grab the perpendicular offset (poffset)
        */
    action->darg2 = argumentStackKeyToDouble(argumentStackPointer, "poffset", 5.0);


       /*
        *  decide whether to bin the shortest distances seen (to phosphates or base
	*  centroid) or whether to simply bin count or counttopcone or countbottomcone
	*/
    action->iarg3 = 0;
    if (argumentStackContains(argumentStackPointer, "shortest"))
      action->iarg3 = 1;
    else if (argumentStackContains(argumentStackPointer, "counttopcone"))
      action->iarg3 = 2;
    else if (argumentStackContains(argumentStackPointer, "countbottomcone"))
      action->iarg3 = 3;
    else if (argumentStackContains(argumentStackPointer, "count"))
      action->iarg3 = 0;


       /*
        *  push the distance info on to the distance stack
        */
      fprintf(stdout,"Warning: scalarStack disabled for Cpptraj\n");
//    pushBottomStack(&scalarStack, (void *) distanceInfo);

       /*
        *  grab a time interval between frames in ps (for output)
        */
    action->darg1 = argumentStackKeyToDouble(argumentStackPointer, "time", 1.0);

       /*
        *  check to see if we want imaging disabled
        */
    action->iarg1 = argumentStackContains(argumentStackPointer, "noimage");

       /*
        *  process the atom masks
        */
    buffer = getArgumentString(argumentStackPointer, NULL);
    if (buffer == NULL) {
      fprintf(stdout, 
	      "WARNING in ptraj(), dnaiontracker: Error in specification of the first phosphate mask\n");
      fprintf(stdout, "Ignoring command\n");
      safe_free(distanceInfo);
      return -1;

    } else {
      distanceInfo->mask1 = processAtomMask(buffer, action->state);
      safe_free(buffer);
    }

    buffer = getArgumentString(argumentStackPointer, NULL);
    if (buffer == NULL) {
      fprintf(stdout, 
	      "WARNING in ptraj(), dnaiontracker: Error in specification of the second phosphate mask\n");
      fprintf(stdout, "Ignoring command\n");
      safe_free(distanceInfo);
      return -1;

    } else {
      distanceInfo->mask2 = processAtomMask(buffer, action->state);
      safe_free(buffer);
    }

    buffer = getArgumentString(argumentStackPointer, NULL);
    if (buffer == NULL) {
      fprintf(stdout, 
	      "WARNING in ptraj(), dnaiontracker: Error in specification of the base centroid mask\n");
      fprintf(stdout, "Ignoring command\n");
      safe_free(distanceInfo);
      return -1;

    } else {
      distanceInfo->mask3 = processAtomMask(buffer, action->state);
      safe_free(buffer);
    }

    buffer = getArgumentString(argumentStackPointer, NULL);
    if (buffer == NULL) {
      fprintf(stdout, 
	      "WARNING in ptraj(), dnaiontracker: Error in specification of the ion mask\n");
      fprintf(stdout, "Ignoring command\n");
      safe_free(distanceInfo);
      return -1;

    } else {
      distanceInfo->mask4 = processAtomMask(buffer, action->state);
      safe_free(buffer);
    }


    /*
     *  check to see if each mask only represents a single atom or not (to save on
     *  memory)
     */
    mask1tot = 0; distanceInfo->atom1 = -1;
    mask2tot = 0; distanceInfo->atom2 = -1;
    mask3tot = 0; distanceInfo->atom4 = -1;
    mask4tot = 0; distanceInfo->atom3 = -1;
    for (i=0; i < action->state->atoms; i++) {
      if (distanceInfo->mask1[i] == 1) {
	mask1tot++;
	distanceInfo->atom1 = i;
      }
      if (distanceInfo->mask2[i] == 1) {
	mask2tot++;
	distanceInfo->atom2 = i;
      }
      if (distanceInfo->mask3[i] == 1) {
	mask3tot++;
	distanceInfo->atom3 = i;
      }
      if (distanceInfo->mask4[i] == 1) {
	mask4tot++;
	distanceInfo->atom4 = i;
      }
    }

    if (mask1tot == 0) {
      fprintf(stdout, 
	      "WARNING in ptraj(), distance: No atoms selected in mask1, ignoring command\n");
      safe_free(distanceInfo->mask1);
      safe_free(distanceInfo);
      return -1;
    } else if (mask1tot == 1) {
      safe_free(distanceInfo->mask1);
      distanceInfo->mask1 = NULL;
    } else
      distanceInfo->atom1 = -1;

    if (mask2tot == 0) {
      fprintf(stdout, 
	      "WARNING in ptraj(), distance: No atoms selected in mask2, ignoring command\n");
      safe_free(distanceInfo->mask2);
      safe_free(distanceInfo);
      return -1;
    } else if (mask2tot == 1) {
      safe_free(distanceInfo->mask2);
      distanceInfo->mask2 = NULL;
    } else
      distanceInfo->atom2 = -1;

    if (mask3tot == 0) {
      fprintf(stdout, 
	      "WARNING in ptraj(), distance: No atoms selected in mask3, ignoring command\n");
      safe_free(distanceInfo->mask3);
      safe_free(distanceInfo);
      return -1;
    } else if (mask3tot == 1) {
      safe_free(distanceInfo->mask3);
      distanceInfo->mask3 = NULL;
    } else
      distanceInfo->atom3 = -1;

    if (mask4tot == 0) {
      fprintf(stdout, 
	      "WARNING in ptraj(), distance: No atoms selected in mask4, ignoring command\n");
      safe_free(distanceInfo->mask4);
      safe_free(distanceInfo);
      return -1;
    } else if (mask4tot == 1) {
      safe_free(distanceInfo->mask4);
      distanceInfo->mask4 = NULL;
    } else
      distanceInfo->atom4 = -1;

    action->carg1 = (void *) distanceInfo;

    return 0;
  }


  distanceInfo = (scalarInfo *) action->carg1;


  if (mode == PTRAJ_STATUS) {

    /*
     *  ACTION: PTRAJ_STATUS
     */

    fprintf(stdout, "  DNAIONTRACKER: Data representing the ");
    if (action->iarg3 == 0)
      fprintf(stdout, "count within the cone will be\n");
    else if (action->iarg3 == 1)
      fprintf(stdout, "shortest distance to a phosphate or base centroid will be\n");
    else if (action->iarg3 == 2)
      fprintf(stdout, "count in the top half of the cone (and sort-of bound) will be\n");
    else if (action->iarg3 == 3)
      fprintf(stdout, "count in the bottom half of the cone will be\n");
    fprintf(stdout, "      saved to array named %s\n", distanceInfo->name);
    fprintf(stdout, "      Perpendicular offset for cone is %5.2f angstroms\n", action->darg2);
    if (action->iarg1)
      fprintf(stdout, "      Imaging has been disabled\n");
    if (distanceInfo->atom1 == -1) {
      fprintf(stdout, "      Atom selection 1 is ");
      printAtomMask(stdout, distanceInfo->mask1, action->state);
      fprintf(stdout, "\n");
    } else {
      fprintf(stdout, "      Atom selection 1 is :%i@%s\n",
	      atomToResidue(distanceInfo->atom1+1, action->state->residues, action->state->ipres),
	      action->state->atomName[distanceInfo->atom1]);
    }
    if (distanceInfo->atom2 == -1) {
      fprintf(stdout, "      Atom selection 2 is ");
      printAtomMask(stdout, distanceInfo->mask2, action->state);
      fprintf(stdout, "\n");
    } else {
      fprintf(stdout, "      Atom selection 2 is :%i@%s\n",
	      atomToResidue(distanceInfo->atom2+1, action->state->residues, action->state->ipres),
	      action->state->atomName[distanceInfo->atom2]);
    }
    if (distanceInfo->atom3 == -1) {
      fprintf(stdout, "      Atom selection 3 is ");
      printAtomMask(stdout, distanceInfo->mask3, action->state);
      fprintf(stdout, "\n");
    } else {
      fprintf(stdout, "      Atom selection 3 is :%i@%s\n",
	      atomToResidue(distanceInfo->atom3+1, action->state->residues, action->state->ipres),
	      action->state->atomName[distanceInfo->atom3]);
    }
    if (distanceInfo->atom4 == -1) {
      fprintf(stdout, "      Atom selection 4 is ");
      printAtomMask(stdout, distanceInfo->mask4, action->state);
      fprintf(stdout, "\n");
    } else {
      fprintf(stdout, "      Atom selection 4 is :%i@%s\n",
	      atomToResidue(distanceInfo->atom4+1, action->state->residues, action->state->ipres),
	      action->state->atomName[distanceInfo->atom4]);
    }


    if (distanceInfo->filename != NULL) {
      fprintf(stdout, "      Data will be dumped to a file named %s\n",
	      distanceInfo->filename);
    }

  } else if (mode == PTRAJ_PRINT) {

    /*
     *  ACTION: PTRAJ_PRINT
     */

    if ( ( outFile = fopen(distanceInfo->filename, "w") ) == NULL ) {
      fprintf(stdout, "WARNING in ptraj(), dnaiontracker: couldn't open file %s\n",
	      distanceInfo->filename);
      return 0;
    }
    if (prnlev > 2)
      fprintf(stdout, "PTRAJ DNAIONTRACKER dumping distance %s\n",
	      distanceInfo->name);
    for (i=0; i < action->state->maxFrames; i++) {
      fprintf(outFile, "%10.2f %f\n", (i+1) * action->darg1, distanceInfo->value[i]);
    }
    safe_fclose(outFile);

  } else if (mode == PTRAJ_CLEANUP) {

    /*
     *  ACTION: PTRAJ_CLEANUP
     */

    safe_free(distanceInfo->name);
    safe_free(distanceInfo->filename);
    safe_free(distanceInfo->mask1);
    safe_free(distanceInfo->mask2);
    safe_free(distanceInfo->mask3);
    safe_free(distanceInfo->mask4);
    safe_free(distanceInfo->value);
    INITIALIZE_scalarInfo(distanceInfo);
    safe_free(distanceInfo);

  }



  if (mode != PTRAJ_ACTION) return 0;


  /*
   *  ACTION: PTRAJ_ACTION
   */


  state = (ptrajState *) action->state;

  /*
   *  update local state information
   */
  for (i=0; i<6; i++)
    state->box[i] = box[i];

  if (distanceInfo->totalFrames < 0) {
    distanceInfo->totalFrames = state->maxFrames;
    distanceInfo->value = (double *) 
      safe_malloc(sizeof(double) * distanceInfo->totalFrames);
  }

  if (distanceInfo->frame > distanceInfo->totalFrames) {
    warning("transformDNAiontracker()", "Blowing array; too many frames!!\n");
    return 0;
  }


  /*
   *  setup for imaging if necessary
   */

  if (box[3] <= 0.0 && action->iarg1 == 0) {
    action->iarg1 = 1;
    fprintf(stdout, "  DNAIONTRACKER: box angles are zero, disabling imaging!\n");
  }
  if (action->iarg1 == 0 && (box[3] != 90.0 || box[4] != 90.0 || box[5] != 90.0))
    boxToRecip(box, ucell, recip);

  /*
   *  calculate distances
   */

     /*
      *  P -- P distance (as specified in masks1 and masks2)
      */
  setupDistance(X, Y, Z, x, y, z, state, distanceInfo->mask1, distanceInfo->mask2,
		distanceInfo->atom1, distanceInfo->atom2);

  pp_centroidx = (X[0] + X[1]) / 2.0;
  pp_centroidy = (Y[0] + Y[1]) / 2.0;
  pp_centroidz = (Z[0] + Z[1]) / 2.0;

  d_pp = calculateDistance2(0, 1, X, Y, Z, 
			    box, ucell, recip, 0.0, action->iarg1);
  d_pp = sqrt(d_pp);


     /*
      *  perpendicular offset
      */
  poffset = action->darg2;

     /*
      *  P -- base centroid to median point
      */

  setupDistance(X, Y, Z, x, y, z, state, NULL, distanceInfo->mask3,
		1, distanceInfo->atom3);

  X[0] = pp_centroidx;
  Y[0] = pp_centroidy;
  Z[0] = pp_centroidz;

  d_pbase = calculateDistance2(0, 1, X, Y, Z, 
			       box, ucell, recip, 0.0, action->iarg1);
  d_pbase = sqrt(d_pbase);

  /*
   *  loop over ion positions
   */
  d_min = 9999999999.0;
  if (action->iarg3 == 1)
    distanceInfo->value[distanceInfo->frame] = d_min;

  for (i=0; i < state->atoms; i++) {

    if (distanceInfo->mask4[i] == 1) {

      setupDistance(X, Y, Z, x, y, z, state, NULL, distanceInfo->mask1,
		    i, distanceInfo->atom1);
      d_p1ion = calculateDistance2(0, 1, X, Y, Z, 
				   box, ucell, recip, 0.0, action->iarg1);
      d_p1ion = sqrt(d_p1ion);



      setupDistance(X, Y, Z, x, y, z, state, NULL, distanceInfo->mask2,
		    i, distanceInfo->atom2);
      d_p2ion = calculateDistance2(0, 1, X, Y, Z, 
				   box, ucell, recip, 0.0, action->iarg1);
      d_p2ion = sqrt(d_p2ion);

      setupDistance(X, Y, Z, x, y, z, state, NULL, distanceInfo->mask3,
		    i, distanceInfo->atom3);
      d_baseion = calculateDistance2(0, 1, X, Y, Z, 
				     box, ucell, recip, 0.0, action->iarg1);
      d_baseion = sqrt(d_baseion);

      printf("DEBUG: ion atom %i to P1 is %f\n", i+1, d_p1ion);
      printf("DEBUG: ion atom %i to P2 is %f\n", i+1, d_p2ion);
      printf("DEBUG: ion atom %i to base is %f\n", i+1, d_baseion);

      d_cut = sqrt( (d_pp*d_pp*0.25 + poffset*poffset) );

      printf("DEBUG: d_pp is %f, poffset is %f, d_cut is %f\n", d_pp, poffset, d_cut);

      bound = 0;
      boundLower = 0;
      boundUpper = 0;

      if (d_p1ion < d_cut && d_p2ion < d_cut)
	bound = 1;
      if (d_baseion < d_pbase)
	boundLower = 1;

      if (d_p1ion < d_p2ion)
	d_tmp = d_p1ion;
      else
	d_tmp = d_p2ion;
      if (d_tmp > d_baseion)
	d_tmp = d_baseion;

      if (d_tmp > d_min)
	d_min = d_tmp;

      if (bound && boundLower == 0)
	boundUpper = 1;


      if (action->iarg3 == 0)
	distanceInfo->value[distanceInfo->frame] += bound;
      else if (action->iarg3 == 2)
	distanceInfo->value[distanceInfo->frame] += boundUpper;
      else if (action->iarg3 == 3)
	distanceInfo->value[distanceInfo->frame] += boundLower;
      else if (action->iarg3 == 1)
	if (distanceInfo->value[distanceInfo->frame] > d_min)
	  distanceInfo->value[distanceInfo->frame] = d_min;

    }

  }


  distanceInfo->frame++;

  return 1;
}

/** ACTION ROUTINE *************************************************************
 *
 *  transformRandomizeIons() --- swap positions of ions and solvent randomly
 *
 ******************************************************************************/

   int
transformRandomizeIons(actionInformation *action, 
		       double *x, double *y, double *z, 
		       double *box, int mode)
{
  //char *name = "randomizeions";
  char *buffer;
  double distance, sx, sy, sz;
  int ion, i, j, w;
  int *around;
  int *solvent;
  argStackType **argumentStackPointer;

  double ucell[9], recip[9];

  /*
   *  USAGE:
   *
   *    randomizeions <mask> [around <mask> by <distance>] [overlap <value>] [noimage] [seed <value>]
   *
   *  action argument usage:
   *
   *  mask: the list of ions to be moved.  Each is assumed to be a single atom residue.
   *  iarg1: if 1, disable imaging
   *  iarg2: seed
   *  darg1: the minimum distance between ions (overlap)
   *  darg2: the minimum distance to the around mask
   *  carg1: the around mask (region of space to avoid)
   */

  if (mode == PTRAJ_SETUP) {

    /*
     *  ACTION: PTRAJ_SETUP
     */

#ifdef MPI

#endif

    argumentStackPointer = (argStackType **) action->carg1;
    action->carg1 = NULL;

    if (action->state->solventMolecules == 0) {
      fprintf(stdout, 
	      "WARNING in ptraj(), randomizeions: This command only works if solvent\n");
      fprintf(stdout, "information has been specified.  See the \"solvent\" command.\n");
      fprintf(stdout, "Ignoring this command.\n");
      return -1;
    }

    buffer = getArgumentString(argumentStackPointer, NULL);
    action->mask = processAtomMask(buffer, action->state);
    safe_free(buffer);

    if (action->mask == NULL) {
      fprintf(stdout, "WARNING in ptraj(), randomizeions: NULL mask for the ion specification\n");
      return -1;
    }

    /*
     *  check to see that each ion selected is only a single atom residue!
     */
    for (i=0; i < action->state->atoms; i++) {
      if (action->mask[i]) {

	j = atomToResidue(i,action->state->residues,action->state->ipres);

	if (prnlev > 6) {
	  printf("Atom %i is in residue %i which spans atoms %i to %i\n",
		 i+1, j+1, action->state->ipres[j], action->state->ipres[j+1]);
	}
		 
	if (action->state->ipres[j+1] - action->state->ipres[j] > 1) {
	  fprintf(stdout, 
		  "WARNING IN randomize ions: residue %i appears to contain more than 1 atom!\n",
		  j);
	}
      }
    }



    /*
     *  check the solvent information to make sure that each solvent listed has the
     *  same number of atoms in each molecule; otherwise a uniform trajectory is not
     *  possible and therefore this command will be ignored...
     */

    j = action->state->solventMoleculeStop[0] - action->state->solventMoleculeStart[0];
    for (i=1; i < action->state->solventMolecules; i++) {
      if (j != (action->state->solventMoleculeStop[i] - 
		action->state->solventMoleculeStart[i])) {
	fprintf(stdout, 
		"WARNING in ptraj(), randomizeions: the solvent molecules are not of uniform\n");
	fprintf(stdout, "size hence this command will be ignored.  [Try resetting the solvent\n");
	fprintf(stdout, "information with the \"solvent\" command...\n");
	return -1;
      }
    }

    action->iarg1 = argumentStackContains(argumentStackPointer, "noimage");
    action->iarg2 = argumentStackKeyToInteger(argumentStackPointer, "seed", -1);
    action->darg1 = argumentStackKeyToDouble(argumentStackPointer, "overlap",  3.5);
    action->darg2 = argumentStackKeyToDouble(argumentStackPointer, "by", 3.5);
    action->darg1 = action->darg1 * action->darg1;
    action->darg2 = action->darg2 * action->darg2;

    buffer = argumentStackKeyToString(argumentStackPointer, "around", NULL);
    if (buffer) {
      action->carg1 = processAtomMask(buffer, action->state);
      safe_free(buffer);
    } else
      action->carg1 = NULL;

  } else if (mode == PTRAJ_STATUS) {

    /*
     *  ACTION: PTRAJ_STATUS
     */

    fprintf(stdout, "  RANDOMIZEIONS: swapping the postions of the ions: ");
    printAtomMask(stdout, action->mask, action->state);
    fprintf(stdout, "\n");
    fprintf(stdout, "      with the solvent.  No ions can get closer than %5.2f angstroms to another ion\n",
	    sqrt(action->darg1));
    around = (int *) action->carg1;
    if (around != NULL) {
      fprintf(stdout, "      No ion can get closer than %5.2f angstroms to: ",
	    sqrt(action->darg2));
      printAtomMask(stdout, around, action->state);
      fprintf(stdout, "\n");
    }

    if (action->iarg1) {
      fprintf(stdout, "      Imaging of the coordinates will not be performed\n");
    }
    if (action->iarg2 > 0) {
      fprintf(stdout, "      Random number generator seed is %i\n", action->iarg2);
      srandom((unsigned) action->iarg2);
    }


  } else if (mode == PTRAJ_CLEANUP) {

    action->carg1 = NULL;

  }


  if (mode != PTRAJ_ACTION) return 0;

  /*
   *  ACTION: PTRAJ_ACTION
   */

  if (action->mask == NULL) return 0;


  if (action->iarg1 == 0 && box[3] == 0.0) {
    action->iarg1 = 1;
    fprintf(stdout, "  RANDOMIZEIONS: box angles are zero, disabling imaging!\n");
  }
  if (action->iarg1 == 0 && (box[3] != 90.0 || box[4] != 90.0 || box[5] != 90.0))
    boxToRecip(box, ucell, recip);

  around = (int *) action->carg1;

  solvent = (int *) safe_malloc(sizeof(int) * action->state->solventMolecules);
  memset(solvent, 0, sizeof(int) * action->state->solventMolecules);

  /*
   *  loop over all solvent molecules and mark those that are too close to the solute
   */
  for (j=0; j < action->state->solventMolecules; j++) {

    solvent[j] = 1; 

    /*
     *  is solvent molecule to near any atom in the around mask?
     */
    if (around != NULL) {

      for (i=0; i < action->state->atoms; i++) {
	if ( around[i] ) {
	  if (action->state->solventMoleculeStart[j] != i) {
	    distance = calculateDistance2(action->state->solventMoleculeStart[j], i, x, y, z, 
					  box, (double *) ucell, (double *) recip, 0.0, action->iarg1);
	    if (distance < action->darg2) {
	      solvent[j] = 0;
	      if (prnlev > 6) {
		fprintf(stdout, "  RANDOMIZEIONS: water %i is only %5.2f angstroms from atom %i\n",
			j+1, sqrt(distance), i+1);
		
	      }
	      i = action->state->atoms;
	    }
	  }
	}
      }
    }
  }

  if (prnlev > 4) {
    i = 0;
    if (prnlev > 6)
      fprintf(stdout, "RANDOMIZEIONS: The following waters are ACTIVE so far:\n");
    for (j=0; j < action->state->solventMolecules; j++) {
      if (solvent[j]) {
	i++;
	if (prnlev > 6) {
	  fprintf(stdout, " %5i ", j+1);
	  if (i%10 == 0) printf("\n");
	}
      }
    }
    fprintf(stdout, "  RANDOMIZEIONS: A total of %i waters (out of %i) are active\n", i, action->state->solventMolecules);
  }

  /*
   *  loop over all ions
   */
  for (ion=0; ion < action->state->atoms; ion++) {

    if (action->mask[ion]) {

      if (prnlev > 2) fprintf(stdout, "  RANDOMIZEIONS: Processing ion atom %i\n", ion+1);

	/* 
	 *  is a potential solvent molecule close to any of the ions (except this one)?
	 */
      for (j=0; j < action->state->solventMolecules; j++) {
	if ( solvent[j] ) {

	  /*
	   *  if this solvent is active, check distance to all other ions
	   */
	  for (i=0; i < action->state->atoms; i++) {
	  
	    if (action->mask[i] && ion != i) {
	      distance = calculateDistance2(action->state->solventMoleculeStart[j], i, x, y, z, 
					    box, (double *) ucell, (double *) recip, 0.0, action->iarg1);
	      if (distance < action->darg1) {
		i = action->state->atoms;
		solvent[j] = 0;
		if (prnlev > 6) {
		  fprintf(stdout, "  RANDOMIZEIONS: water %i is only %5.2f angstroms from (ion) atom %i\n",
			  j+1, sqrt(distance), i+1);
		}
	      }
	    }
	  }
	}
      }

      i = 1;
      while (i > 0 && i < 10000) {
	/*
	 *  Run the random number generator so that the same number is not produced
	 *  when the seed was set manually
	 */
#ifdef MPI	
	for (j = 0; j < worldsize; j++) {
	  if (j == worldrank)
	    w = random() % action->state->solventMolecules;
	  else
	    random();
	}
#else
	w = random() % action->state->solventMolecules;
#endif
	if (solvent[w] == 1) {
	  i = -1;
	} else {
	  i++;
	}
      }

      if (i > 0) {
	fprintf(stdout, "  RANDOMIZEIONS: warning tried 10000 random waters and couldn't meet criteria!  Skipping\n");
      }

      if (i < 0) {
	if (prnlev > 2) {
	  fprintf(stdout, "  RANDOMIZEIONS: Swaping solvent %i for ion %i\n",
		  w+1, ion+1);
	}


	i = action->state->solventMoleculeStart[w];
	sx = x[ion] - x[i];
	sy = y[ion] - y[i];
	sz = z[ion] - z[i];
	
	for (i = action->state->solventMoleculeStart[w]; i < action->state->solventMoleculeStop[w]; i++) {

	  x[i] += sx;
	  y[i] += sy;
	  z[i] += sz;

	}
	x[ion] -= sx;
	y[ion] -= sy;
	z[ion] -= sz;

      }
    }
  }
  safe_free(solvent);
  return 1;
}

/** ACTION ROUTINE *************************************************************
 *
 *  transformScale() --- Scale the coordinates by a specified amount
 *
 ******************************************************************************/


   int
transformScale(actionInformation *action, 
		   double *x, double *y, double *z,
		   double *box, int mode)
{
  //char *name = "scale";
  argStackType **argumentStackPointer;
  char *buffer;
  int i;

  /*
   *  USAGE:
   *
   *    scale [x <scalex>] [y <scaley>] [z <scalez>] [mask]
   *
   *  action argument usage:
   *
   *  mask : atom selection representing atoms to shift
   *  darg1: scalex
   *  darg2: scaley
   *  darg3: scalez 
   */

  if (mode == PTRAJ_SETUP) {
    /*
     *  ACTION: PTRAJ_SETUP
     */

#ifdef MPI
    printParallelError(name);
    return -1;
#endif

    argumentStackPointer = (argStackType **) action->carg1;
    action->carg1 = NULL;

    action->darg1 = argumentStackKeyToDouble(argumentStackPointer, "x", 0.0);
    action->darg2 = argumentStackKeyToDouble(argumentStackPointer, "y", 0.0);
    action->darg3 = argumentStackKeyToDouble(argumentStackPointer, "z", 0.0);

    buffer = safe_malloc(sizeof(char) * BUFFER_SIZE);
    buffer = getArgumentString(argumentStackPointer, NULL);
    if (buffer == NULL) {
      action->mask = NULL;
    } else
      action->mask = processAtomMask(buffer, action->state);
    safe_free(buffer);

  } else if (mode == PTRAJ_STATUS) {

    /*
     *  ACTION: PTRAJ_STATUS
     */

    fprintf(stdout, "  SCALE coordinates: ");
    if (action->darg1 != 0.0) fprintf(stdout, "X by %.3f ", action->darg1);
    if (action->darg2 != 0.0) fprintf(stdout, "Y by %.3f ", action->darg2);
    if (action->darg3 != 0.0) fprintf(stdout, "Z by %.3f ", action->darg3);
    if (action->mask != NULL) {
      fprintf(stdout, " mask is ");
      printAtomMask(stdout, action->mask, action->state);
      fprintf(stdout, "\n");
    }
    if (action->mask == NULL) fprintf(stdout, "\n");

  }

  if (mode != PTRAJ_ACTION) return 0;

  /*
   *  ACTION: PTRAJ_ACTION
   */

  for (i=0; i < action->state->atoms; i++) 
    if (action->mask == NULL || action->mask[i]) {
      x[i] *= action->darg1;
      y[i] *= action->darg2;
      z[i] *= action->darg3;
    }

  return 1;

}

/** ACTION ROUTINE *************************************************************
 *
 *  transformTruncOct() --- trim/orient a box to make it a truncated octahedron
 *
 ******************************************************************************/
/* DISABLED FOR CPPTRAJ - relies on too much ingrained parm data in ptraj

   int
transformTruncOct(actionInformation *action, 
		  double *x, double *y, double *z,
		  double *box, int mode)
{
  char *name = "truncoct";
  argStackType **argumentStackPointer;
  char *buffer;
  ptrajState *state;
  int ii, i, j;
  int *mask;
  double cx, cy, cz;
  double total_mass, max_dist, dist;
  double sideDist,sideDist0,diagDist,diagDist2,diagCoord;
  double toDist, pdist,dnormCoord;
  int *zapMask, zap, zapTotal;
  double phi,cos1,sin1,cos2,sin2,tetra_angl;
  double t11,t12,t13,t21,t22,t23,t31,t32,t33,xx,yy;
  double ucell1[3];
  double ucell2[3];
  double ucell3[3];
  double gamma;
  int new_waters;
  //Parm *newparm, *tmpparm;
  FILE *fpout;


  //  USAGE:
  //
  //    truncoct <mask> <distance> prmtop <filename>
  //
  //  action argument usage:
  //
  //  mask: atom selection for solute
  //  iarg1: the index of the first solvent molecule
  //  darg1: the size of the truncated octahedron(?)

  if (mode == PTRAJ_SETUP) {
    //  ACTION: PTRAJ_SETUP

#ifdef MPI
    printParallelError(name);
    return -1;
#endif

    argumentStackPointer = (argStackType **) action->carg1;
    action->carg1 = NULL;

    buffer = getArgumentString(argumentStackPointer, NULL);
    if (buffer == NULL) {
      fprintf(stdout, "WARNING in ptraj(), truncoct: No atom mask for the solute was\n");
      fprintf(stdout, "specified...  Ignoring command.\n");
      return -1;
    }
    action->mask = processAtomMask(buffer, action->state);
    safe_free(buffer);

    action->darg1 = getArgumentDouble(argumentStackPointer, -1.0);
    if (action->darg1 < 0) {
      fprintf(stdout, "WARNING in ptraj(), truncoct: The buffer distance specified is\n");
      fprintf(stdout, "out of range or was not specified.  Ignoring command\n");
      return -1;
    } 

    if (parm == NULL) {
      fprintf(stdout, "WARNING in ptraj(), truncoct: No AMBER prmtop file is present\n");
      fprintf(stdout, "This command only works with AMBER prmtop files, hence ignoring\n");
      fprintf(stdout, "command...\n");
      return -1;
    }

    if (action->state->solventMolecules == 0) {
      fprintf(stdout, "WARNING in ptraj(), truncoct: No solvent information has been\n");
      fprintf(stdout, "specified.  See the \"solvent\" command.  Ignoring...\n");
      return -1;
    }

    buffer = argumentStackKeyToString(argumentStackPointer, "prmtop", NULL);
    action->carg1 = (void *) buffer;


  } else if (mode == PTRAJ_STATUS) {

    //  ACTION: PTRAJ_STATUS

    fprintf(stdout, 
	    "  TRUNCATED OCTAHEDRON: will be created with minimum distance from solute\n");
    fprintf(stdout, "      to the sides of the truncated octahedron of %.3f angstroms\n",
	    action->darg1);
    buffer = (char *) action->carg1;
    if (buffer != NULL) {
      fprintf(stdout, "      Creating a prmtop named: %s\n", buffer);
    }

    fprintf(stdout, "      The solute mask is ");
    printAtomMask(stdout, action->mask, action->state);
    fprintf(stdout, "\n");

  }

  if (mode != PTRAJ_ACTION) return 0;

  //  ACTION: PTRAJ_ACTION

  state = (ptrajState *) action->state;

  //  update local state information
  for (i=0; i<6; i++)
    state->box[i] = box[i];

  //  FIRST CENTER of geometry of the solute at origin
  //  
  //  accumulate center of geometry...

  mask = action->mask;
  cx = 0.0;
  cy = 0.0;
  cz = 0.0;
  
  total_mass=0.;
  printf("\n***********************************************************\n");
  printf(  "*********  Truncated Octahedral Data                *******\n");
  printf(  "*********                                           *******\n");
  printf(  "***********************************************************\n");
  for (i=0; i < state->atoms; i++) {
      if (mask[i]) {
	  cx += x[i];
	  cy += y[i];
	  cz += z[i];
	  total_mass += 1.0;
      }
  }

  cx /= total_mass;
  cy /= total_mass;
  cz /= total_mass;
  max_dist=0.;
  printf("Center of geometry Offset     %lf %lf %lf\n",cx,cy,cz);
  for (i=0; i < state->atoms; i++) {
      x[i] -= cx;
      y[i] -= cy;
      z[i] -= cz;
      if (mask[i]) {
	  dist=x[i]*x[i]+y[i]*y[i]+z[i]*z[i];
	  if(dist>max_dist) max_dist=dist;	  
      }
  }
  max_dist=sqrt(max_dist);
  printf("max radius 0f solute is %lf\n",max_dist);

//     calculate the face distances
  toDist=action->darg1;
// printf("\n\nInside TruncOct toDist is %lf\n\n",toDist);
  diagDist=toDist+max_dist-0.5;
  diagCoord=diagDist/sqrt(3);
  dnormCoord=1./sqrt(3.);
  sideDist=diagCoord* 2.;
  sideDist0=diagCoord* 2.-0.5;
  if(sideDist > state->box[0]*0.5 || 
     sideDist > state->box[1]*0.5 ||
     sideDist > state->box[2]*0.5){
      printf("\nWARNING WARNING WARNING in truncoct: ");
      printf("Original box MAY not be big enough\n");
      printf("           ...... Continuing anyway ......\n\n");
  }
  printf("   TO cubic faces have dist %f while \n    orig. box sizes are %f %f %f\n\n",
	 2.*sideDist,state->box[0],state->box[1],state->box[2]);
// printf("Inside TruncOct side and diag are %lf %lf\n\n",sideDist,diagDist);

//    start removing solvent
//
//    NOTE: now we only keep track of solvent molecules we want to remove!

  zapTotal = 0;
  zapMask = (int *) safe_malloc(sizeof(int) * state->atoms);
  for (i=0; i < state->atoms; i++)
    zapMask[i] = 0;

  for(i=0; i < state->solventMolecules; i++) {

    zap = 0;
    for(ii=state->solventMoleculeStart[i]; ii < state->solventMoleculeStop[i]; ii++) {
      if(ABS(x[ii])>sideDist0 || ABS(y[ii])>sideDist0 || ABS(z[ii])>sideDist0) {
	zap=1;
      } else {
	pdist=(ABS(x[ii])+ABS(y[ii])+ABS(z[ii])-3.*diagCoord)*dnormCoord;
	if(pdist>0){
	  zap=1;
	}
      }
    }
    if (zap == 1) {
      for(ii=state->solventMoleculeStart[i]; ii < state->solventMoleculeStop[i]; ii++) {
	zapMask[ii] = 1;
      }
      zapTotal++;
    }
  }


  //  modify coordinates

  j = 0;
  for (i=0; i < state->atoms; i++) {
    if (zapMask[i] == 0) {
      x[j] = x[i];
      y[j] = y[i];
      z[j] = z[i];
      j++;
    }
  }

  //  modify the current state
  action->state = NULL;

  if (prnlev > 2) {
    printf("ZAP MASK IS: ");
    printAtomMask(stdout, zapMask, state);
    fprintf(stdout, "\n");
  }
  modifyStateByMask(&action->state, &state, zapMask, 1);
    
  safe_free(zapMask);
  zapMask = NULL;

  ptrajClearState(&state);
  state = action->state;

  new_waters=state->solventMolecules;
  printf("Number of waters in TO %d\n",new_waters);

  // *******************************************************
  //       Now Rotate the whole thing to line up the axes *
  // *******************************************************
  tetra_angl=2*acos(1./sqrt(3.));
  phi=PI/4.;
  cos1=cos(phi);
  sin1=sin(phi);
  phi=PI/2.-tetra_angl/2.;
  cos2=sqrt(2.)/sqrt(3.);
  sin2=1./sqrt(3.);
  
  // *****************************************************   
  //       45 around z axis, (90-tetra/2) around y axis,   
  //       90 around x axis                                 
  //                                                        
  //   (1  0  0)    (cos2  0 -sin2)    (cos1 -sin1  0)      
  //   (0  0 -1)    (   0  1     0)    (sin1  cos1  0)      
  //   (0  1  0)    (sin2  0  cos2)    (   0     0  1)      
  //                                                        
  //   cntr-clk       clock              clock              
  //   Looking down + axis of rotation toward origin        
  // *****************************************************   

  t11= cos2*cos1;
  t12=-cos2*sin1;
  t13=-sin2;
  t21=-sin2*cos1;
  t22= sin2*sin1;
  t23=-cos2;
  t31= sin1;
  t32= cos1;
  t33=0;

  for (i=0; i < state->atoms; i++) {
      xx = t11*x[i]+t12*y[i]+t13*z[i];
      yy = t21*x[i]+t22*y[i]+t23*z[i];
      z[i] = t31*x[i]+t32*y[i]+t33*z[i];
      x[i]=xx;
      y[i]=yy;
  }
  diagDist2=2.*diagDist;
  gamma=tetra_angl;
  ucell1[0] = diagDist2;
  ucell1[1] = 0.;
  ucell1[2] = 0.;
  ucell2[0] = diagDist2*cos(gamma);
  ucell2[1] = diagDist2*sin(gamma);
  ucell2[2] = 0.;
  ucell3[0] = diagDist2*cos(gamma);
  ucell3[1] = (diagDist2*diagDist2*cos(gamma)-
		 ucell3[0]*ucell2[0])/ucell2[1];
  ucell3[2] = sqrt( diagDist2*diagDist2 - ucell3[0]*ucell3[0] - 
		      ucell3[1]*ucell3[1] );

  if (prnlev > 2) {
    printf("TRUNCATED OCTAHEDRON GENERATION:\n");
    printf("UCELL %f %f %f \n",ucell1[0],ucell1[1],ucell1[2]);
    printf("UCELL %f %f %f \n",ucell2[0],ucell2[1],ucell2[2]);
    printf("UCELL %f %f %f \n",ucell3[0],ucell3[1],ucell3[2]);
  }
  printf("UCELL length for mdin file is %f, padded by 1.0 angstrom\n",ucell1[0]);

  state->box[0]=ucell1[0] + 1.0;
  state->box[1]=ucell1[0] + 1.0;
  state->box[2]=ucell1[0] + 1.0;
  state->box[3]=tetra_angl*RADDEG;
  state->box[4]=state->box[3];
  state->box[5]=state->box[3];
  for (i=0; i < 6; i++)
    box[i] = state->box[i];

  //  Dump out a new prmtop file if requested.
  buffer = (char *) action->carg1;
  action->carg1 = NULL;

  if (buffer) {
    fprintf(stdout,"Warning: truncoct Parm write disabled for Cpptraj.\n");
//
//  if ( (fpout=safe_fopen(buffer,"w")) == NULL ) {
//    fprintf(stdout, "WARNING in ptraj(), truncoct: Couldn't open prmtop file %s\n",
//            buffer);
//    return 1;
//  }

//  tmpparm = parm;
//  if (new_waters == 0) {
//    fprintf(stdout, "WARNING in ptraj(), truncoct: No waters were removed...\n");
//  } else {
//    newparm = modifyTIP3P(new_waters);
//    parm = newparm;
//  }
//  parm->IFBOX=2;
//  parm->box->beta  = state->box[4];
//  parm->box->box[0]= state->box[0];
//  parm->box->box[1]= state->box[0];
//  parm->box->box[2]= state->box[0];
//  writeParm( fpout, 1 ); 
//  parm = tmpparm;
//  safe_fclose(fpout);
//  safe_free(buffer);
  }  
  return 1;
}
*/


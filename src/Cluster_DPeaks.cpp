#include <cmath> // fabs
#include <algorithm> // sort
#include "Cluster_DPeaks.h"
#include "CpptrajStdio.h"
#include "DataSet_Mesh.h"

Cluster_DPeaks::Cluster_DPeaks() : epsilon_(-1.0) {}

void Cluster_DPeaks::Help() {
  mprintf("\t[dpeaks epsilon <e>]\n");
}

int Cluster_DPeaks::SetupCluster(ArgList& analyzeArgs) {
  epsilon_ = analyzeArgs.getKeyDouble("epsilon", -1.0);
  if (epsilon_ <= 0.0) {
    mprinterr("Error: DPeaks requires epsilon to be set and > 0.0\n"
              "Error: Use 'epsilon <e>'\n");
    return 1;
  }
  return 0;
}

void Cluster_DPeaks::ClusteringInfo() {
  mprintf("\tDPeaks: Cutoff (epsilon) for determining local density is %g\n", epsilon_);
}

int Cluster_DPeaks::Cluster() {
  Points_.clear();
  // First determine which frames are being clustered.
  for (int frame = 0; frame < (int)FrameDistances_.Nframes(); ++frame)
    if (!FrameDistances_.IgnoringRow( frame ))
      Points_.push_back( Cpoint(frame) );
  // Sanity check.
  if (Points_.size() < 2) {
    mprinterr("Error: Only 1 frame in initial clustering.\n");
    return 1;
  }
  // For each point, determine how many others are within epsilon
  for (Carray::iterator point0 = Points_.begin();
                        point0 != Points_.end(); ++point0)
  {
    int density = 0;
    for (Carray::const_iterator point1 = Points_.begin();
                                point1 != Points_.end(); ++point1)
    {
      if (point0 != point1) {
        if ( FrameDistances_.GetFdist(point0->Fnum(), point1->Fnum()) < epsilon_ )
          density++;
      }
    }
    point0->SetDensity( density );
  }
  // Sort by density here. Otherwise array indices will be invalid later.
  std::sort( Points_.begin(), Points_.end() );
  // For each point, find the closest point that has higher density.
  for (unsigned int idx0 = 0; idx0 != Points_.size(); idx0++)
  {
    double min_dist = -1.0;
    double max_dist = -1.0;
    int nearestIdx = -1; // Index of nearest neighbor with higher density
    Cpoint& point0 = Points_[idx0];
    mprintf("\nDBG:\tSearching for nearest neighbor to idx %u with higher density than %i.\n",
            idx0, point0.Density());
    for (unsigned int idx1 = 0; idx1 != Points_.size(); idx1++)
    {
      if (idx0 != idx1) {
        Cpoint const& point1 = Points_[idx1];
        double dist1_2 = FrameDistances_.GetFdist(point0.Fnum(), point1.Fnum());
        max_dist = std::max(max_dist, dist1_2); 
        if (point1.Density() > point0.Density())
        {
          if (min_dist < 0.0) {
            min_dist = dist1_2;
            nearestIdx = (int)idx1;
            mprintf("DBG:\t\tNeighbor idx %i is first point (density %i), distance %g\n",
                    nearestIdx, point1.Density(), min_dist);
          } else if (dist1_2 < min_dist) {
            min_dist = dist1_2;
            nearestIdx = (int)idx1;
            mprintf("DBG:\t\tNeighbor idx %i is closer (density %i, distance %g)\n",
                    nearestIdx, point1.Density(), min_dist);
          }
        }
      }
    }
    // If min_dist is -1 at this point there is no point with higher density
    // i.e. this point has the highest density. Assign it the maximum observed
    // distance.
    mprintf("DBG:\tClosest point to %u with higher density is %i (distance %g)\n",
            idx0, nearestIdx, min_dist);
    if (min_dist < 0.0)
      point0.SetDist( max_dist );
    else
      point0.SetDist( min_dist );
    point0.SetNearestIdx( nearestIdx );
  }
  // DEBUG - Plot density vs distance for each point.
  CpptrajFile output;
  output.OpenWrite("dpeaks.dat");
  output.Printf("%-10s %10s %s %10s %10s\n", "#Density", "Distance", "Frame", "Idx", "Neighbor");
  for (Carray::const_iterator point = Points_.begin();
                              point != Points_.end(); ++point)
    output.Printf("%-10i %10g \"%i\" %10u %10i\n", point->Density(), point->Dist(),
                  point->Fnum()+1, point-Points_.begin(), point->NearestIdx());
  output.CloseFile();
  // Choose points for which the min distance to point with higher density is
  // anomalously high.
  // Currently doing this by calculating the running average of density vs 
  // distance, then choosing points with distance > twice the SD of the 
  // running average.
  // NOTE: Store in a mesh data set for now in case we want to spline etc later.
  unsigned int avg_factor = 10;
  unsigned int window_size = Points_.size() / avg_factor;
  mprintf("DBG:\tRunning avg window size is %u\n", window_size);
  // FIXME: Handle case where window_size < frames
  DataSet_Mesh runavg;
  unsigned int ra_size = Points_.size() - window_size + 1;
  runavg.Allocate1D( ra_size );
  mprintf("DBG:\tRunning avg set should be size %u\n", ra_size);
  CpptrajFile raOut;
  raOut.OpenWrite("runavg.dpeaks.dat");
  double dwindow = (double)window_size;
  double sumx = 0.0;
  double sumy = 0.0;
  for (unsigned int i = 0; i < window_size; i++) {
    sumx += (double)Points_[i].Density();
    sumy += Points_[i].Dist();
  }
  double avgy = sumy / dwindow;
  runavg.AddXY( sumx / dwindow, avgy );
  raOut.Printf("%g %g\n", sumx / dwindow, avgy );
  for (unsigned int i = 1; i < ra_size; i++) {
    unsigned int nextwin = i + window_size - 1;
    unsigned int prevwin = i - 1;
    sumx = (double)Points_[nextwin].Density() - (double)Points_[prevwin].Density() + sumx;
    sumy =         Points_[nextwin].Dist()    -         Points_[prevwin].Dist()    + sumy;
    avgy = sumy / dwindow;
    runavg.AddXY( sumx / dwindow, avgy );
    raOut.Printf("%g %g\n", sumx / dwindow, avgy );
  }
  raOut.CloseFile();
  mprintf("DBG:\tRunning avg set is size %zu\n", runavg.Size());
  double ra_sd;
  double ra_avg = runavg.Avg( ra_sd );
  // Double stdev
  ra_sd *= 2.0;
  mprintf("DBG:\tAvg of running avg set is %g, sd*2.0 is %g\n", ra_avg, ra_sd);
  // For each point, what is the closest running avgd point?
  CpptrajFile raDelta;
  raDelta.OpenWrite("radelta.dat");
  raDelta.Printf("%-10s %10s %10s\n", "#Frame", "RnAvgPos", "Delta");
  unsigned int ra_position = 0;
  unsigned int ra_end = Points_.size() - 1;
  int cnum = 0;
  for (Carray::iterator point = Points_.begin();
                        point != Points_.end(); ++point)
  {
    if (ra_position != ra_end) {
      // Is the next running avgd point closer to this point?
      while (ra_position != ra_end) {
        double dens  = (double)point->Density();
        double diff0 = fabs( dens - runavg.X(ra_position  ) );
        double diff1 = fabs( dens - runavg.X(ra_position+1) );
        if (diff1 < diff0)
          ++ra_position; // Next running avg position is closer for this point.
        else
          break; // This position is closer.
      }
    }
    double delta = point->Dist() - runavg.Y(ra_position);
    raDelta.Printf("%-10i %10u %10g", point->Fnum()+1, ra_position, delta);
    if (delta > ra_sd) {
      raDelta.Printf(" POTENTIAL CLUSTER %i", cnum);
      point->SetCluster(cnum++);
    }
    raDelta.Printf("\n");
  }
  raDelta.CloseFile();
  int nclusters = cnum;
  mprintf("%i clusters.\n", nclusters);
  // Each remaining point is assigned to the same cluster as its nearest
  // neighbor of higher density. Do this recursively until a cluster
  // center is found.
  cnum = -1;
  for (unsigned int idx = 0; idx != Points_.size(); idx++) {
    if (Points_[idx].Cnum() == -1) {// Point is unassigned.
      AssignClusterNum(idx, cnum);
      mprintf("Finished recursion for index %i\n\n", idx);
    }
  }
  // Add the clusters.
  std::vector<ClusterDist::Cframes> TempClusters( nclusters );
  for (Carray::const_iterator point = Points_.begin(); point != Points_.end(); ++point)
    TempClusters[point->Cnum()].push_back( point->Fnum() );
  for (std::vector<ClusterDist::Cframes>::const_iterator clust = TempClusters.begin();
                                                         clust != TempClusters.end();
                                                       ++clust)
    AddCluster( *clust );
  // Calculate the distances between each cluster based on centroids
  CalcClusterDistances();
  return 0;
}

/** This should never be called for the point with highest density
  * which by definition should be a cluster center.
  */
void Cluster_DPeaks::AssignClusterNum(int idx, int& cnum) {
  // Who is the nearest neighbor with higher density. 
  int neighbor_idx = Points_[idx].NearestIdx();
  mprintf("Index %i nearest neighbor index %i\n", idx, neighbor_idx);
  // SANITY CHECK
  if (neighbor_idx == -1) {
    mprinterr("Internal Error: In Cluster_DPeaks::AssignClusterNum nearest neighbor is -1.\n");
    return;
  }
  if (Points_[neighbor_idx].Cnum() != -1) {
    // Nearest neighbor has a cluster num assigned.
    cnum = Points_[neighbor_idx].Cnum();
    mprintf("Neighbor index %i is cluster %i\n", neighbor_idx, cnum);
  } else
    // Ask neighbor to find cluster num.
    AssignClusterNum(neighbor_idx, cnum);
  mprintf("Index %i cnum %i\n", idx, cnum);
  // At this point cnum should be set. One more sanity check.
  if (cnum == -1) {
    mprinterr("Internal Error: In Cluster_DPeaks::AssignClusterNum could not get"
              " cluster num for index %u.\n", idx);
    return;
  }
  Points_[idx].SetCluster( cnum );
}

void Cluster_DPeaks::ClusterResults(CpptrajFile& outfile) const {
   outfile.Printf("#Algorithm: DPeaks epsilon %g\n", epsilon_);
}

void Cluster_DPeaks::AddSievedFrames() {
  mprintf("FIXME: Adding sieved frames not yet supported.\n");
}

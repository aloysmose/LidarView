//=========================================================================
//
// Copyright 2018 Kitware, Inc.
// Author: Guilbert Pierre (spguilbert@gmail.com)
// Data: 03-27-2018
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//=========================================================================

// This slam algorithm is inspired by the LOAM algorithm:
// J. Zhang and S. Singh. LOAM: Lidar Odometry and Mapping in Real-time.
// Robotics: Science and Systems Conference (RSS). Berkeley, CA, July 2014.

// The algorithm is composed of three sequential steps:
//
// - Keypoints extraction: this step consists of extracting keypoints over
// the points clouds. To do that, the laser lines / scans are trated indepently.
// The laser lines are projected onto the XY plane and are rescale depending on
// their vertical angle. Then we compute their curvature and create two class of
// keypoints. The edges keypoints which correspond to points with a hight curvature
// and planar points which correspond to points with a low curvature.
//
// - Ego-Motion: this step consists of recovering the motion of the lidar
// sensor between two frames (two sweeps). The motion is modelized by a constant
// velocity and angular velocity between two frames (i.e null acceleration). 
// Hence, we can parameterize the motion by a rotation and translation per sweep / frame
// and interpolate the transformation inside a frame using the timestamp of the points.
// Since the points clouds generated by a lidar are sparses we can't design a
// pairwise match between keypoints of two successive frames. Hence, we decided to use
// a closest-point matching between the keypoints of the current frame
// and the geometrics features derived from the keypoints of the previous frame.
// The geometrics features are lines or planes and are computed using the edges keypoints
// and planar keypoints of the previous frame. Once the matching is done, a keypoint
// of the current frame is matched with a plane / line (depending of the
// nature of the keypoint) from the previous frame. Then, we recover R and T by
// minimizing the function f(R, T) = sum(d(point, line)^2) + sum(d(point, plane)^2).
// Which can be writen f(R, T) = sum((R*X+T-P).t*A*(R*X+T-P)) where:
// - X is a keypoint of the current frame
// - P is a point of the corresponding line / plane
// - A = (n*n.t) with n being the normal of the plane
// - A = (I - n*n.t).t * (I - n*n.t) with n being a director vector of the line
// Since the function f(R, T) is a non-linear mean square error function
// we decided to use the Levenberg-Marquardt algorithm to recover its argmin.
//
// - Mapping: This step consists of refining the motion recovered in the Ego-Motion
// step and to add the new frame in the environment map. Thanks to the ego-motion
// recovered at the previous step it is now possible to estimate the new position of
// the sensor in the map. We use this estimation as an initial point (R0, T0) and we
// perform an optimization again using the keypoints of the current frame and the matched
// keypoints of the map (and not only the previous frame this time!). Once the position in the
// map has been refined from the first estimation it is then possible to update the map by
// adding the keypoints of the current frame into the map.
//
// In the following programs : "vtkSlam.h" and "vtkSlam.cxx" the lidar
// coordinate system {L} is a 3D coordinate system with its origin at the
// geometric center of the lidar. The world coordinate system {W} is a 3D
// coordinate system which coinciding with {L] at the initial position. The
// points will be denoted by the ending letter L or W if they belong to
// the corresponding coordinate system

#ifndef VTK_SLAM_H
#define VTK_SLAM_H

// LOCAL
#include "vtkPCLConversions.h"
// STD
#include <string>
#include <ctime>
// VTK
#include <vtkPolyDataAlgorithm.h>
#include <vtkSmartPointer.h>
#include <vtkNew.h>
// EIGEN
#include <Eigen/Dense>
// PCL
#include <pcl/point_types.h>
#include <pcl/filters/voxel_grid.h>
#include <pcl/kdtree/kdtree_flann.h>

#include "KalmanFilter.h"

class vtkVelodyneTransformInterpolator;
class RollingGrid;
typedef pcl::PointXYZINormal Point;

class VTK_EXPORT vtkSlam : public vtkPolyDataAlgorithm
{
public:
  // vtkPolyDataAlgorithm functions
  static vtkSlam *New();
  vtkTypeMacro(vtkSlam, vtkPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent);

  // Add a new frame to process to the slam algorithm
  // From this frame; keypoints will be computed and extracted
  // in order to recover the ego-motion of the lidar sensor
  // and to update the map using keypoints and ego-motion
  void AddFrame(vtkPolyData* newFrame);

  // Get the computed world transform so far
  void GetWorldTransform(double* Tworld);

  // Get/Set General
  vtkGetMacro(DisplayMode, bool)
  vtkSetMacro(DisplayMode, bool)

  vtkGetMacro(MaxDistBetweenTwoFrames, double)
  vtkSetMacro(MaxDistBetweenTwoFrames, double)

  vtkGetMacro(AngleResolution, double)
  vtkSetMacro(AngleResolution, double)

  vtkGetMacro(MaxDistanceForICPMatching, double)
  vtkSetMacro(MaxDistanceForICPMatching, double)

  vtkGetMacro(FastSlam, bool)
  vtkSetMacro(FastSlam, bool)

  void SetUndistortion(bool input);
  vtkGetMacro(Undistortion, bool)

  // set LeafSize
  void SetLeafSize(double argInput);

  // Get/Set RollingGrid
  unsigned int Get_RollingGrid_VoxelSize() const;
  void Set_RollingGrid_VoxelSize(const unsigned int size);

  void Get_RollingGrid_Grid_NbVoxel(double nbVoxel[3]) const;
  void Set_RollingGrid_Grid_NbVoxel(const double nbVoxel[3]);

  void Get_RollingGrid_PointCloud_NbVoxel(double nbVoxel[3]) const;
  void Set_RollingGrid_PointCloud_NbVoxel(const double nbVoxel[3]);

  double Get_RollingGrid_LeafVoxelFilterSize() const;
  void Set_RollingGrid_LeafVoxelFilterSize(const double size);

  // Get/Set Keypoint
  vtkGetMacro(MaxEdgePerScanLine, unsigned int)
  vtkSetMacro(MaxEdgePerScanLine, unsigned int)

  vtkGetMacro(MaxPlanarsPerScanLine, unsigned int)
  vtkSetMacro(MaxPlanarsPerScanLine, unsigned int)

  vtkGetMacro(MinDistanceToSensor, double)
  vtkSetMacro(MinDistanceToSensor, double)

  vtkGetMacro(EdgeSinAngleThreshold, double)
  vtkSetMacro(EdgeSinAngleThreshold, double)

  vtkGetMacro(PlaneSinAngleThreshold, double)
  vtkSetMacro(PlaneSinAngleThreshold, double)

  vtkGetMacro(EdgeDepthGapThreshold, double)
  vtkSetMacro(EdgeDepthGapThreshold, double)

  // Get/Set EgoMotion
  vtkGetMacro(EgoMotionLMMaxIter, unsigned int)
  vtkSetMacro(EgoMotionLMMaxIter, unsigned int)

  vtkGetMacro(EgoMotionICPMaxIter, unsigned int)
  vtkSetMacro(EgoMotionICPMaxIter, unsigned int)

  vtkGetMacro(EgoMotionLineDistanceNbrNeighbors, unsigned int)
  vtkSetMacro(EgoMotionLineDistanceNbrNeighbors, unsigned int)

  vtkGetMacro(EgoMotionMinimumLineNeighborRejection, unsigned int)
  vtkSetMacro(EgoMotionMinimumLineNeighborRejection, unsigned int)

  vtkGetMacro(EgoMotionLineDistancefactor, double)
  vtkSetMacro(EgoMotionLineDistancefactor, double)

  vtkGetMacro(EgoMotionPlaneDistanceNbrNeighbors, unsigned int)
  vtkSetMacro(EgoMotionPlaneDistanceNbrNeighbors, unsigned int)

  vtkGetMacro(EgoMotionPlaneDistancefactor1, double)
  vtkSetMacro(EgoMotionPlaneDistancefactor1, double)

  vtkGetMacro(EgoMotionPlaneDistancefactor2, double)
  vtkSetMacro(EgoMotionPlaneDistancefactor2, double)

  vtkGetMacro(EgoMotionMaxLineDistance, double)
  vtkSetMacro(EgoMotionMaxLineDistance, double)

  vtkGetMacro(EgoMotionMaxPlaneDistance, double)
  vtkSetMacro(EgoMotionMaxPlaneDistance, double)

  // Get/Set Mapping
  vtkGetMacro(MappingLMMaxIter, unsigned int)
  vtkSetMacro(MappingLMMaxIter, unsigned int)

  vtkGetMacro(MappingICPMaxIter, unsigned int)
  vtkSetMacro(MappingICPMaxIter, unsigned int)

  vtkGetMacro(MappingLineDistanceNbrNeighbors, unsigned int)
  vtkSetMacro(MappingLineDistanceNbrNeighbors, unsigned int)

  vtkGetMacro(MappingMinimumLineNeighborRejection, unsigned int)
  vtkSetMacro(MappingMinimumLineNeighborRejection, unsigned int)

  vtkGetMacro(MappingLineDistancefactor, double)
  vtkSetMacro(MappingLineDistancefactor, double)

  vtkGetMacro(MappingPlaneDistanceNbrNeighbors, unsigned int)
  vtkSetMacro(MappingPlaneDistanceNbrNeighbors, unsigned int)

  vtkGetMacro(MappingPlaneDistancefactor1, double)
  vtkSetMacro(MappingPlaneDistancefactor1, double)

  vtkGetMacro(MappingPlaneDistancefactor2, double)
  vtkSetMacro(MappingPlaneDistancefactor2, double)

  vtkGetMacro(MappingMaxLineDistance, double)
  vtkSetMacro(MappingMaxLineDistance, double)

  vtkGetMacro(MappingMaxPlaneDistance, double)
  vtkSetMacro(MappingMaxPlaneDistance, double)

  vtkGetMacro(MappingLineMaxDistInlier, double)
  vtkSetMacro(MappingLineMaxDistInlier, double)

protected:
  // vtkPolyDataAlgorithm functions
  vtkSlam();
  ~vtkSlam();
  virtual int RequestData(vtkInformation *, vtkInformationVector **, vtkInformationVector *);
private:
  vtkSlam(const vtkSlam&);
  void operator = (const vtkSlam&);
  // Polydata which represents the trajectory computed
  vtkSmartPointer<vtkPolyData> Trajectory;
  vtkSmartPointer<vtkPolyData> Orientation;
  vtkNew<vtkVelodyneTransformInterpolator> InternalInterp;

  // Current point cloud stored in two differents
  // formats: PCL-pointcloud and vtkPolyData
  vtkSmartPointer<vtkPolyData> vtkCurrentFrame;
  vtkSmartPointer<vtkPolyData> vtkProcessedFrame;
  pcl::PointCloud<Point>::Ptr pclCurrentFrame;
  std::vector<pcl::PointCloud<Point>::Ptr> pclCurrentFrameByScan;
  std::vector<std::pair<int, int> > FromVTKtoPCLMapping;
  std::vector<std::vector<int > > FromPCLtoVTKMapping;

  // Mapping between keypoints and their corresponding
  // index in the vtk input frame
  std::vector<std::pair<int, int> > EdgesIndex;
  std::vector<std::pair<int, int> > PlanarIndex;
  std::vector<std::pair<int, int> > BlobIndex;
  std::vector<int> EdgePointRejectionEgoMotion;
  std::vector<int> PlanarPointRejectionEgoMotion;
  std::vector<int> EdgePointRejectionMapping;
  std::vector<int> PlanarPointRejectionMapping;

  // If set to true the mapping planars keypoints used
  // will be the same than the EgoMotion one. If set to false
  // all points that are not set to invalid will be used
  // as mapping planars points.
  bool FastSlam = true;

  // Should the algorithm undistord the frame or not
  // The undistortion will improve the accuracy but
  // the computation speed will decrease
  bool Undistortion = false;
  vtkSmartPointer<vtkVelodyneTransformInterpolator> EgoMotionInterpolator;
  vtkSmartPointer<vtkVelodyneTransformInterpolator> MappingInterpolator;

  // Size of the leafs in the voxel grid filter
  // used by the local maps
  double LeafSize = 0.6;

  // keypoints extracted
  pcl::PointCloud<Point>::Ptr CurrentEdgesPoints;
  pcl::PointCloud<Point>::Ptr CurrentPlanarsPoints;
  pcl::PointCloud<Point>::Ptr CurrentBlobsPoints;
  pcl::PointCloud<Point>::Ptr PreviousEdgesPoints;
  pcl::PointCloud<Point>::Ptr PreviousPlanarsPoints;
  pcl::PointCloud<Point>::Ptr PreviousBlobsPoints;

  // keypoints local map
  RollingGrid* EdgesPointsLocalMap;
  RollingGrid* PlanarPointsLocalMap;
  RollingGrid* BlobsPointsLocalMap;

  // Mapping of the lasers id
  std::vector<int> LaserIdMapping;

  // Curvature and over differntial operations
  // scan by scan; point by point
  std::vector<std::vector<double> > Angles;
  std::vector<std::vector<double> > DepthGap;
  std::vector<std::vector<double> > BlobScore;
  std::vector<std::vector<double> > LengthResolution;
  std::vector<std::vector<double> > SaillantPoint;
  std::vector<std::vector<int> > IsPointValid;
  std::vector<std::vector<int> > Label;

  // with of the neighbor used to compute discrete
  // differential operators
  int NeighborWidth = 4;

  // Number of lasers scan lines composing the pointcloud
  unsigned int NLasers = 0;

  // maximal angle resolution of the lidar
  // azimutal resolution of the VLP-16. We add an extra 20 %
  double AngleResolution = 0.00698132; // 0.4 degree

  // Number of frame that have been processed
  unsigned int NbrFrameProcessed = 0;

  // minimal point/sensor sensor to consider a point as valid
  double MinDistanceToSensor = 3.0;

  // Indicated the number max of keypoints
  // that we admit per laser scan line
  unsigned int MaxEdgePerScanLine = 200;
  unsigned int MaxPlanarsPerScanLine = 200;

  // Sharpness threshold to select a point
  double EdgeSinAngleThreshold = 0.86; // 60 degrees
  double PlaneSinAngleThreshold = 0.5; // 30 degrees
  double EdgeDepthGapThreshold = 0.15;
  double DistToLineThreshold = 0.20;

  // The max distance allowed between two frames
  // If the distance is over this limit, the ICP
  // matching will not match point and the odometry
  // will fail. It has to be setted according to the
  // maximum speed of the vehicule used
  // Represent the distance that the lidar has made during one sweep
  // if it is moving at a speed of 90 km/h and spinning at a rpm
  // of 600 rotation per minute
  double MaxDistBetweenTwoFrames = (90.0 / 3.6) * (60.0 / 600.0);

  // Maximum number of iteration
  // in the ego motion optimization step
  unsigned int EgoMotionLMMaxIter = 15;

  // Maximum number of iteration
  // in the mapping optimization step
  unsigned int MappingLMMaxIter = 15;

  // During the Levenberg-Marquardt algoritm
  // keypoints will have to be match with planes
  // and lines of the previous frame. This parameter
  // indicates how many times we want to do the
  // the ICP matching
  unsigned int EgoMotionICPMaxIter = 4;
  unsigned int MappingICPMaxIter = 3;

  // When computing the point<->line and point<->plane distance
  // in the ICP, the kNearest edges/planes points of the current
  // points are selected to approximate the line/plane using a PCA
  // If the one of the k-nearest points is too far the neigborhood
  // is rejected. We also make a filter upon the ratio of the eigen
  // values of the variance-covariance matrix of the neighborhood
  // to check if the points are distributed upon a line or a plane
  unsigned int MappingLineDistanceNbrNeighbors = 15;
  unsigned int MappingMinimumLineNeighborRejection = 5;
  double MappingLineDistancefactor = 5.0;

  unsigned int MappingPlaneDistanceNbrNeighbors = 5;
  double MappingPlaneDistancefactor1 = 35.0;
  double MappingPlaneDistancefactor2 = 8.0;

  double MappingMaxPlaneDistance = 0.2;
  double MappingMaxLineDistance = 0.2;
  double MappingLineMaxDistInlier = 0.2;

  unsigned int EgoMotionLineDistanceNbrNeighbors = 10;
  unsigned int EgoMotionMinimumLineNeighborRejection = 4;
  double EgoMotionLineDistancefactor = 5.;

  unsigned int EgoMotionPlaneDistanceNbrNeighbors = 5;
  double EgoMotionPlaneDistancefactor1 = 35.0;
  double EgoMotionPlaneDistancefactor2 = 8.0;

  double EgoMotionMaxPlaneDistance = 0.2;
  double EgoMotionMaxLineDistance = 0.10;

  // norm of the farest keypoints
  double FarestKeypointDist;

  // Use or not blobs
  bool UseBlob = false;

  // Threshold upon sphricity of a neighborhood
  // to select a blob point
  double SphericityThreshold = 0.35;

  // Coef to apply to the incertitude
  // radius of the blob neighborhood
  double IncertitudeCoef = 3.0;

  // The max distance allowed between two frames
  // If the distance is over this limit, the ICP
  // matching will not match point and the odometry
  // will fail. It has to be setted according to the
  // maximum speed of the vehicule used
  double MaxDistanceForICPMatching = 20.0;

  // Transformation to map the current pointcloud
  // in the referential of the previous one
  Eigen::Matrix<double, 6, 1> Trelative;

  // Transformation to map the current pointcloud
  // in the world (i.e first frame) one
  Eigen::Matrix<double, 6, 1> Tworld;
  Eigen::Matrix<double, 6, 1> PreviousTworld;

  // Computed trajectory of the sensor
  // i.e the list of transforms computed
  std::vector<Eigen::Matrix<double, 6, 1> > TworldList;

  // To recover the ego-motion we have to minimize the function
  // f(R, T) = sum(d(point, line)^2) + sum(d(point, plane)^2). In both
  // case the distance between the point and the line / plane can be
  // writen (R*X+T - P).t * A * (R*X+T - P). Where X is the key point
  // P is a point on the line / plane. A = (n*n.t) for a plane with n
  // being the normal and A = (I - n*n.t)^2 for a line with n being
  // a director vector of the line
  // - Avalues will store the A matrix
  // - Pvalues will store the P points
  // - Xvalues will store the W points
  // - residualCoefficient will attenuate the distance function for outliers
  // - TimeValues store the time acquisition
  std::vector<Eigen::Matrix3d > Avalues;
  std::vector<Eigen::Vector3d > Pvalues;
  std::vector<Eigen::Vector3d > Xvalues;
  std::vector<double> RadiusIncertitude;
  std::vector<double> residualCoefficient;
  std::vector<double> TimeValues;

  // Histogram of the ICP matching rejection causes
  std::vector<double> MatchRejectionHistogramPlane;
  std::vector<double> MatchRejectionHistogramLine;
  std::vector<double> MatchRejectionHistogramBlob;
  int NrejectionCauses = 7;
  void ResetDistanceParameters();

  // Display information about the keypoints - neighborhood
  // mathching rejections
  void RejectionInformationDisplay();

  // Add a default point to the trajectories
  void AddDefaultPoint(double x, double y, double z, double rx, double ry, double rz, double t);

  // Convert the input vtk-format pointcloud
  // into a pcl-pointcloud format. scan lines
  // will also be sorted by their vertical angles
  void ConvertAndSortScanLines(vtkSmartPointer<vtkPolyData> input);

  // Extract keypoints from the pointcloud. The key points
  // will be separated in two classes : Edges keypoints which
  // correspond to area with high curvature scan lines and
  // planar keypoints which have small curvature
  void ComputeKeyPoints(vtkSmartPointer<vtkPolyData> input);

  // Compute the curvature of the scan lines
  // The curvature is not the one of the surface
  // that intersected the lines but the curvature
  // of the scan lines taken in an isolated way
  void ComputeCurvature(vtkSmartPointer<vtkPolyData> input);

  // Invalid the points with bad criteria from
  // the list of possible future keypoints.
  // This points correspond to planar surface
  // roughtly parallel to laser beam and points
  // close to a gap created by occlusion
  void InvalidPointWithBadCriteria();

  // Labelizes point to be a keypoints or not
  void SetKeyPointsLabels(vtkSmartPointer<vtkPolyData> input);

  // Add Transform to the interpolator
  void AddTransform(double time);
  void AddTransform(double rx, double ry, double rz, double tx, double ty, double tz, double t);

  // Reset all mumbers variables that are
  // used during the process of a frame.
  // The map and the recovered transformations
  // won't be reset.
  void PrepareDataForNextFrame();

  // Find the ego motion of the sensor between
  // the current frame and the next one using
  // the keypoints extracted.
  void ComputeEgoMotion();

  // Map the position of the sensor from
  // the current frame in the world referential
  // using the map and the keypoints extracted.
  void Mapping();

  // Transform the input point already undistort into Tworld.
  void TransformToWorld(Point& p);

  // Match the current keypoint with its neighborhood in the map / previous
  // frames. From this match we compute the point-to-neighborhood distance
  // function: 
  // (R * X + T - P).t * A * (R * X + T - P)
  // Where P is the mean point of the neighborhood and A is the symmetric
  // variance-covariance matrix encoding the shape of the neighborhood
  int ComputeLineDistanceParameters(pcl::KdTreeFLANN<Point>::Ptr kdtreePreviousEdges, Eigen::Matrix3d& R,
                                             Eigen::Vector3d& dT, Point p, std::string step);
  int ComputePlaneDistanceParameters(pcl::KdTreeFLANN<Point>::Ptr kdtreePreviousPlanes, Eigen::Matrix3d& R,
                                              Eigen::Vector3d& dT, Point p, std::string step);
  int ComputeBlobsDistanceParameters(pcl::KdTreeFLANN<Point>::Ptr kdtreePreviousBlobs, Eigen::Matrix3d& R,
                                              Eigen::Vector3d& dT, Point p, std::string step);

  // Instead of taking the k-nearest neigbirs in the odometry
  // step we will take specific neighbor using the particularities
  // of the velodyne's lidar sensor
  void GetEgoMotionLineSpecificNeighbor(std::vector<int>& nearestValid, std::vector<float>& nearestValidDist,
                                        unsigned int nearestSearch, pcl::KdTreeFLANN<Point>::Ptr kdtreePreviousEdges, Point p);

  // Instead of taking the k-nearest neighbors in the mapping
  // step we will take specific neighbor using a sample consensus
  // model
  void GetMappingLineSpecificNeigbbor(std::vector<int>& nearestValid, std::vector<float>& nearestValidDist, double maxDistInlier,
                                        unsigned int nearestSearch, pcl::KdTreeFLANN<Point>::Ptr kdtreePreviousEdges, Point p);

  // All points of the current frame has been
  // acquired at a different timestamp. The goal
  // is to express them in a same referential
  // This can be done using estimated egomotion and assuming
  // a constant angular velocity and velocity during a sweep

  // Express the provided point into the referential of the sensor
  // at time t0. The referential at time of acquisition t is estimated
  // using the constant velocity hypothesis and the provided sensor
  // position estimation
  void ExpressPointInOtherReferencial(Point& p, vtkSmartPointer<vtkVelodyneTransformInterpolator> transform);

  // Initialize the undistortion interpolator
  // for the EgoMotion part it is just an interpolation
  // between Id and Trelative
  // for the mapping part it is an interpolation between indentity
  // and the incremental transform between TworldPrevious and Tworld
  vtkSmartPointer<vtkVelodyneTransformInterpolator> InitUndistortionInterpolatorEgoMotion();
  vtkSmartPointer<vtkVelodyneTransformInterpolator> InitUndistortionInterpolatorMapping();

  // Update the world transformation by integrating
  // the relative motion recover and the previous
  // world transformation
  void UpdateTworldUsingTrelative();

  // Fill the information array with default value
  // it is used if a mapping step is skipped for example
  void FillMappingInfoArrayWithDefaultValues();
  void FillEgoMotionInfoArrayWithDefaultValues();

  // Update the maps by populate the rolling grids
  // using the current keypoints expressed in the
  // world reference frame coordinate system
  void UpdateMapsUsingTworld();

  // Display infos
  template<typename T, typename Tvtk>
  void AddVectorToPolydataPoints(const std::vector<std::vector<T>>& vec, const char* name, vtkPolyData* pd);
  void DisplayLaserIdMapping(vtkSmartPointer<vtkPolyData> input);
  void DisplayRelAdv(vtkSmartPointer<vtkPolyData> input);
  void DisplayUsedKeypoints(vtkSmartPointer<vtkPolyData> input);

  // Indicate if we are in display mode or not
  // Display mode will add arrays showing some
  // results of the slam algorithm such as
  // the keypoints extracted, curvature etc
  bool DisplayMode = false;

  // Identity matrix
  Eigen::Matrix3d I3 = Eigen::Matrix3d::Identity();
  Eigen::Matrix<double, 6, 6> I6 = Eigen::Matrix<double, 6, 6>::Identity();
};

#endif // VTK_SLAM_H

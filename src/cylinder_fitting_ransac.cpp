/*
Copyright 2018 Rui Miguel Horta Pimentel de Figueiredo

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

/*!    
    \author Rui Figueiredo : ruipimentelfigueiredo
*/

#include "cylinder_fitting_ransac.h"
CylinderFittingRansac::CylinderFittingRansac(float normal_distance_weight_, unsigned int max_iterations_, float distance_threshold_, float min_radius_,float max_radius_, bool do_refine_) :
	CylinderFitting(min_radius_,max_radius_,do_refine_),
	normal_distance_weight(normal_distance_weight_),
	max_iterations(max_iterations_),
	distance_threshold(distance_threshold_)
{
	// Estimate point normals
	ne.setSearchMethod (tree);
	ne.setKSearch (50);


	// Create the segmentation object for cylinder segmentation and set all the parameters
	seg.setOptimizeCoefficients (do_refine_);
	seg.setModelType (pcl::SACMODEL_CYLINDER);
	seg.setMethodType (pcl::SAC_RANSAC);
	seg.setDistanceThreshold (distance_threshold);
	seg.setNormalDistanceWeight (normal_distance_weight);
	seg.setMaxIterations (max_iterations);
	seg.setRadiusLimits (min_radius, max_radius);
};

FittingData CylinderFittingRansac::fit(const PointCloudT::ConstPtr & point_cloud_in_)
{
	// Compute normals
	ne.setInputCloud (point_cloud_in_);
	ne.compute (*cloud_normals);

	seg.setInputCloud (point_cloud_in_);
	seg.setInputNormals (cloud_normals);

	// Obtain the cylinder inliers and coefficients
	pcl::ModelCoefficients::Ptr coefficients_cylinder (new pcl::ModelCoefficients);
	seg.segment (*inliers_cylinder, *coefficients_cylinder);

	//std::cout << "inliers:" << inliers_cylinder->indices.size() << std::endl;

	// Extract the cylinder inliers
	pcl::ExtractIndices<PointT> extract; 
	extract.setInputCloud (point_cloud_in_);
	extract.setIndices (inliers_cylinder);
	extract.setNegative (true);
	extract.filter (*transformed_cloud);
	if (transformed_cloud->points.empty ()) 
		std::cerr << "Can't find the cylindrical component." << std::endl;

	Eigen::Vector3f cylinder_direction(coefficients_cylinder->values[3],coefficients_cylinder->values[4],coefficients_cylinder->values[5]);

	//Get rotation matrix that aligns the cylinder point cloud with rotation axis
	Eigen::Matrix4f R2;
	R2=Eigen::Matrix4f::Identity();

   	Eigen::Vector3f up = Eigen::Vector3f::UnitZ();

	Eigen::Vector3f rot_axis = cylinder_direction.cross(up);

	rot_axis.normalize();

	if(std::isnan(rot_axis[0])||std::isnan(rot_axis[1])||std::isnan(rot_axis[2]))
	{
		R2=Eigen::Matrix4f::Identity();
	}
	else
	{
		Eigen::Matrix3f aux;
		aux=Eigen::AngleAxisf(acos(cylinder_direction.dot(up)),rot_axis);
		R2.block(0,0,3,3)=aux;
	}


	// Executing the transformation that aligns the cylinder rotation axis with z_up)
	pcl::transformPointCloud (*transformed_cloud, *transformed_cloud, R2);

	Eigen::Vector4f min_pt,max_pt;
	pcl::getMinMax3D(*transformed_cloud,min_pt,max_pt);

	// First get cylinder posibilion in transformed coordinates
	Eigen::Vector3f cylinder_position=R2.block(0,0,3,3)*Eigen::Vector3f(coefficients_cylinder->values[0],coefficients_cylinder->values[1],coefficients_cylinder->values[2]);
	// Then change the height for the correct value and convert back to original coordinates;
	//cylinder_position[2]=min_pt[2];
	//Eigen::Vector3f cylinder_position_final=R2.block(0,0,3,3).transpose()*cylinder_position;
	double height=max_pt[2]-min_pt[2];

	Eigen::VectorXf coeffs(7,1);
	coeffs << 
		cylinder_position[0],
		cylinder_position[1],
		cylinder_position[2],
		cylinder_direction[0],
		cylinder_direction[1],
		cylinder_direction[2],
		coefficients_cylinder->values[6];
		//height;

	// Create the filtering object
	PointCloudT::Ptr cloud_projected(new PointCloudT);
	pcl::SampleConsensusModelCylinder<PointT, NormalT>::Ptr dit (new pcl::SampleConsensusModelCylinder<PointT,NormalT> (point_cloud_in_)); 
    	dit->setInputNormals(cloud_normals); 
	std::vector<int> inliers; 
	dit -> selectWithinDistance (coeffs, 0.01, inliers); 
	pcl::copyPointCloud<PointT>(*point_cloud_in_, inliers, *cloud_projected); 


	double inlier_ratio_=((double)cloud_projected->size()/(double)point_cloud_in_->size());

	// Refine height

	pcl::getMinMax3D(*cloud_projected,min_pt,max_pt);

	Eigen::VectorXf final_coeffs(8,1);
	final_coeffs << coeffs,
			height;

	FittingData cylinder_fitting(final_coeffs,inlier_ratio_,FittingData::CYLINDER,cloud_projected);

	return cylinder_fitting;
}





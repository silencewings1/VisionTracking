#pragma once
#include "../def/type_def.h"

class Optimizator
{
public:
	/* 3d center, cost */
	std::tuple<Eigen::Matrix3d, Eigen::Vector3d, double>
		process(const Corners& left, const Corners& right);

private:
	std::tuple<Corners, Corners>
		alignCorners(Corners left, Corners right) const;

	std::tuple<Eigen::Matrix3d, Eigen::Vector3d> calcExtrinsicParabyPNP(const std::vector<Eigen::Vector3d>& M_points) const;

	/* R, t */
	std::tuple<Eigen::Matrix3d, Eigen::Vector3d>
		calcExtrinsicPara(const Eigen::MatrixXd& V, int id) const;
	/* M_poins, V_matrix */
	std::tuple<std::vector<Eigen::Vector3d>, Eigen::MatrixXd>
		initHomography() const;

	std::vector<Eigen::Vector3d> getCorresponding3DPoints() const;

	/* R, t, cost */
	std::tuple<Eigen::Matrix3d, Eigen::Vector3d, double>
		optimizeExtrinsicPara(const std::vector<Eigen::Vector3d>& M_points, const Eigen::MatrixXd& V) const;
	std::tuple<Eigen::Matrix3d, Eigen::Vector3d, double>
		optimizeExtrinsicParabyPNP(const std::vector<Eigen::Vector3d>& M_points) const;
	std::tuple<Eigen::Matrix3d, Eigen::Vector3d, double>
		optimizeExtrinsicParaAll(const std::vector<Eigen::Vector3d>& M_points, const Eigen::MatrixXd& V) const;

	/* R, t, cost */
	std::tuple<Eigen::Matrix3d, Eigen::Vector3d, double>
		opti(const Eigen::Matrix3d& R, const Eigen::Vector3d& t, const std::vector<Eigen::Vector3d>& M_points, double pose[6]) const;

	// others
	void verify(const Eigen::Matrix3d& R, const Eigen::Vector3d& t, const std::vector<Eigen::Vector3d>& M_points) const;

private:
	Corners left_corners;
	Corners right_corners;
};
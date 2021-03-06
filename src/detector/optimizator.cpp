#include "optimizator.h"
#include "../config/config.h"
#include "../alg/rotation.h"
#include <Eigen/Dense>
#include <algorithm>
#include <ceres/ceres.h>
#include <map>
#include <opencv2/core/eigen.hpp>
#include <opencv2/opencv.hpp>

#include "PNPSolver.h"

const auto r = 9.1 / 2; // radius
const auto circle_num = 28;// 36;
const auto L = 2 * PI * r / circle_num; // arch

bool last_opti_result = false;
Eigen::Matrix3d res_R_last = Eigen::Matrix3d::Zero();
Eigen::Vector3d res_T_last = Eigen::Vector3d::Zero();

std::tuple<Eigen::Matrix3d, Eigen::Vector3d, double> Optimizator::process(const Corners& left, const Corners& right)
{
	std::tie(left_corners, right_corners) = alignCorners(left, right);

	if (left_corners.size() >= 4)
	{
		/*zhang method*/
		//auto [M_points, V] = initHomography();
		//auto [R, t, cost] = optimizeExtrinsicPara(M_points, V);

		/*PNP method*/
		//auto M_pointsX = getCorresponding3DPoints();
		//auto [R, t, cost] = optimizeExtrinsicParabyPNP(M_pointsX);

		/*zhang and PNP method*/
		auto [M_points, V] = initHomography();
		auto [R, t, cost] = optimizeExtrinsicParaAll(M_points, V);

		return { R, t, cost };
	}

	// verify(R, t, M_points);

	///////// calc center on image ///////////
	// Eigen::Matrix3d RR = R;
	// RR.col(2) = t;
	// Eigen::Matrix3d H_left = A_cam * RR;
	// Eigen::Vector3d center = H_left * Eigen::Vector3d(0, 0, 1);
	// Corner center_o(center.x() / center.z(), center.y() / center.z());

	return { Eigen::Matrix3d::Identity(),  Eigen::Vector3d::Zero(), 100 };
}

std::tuple<Corners, Corners>
Optimizator::alignCorners(Corners left, Corners right) const
{
	auto comp_x = [](const Corner& lhs, const Corner& rhs) { return lhs.x < rhs.x; };
	auto comp_y = [](const Corner& lhs, const Corner& rhs) { return lhs.y < rhs.y; };

	auto is_range_avg_u_greater_than_v = [&]() {
		auto u_range = [&comp_x](const Corners& corners) {
			auto [min_x, max_x] = std::minmax_element(corners.begin(), corners.end(), comp_x);
			return max_x->x - min_x->x;
		};
		auto v_range = [&comp_y](const Corners& corners) {
			auto [min_y, max_y] = std::minmax_element(corners.begin(), corners.end(), comp_y);
			return max_y->y - min_y->y;
		};

		auto u_range_avg = (u_range(left) + u_range(right)) / 2;
		auto v_range_avg = (v_range(left) + v_range(right)) / 2;

		return u_range_avg > v_range_avg;
	};
	if (is_range_avg_u_greater_than_v())
	{
		std::sort(left.begin(), left.end(), comp_x);
		std::sort(right.begin(), right.end(), comp_x);
	}
	else
	{
		std::sort(left.begin(), left.end(), comp_y);
		std::sort(right.begin(), right.end(), comp_y);
	}

	const auto num_adjust = (size_t)floor(std::min(left.size(), right.size()) / 2);
	std::map<float, int> v_error;
	auto iter = [&](int id)
		-> std::tuple<Corners::iterator, Corners::iterator> {
		Corners::iterator it_left, it_right;
		if (id < num_adjust)
		{
			it_left = left.begin() + id;
			it_right = right.begin();
		}
		else
		{
			it_left = left.begin();
			it_right = right.begin() + (id - num_adjust);
		}

		return { it_left, it_right };
	};
	for (int id = 0; id < 2 * num_adjust + 1; ++id)
	{
		auto [it_left, it_right] = iter(id);
		int count = 0;
		PixelType v_error_total = 0;
		while (it_left != left.end() && it_right != right.end())
		{
			v_error_total += abs(it_left->y - it_right->y);
			++count;
			++it_left;
			++it_right;
		}
		v_error.insert(std::make_pair(v_error_total / count, id));
	}

	auto [it_left, it_right] = iter(v_error.begin()->second);
	auto num = std::min(left.end() - it_left, right.end() - it_right);
	return { Corners(it_left, it_left + num), Corners(it_right, it_right + num) };
}

std::tuple<Eigen::Matrix3d, Eigen::Vector3d>
Optimizator::calcExtrinsicParabyPNP(const std::vector<Eigen::Vector3d>& M_points) const
{
	//初始化PNPSolver类
	PNPSolver p4psolver;
	//初始化相机参数
	p4psolver.SetCameraMatrix(1156.3, 1156.3, 1000.5, 560.3);
	//设置畸变参数
	p4psolver.SetDistortionCoefficients(0, 0, 0, 0, 0);

	//设置特征点的世界坐标,图像坐标
	for (int i = 0; i < M_points.size(); i++)
	{
		//三维坐标的单位是毫米
		p4psolver.Points3D.push_back(cv::Point3f(M_points.at(i)(0), M_points.at(i)(1), 0));

		p4psolver.Points2D.push_back(left_corners.at(i));
	}

	if (p4psolver.Solve(PNPSolver::METHOD::CV_EPNP) == 0)
	{
		Eigen::Matrix3d matrixTemp;
		Eigen::Vector3d vectTemp;
		cv2eigen(p4psolver.RoteM, matrixTemp);
		cv2eigen(p4psolver.TransM, vectTemp);

		return { matrixTemp, vectTemp };
	}
	else
	{
		//初始化PNPSolver类
		PNPSolver p4psolver_p3p;
		//初始化相机参数
		p4psolver_p3p.SetCameraMatrix(1156.3, 1156.3, 1000.5, 560.3);
		//设置畸变参数
		p4psolver_p3p.SetDistortionCoefficients(0, 0, 0, 0, 0);

		//设置特征点的世界坐标,图像坐标
		for (int i = 0; i < 4; i++)
		{
			//三维坐标的单位是毫米
			p4psolver_p3p.Points3D.push_back(cv::Point3f(M_points.at(i)(1), M_points.at(i)(2), 0));

			p4psolver_p3p.Points2D.push_back(left_corners.at(i));
		}
		if (p4psolver.Solve(PNPSolver::METHOD::CV_P3P) == 0)
		{
			Eigen::Matrix3d matrixTemp;
			Eigen::Vector3d vectTemp;
			/*cv::Mat Rmat_temp = p4psolver.RoteM;
			matrixTemp(0, 0) = Rmat_temp.ptr<double>(0)[0]; matrixTemp(0, 1) = Rmat_temp.ptr<double>(0)[1]; matrixTemp(0, 2) = Rmat_temp.ptr<double>(0)[2];
			matrixTemp(1, 0) = Rmat_temp.ptr<double>(1)[0]; matrixTemp(1, 1) = Rmat_temp.ptr<double>(1)[1]; matrixTemp(1, 2) = Rmat_temp.ptr<double>(1)[2];
			matrixTemp(2, 0) = Rmat_temp.ptr<double>(2)[0]; matrixTemp(2, 1) = Rmat_temp.ptr<double>(2)[1]; matrixTemp(2, 2) = Rmat_temp.ptr<double>(2)[2];

			cv::Mat Tmat_temp = p4psolver.TransM;
			vectTemp(0) = Tmat_temp.ptr<double>(0)[0];
			vectTemp(1) = Tmat_temp.ptr<double>(0)[1];
			vectTemp(2) = Tmat_temp.ptr<double>(0)[2];*/

			cv2eigen(p4psolver.RoteM, matrixTemp);
			cv2eigen(p4psolver.TransM, vectTemp);
			return { matrixTemp, vectTemp };
		}
	}

	return { Eigen::Matrix3d::Identity(), Eigen::Vector3d::Zero() };
}

std::tuple<Eigen::Matrix3d, Eigen::Vector3d>
Optimizator::calcExtrinsicPara(const Eigen::MatrixXd& V, int id) const
{
	Eigen::MatrixXd H = V.col(9 - 1 - id);
	H.resize(3, 3);
	H.transposeInPlace();

	Eigen::Matrix3d A_cam_inv = A_cam.inverse();
	Eigen::Vector3d r1 = A_cam_inv * H.col(0);
	Eigen::Vector3d r2 = A_cam_inv * H.col(1);
	auto lambda = (1 / r1.norm() + 1 / r2.norm()) / 2;
	r1 *= lambda;
	r2 *= lambda;
	Eigen::Vector3d t = lambda * A_cam_inv * H.col(2);
	if (t.z() < 0)
	{
		t = -t;
		r1 = -r1;
		r2 = -r2;
	}
	Eigen::Vector3d r3 = r1.cross(r2);
	Eigen::Matrix3d R;
	R.col(0) = r1;
	R.col(1) = r2;
	R.col(2) = r3;

	Eigen::JacobiSVD<Eigen::MatrixXd> svd_R(R, Eigen::ComputeFullU | Eigen::ComputeFullV);
	R = svd_R.matrixU() * svd_R.matrixV().transpose();

	return { R, t };
}

std::vector<Eigen::Vector3d> Optimizator::getCorresponding3DPoints() const
{
	const auto PAIR_SIZE = left_corners.size();

	std::vector<Eigen::Vector3d> M_points;
	for (int i = 0; i < PAIR_SIZE; i++)
	{
		auto angle = i * L / r;
		M_points.emplace_back(r * cos(angle), r * sin(angle), 1);
	}
	return M_points;
}

std::tuple<std::vector<Eigen::Vector3d>, Eigen::MatrixXd>
Optimizator::initHomography() const
{
	const auto PAIR_SIZE = left_corners.size();

	std::vector<Eigen::Vector3d> M_points;
	Eigen::MatrixXd L_map(2 * PAIR_SIZE, 9);
	for (int i = 0; i < PAIR_SIZE; ++i)
	{
		auto angle = i * L / r;
		M_points.emplace_back(r * cos(angle), r * sin(angle), 1);

		L_map.block<1, 3>(2 * i, 0) = M_points.at(i).transpose();
		L_map.block<1, 3>(2 * i, 3) = Eigen::Vector3d::Zero().transpose();
		L_map.block<1, 3>(2 * i, 6) = -left_corners.at(i).x * M_points.at(i).transpose();

		L_map.block<1, 3>(2 * i + 1, 0) = Eigen::Vector3d::Zero().transpose();
		L_map.block<1, 3>(2 * i + 1, 3) = M_points.at(i).transpose();
		L_map.block<1, 3>(2 * i + 1, 6) = -left_corners.at(i).y * M_points.at(i).transpose();
	}

	Eigen::JacobiSVD<Eigen::MatrixXd> svd_L(L_map, Eigen::ComputeFullU | Eigen::ComputeFullV);

	return { M_points, svd_L.matrixV() };
}

std::tuple<Eigen::Matrix3d, Eigen::Vector3d, double>
Optimizator::optimizeExtrinsicParabyPNP(const std::vector<Eigen::Vector3d>& M_points) const
{
	Eigen::Vector3d point_mid = M_points.at(M_points.size() / 2);
	point_mid(2) = 0;
	point_mid = point_mid / point_mid.norm();

	auto costMin = 10000.0;
	std::vector<Eigen::Matrix3d> R_init;
	std::vector<Eigen::Vector3d> t_init;

	auto [R_algebra, t_algebra] = calcExtrinsicParabyPNP(M_points);
	if (last_opti_result)
	{
		R_init.push_back(res_R_last);
		t_init.push_back(res_T_last);
		R_init.push_back(R_algebra);
		t_init.push_back(t_algebra);
		last_opti_result = false;
	}
	else
	{
		R_init.push_back(R_algebra);
		t_init.push_back(t_algebra);
	}

	Eigen::Matrix3d R_test;
	Eigen::Vector3d t_test;
	Eigen::Matrix3d R_candidates;
	Eigen::Vector3d t_candidates;
	for (int i = 0; i < R_init.size(); ++i)
	{
		R_test = R_init.at(i);
		t_test = t_init.at(i);
		if (t_test(2) < 0)
		{
			t_test(0) = -t_test(0);
			t_test(1) = -t_test(1);
			t_test(2) = -t_test(2);
		}

		std::array<double, 2> direction = { 0, PI };
		for (const auto& dir : direction)
		{
			auto rotX = [&dir]() {
				Eigen::Matrix3d rot;
				rot << 1, 0, 0,
					0, cos(dir), -sin(dir),
					0, sin(dir), cos(dir);

				return rot;
			};
			R_test = R_test * rotX();

			// relative pose
			double init_pose[6] = { 1.0, 0, 0, 0, 0, 0 };
			auto [R_relative, t_relative, cost] = opti(R_test, t_test, M_points, init_pose);

			R_test = R_test * R_relative;
			t_test = t_test + t_relative;

			// judge
			Eigen::Vector3d point_cam = R_test * point_mid;

			if (point_cam.z() < 0 && t_test.z() > 0)
			{
				if (cost < OPTI_COST_THRESHOLD / 2)
				{
					last_opti_result = true;
					res_R_last = R_test;
					res_T_last = t_test;
					return { R_test, t_test, cost };
				}
				else
				{
					if (cost < costMin)
					{
						costMin = cost;
						R_candidates = R_test;
						t_candidates = t_test;
					}
				}
			}
		}
	}
	if (costMin < OPTI_COST_THRESHOLD)
	{
		last_opti_result = true;
	}

	if (costMin < 10000.0)
	{
		return { R_candidates, t_candidates, costMin };
	}
	else
	{
		return { Eigen::Matrix3d::Identity(), Eigen::Vector3d::Zero(), costMin };
	}
}

std::tuple<Eigen::Matrix3d, Eigen::Vector3d, double>
Optimizator::optimizeExtrinsicParaAll(const std::vector<Eigen::Vector3d>& M_points, const Eigen::MatrixXd& V) const
{
	Eigen::Vector3d point_mid = M_points.at(M_points.size() / 2);
	point_mid(2) = 0;
	point_mid = point_mid / point_mid.norm();

	auto costMin = 10000.0;
	std::vector<Eigen::Matrix3d> R_init;
	std::vector<Eigen::Vector3d> t_init;

	Eigen::Matrix3d R_PNP, R_homo;
	Eigen::Vector3d t_PNP, t_homo;
	tie(R_PNP, t_PNP) = calcExtrinsicParabyPNP(M_points);
	if (last_opti_result)
	{
		R_init.push_back(res_R_last);
		t_init.push_back(res_T_last);
		R_init.push_back(R_PNP);
		t_init.push_back(t_PNP);
		last_opti_result = false;
	}
	else
	{
		R_init.push_back(R_PNP);
		t_init.push_back(t_PNP);
	}
	/*额外6个homo对应的初值*/
	for (int idx_temp = 0; idx_temp < 6; idx_temp++)
	{
		tie(R_homo, t_homo) = calcExtrinsicPara(V, idx_temp);
		R_init.push_back(R_homo);
		t_init.push_back(t_homo);
	}

	Eigen::Matrix3d R_test;
	Eigen::Vector3d t_test;
	Eigen::Matrix3d R_candidates;
	Eigen::Vector3d t_candidates;
	for (int i = 0; i < R_init.size(); ++i)
	{
		R_test = R_init.at(i);
		t_test = t_init.at(i);
		if (t_test(2) < 0)
		{
			t_test(0) = -t_test(0);
			t_test(1) = -t_test(1);
			t_test(2) = -t_test(2);
		}

		std::array<double, 2> direction = { 0, PI };
		for (const auto& dir : direction)
		{
			auto rotX = [&dir]() {
				Eigen::Matrix3d rot;
				rot << 1, 0, 0,
					0, cos(dir), -sin(dir),
					0, sin(dir), cos(dir);

				return rot;
			};
			R_test = R_test * rotX();

			// relative pose
			double init_pose[6] = { 1.0, 0, 0, 0, 0, 0 };
			auto [R_relative, t_relative, cost] = opti(R_test, t_test, M_points, init_pose);

			R_test = R_test * R_relative;
			t_test = t_test + t_relative;

			// judge
			Eigen::Vector3d point_cam = R_test * point_mid;

			if (point_cam.z() < 0 && t_test.z() > 0)
			{
				if (cost < OPTI_COST_THRESHOLD / 2)
				{
					last_opti_result = true;
					res_R_last = R_test;
					res_T_last = t_test;
					return { R_test, t_test, cost };
				}
				else
				{
					if (cost < costMin)
					{
						costMin = cost;
						R_candidates = R_test;
						t_candidates = t_test;
					}
				}
			}
		}
	}
	if (costMin < OPTI_COST_THRESHOLD)
	{
		last_opti_result = true;
	}

	if (costMin < 10000.0)
	{
		return { R_candidates, t_candidates, costMin };
	}
	else
	{
		return { Eigen::Matrix3d::Identity(), Eigen::Vector3d::Zero(), costMin };
	}
}

std::tuple<Eigen::Matrix3d, Eigen::Vector3d, double>
Optimizator::optimizeExtrinsicPara(const std::vector<Eigen::Vector3d>& M_points, const Eigen::MatrixXd& V) const
{
	std::map<double, std::pair<Eigen::Matrix3d, Eigen::Vector3d>> opti_record;

	Eigen::Vector3d point_mid = M_points.at(M_points.size() / 2);
	point_mid(2) = 0;
	point_mid = point_mid / point_mid.norm();
	float costMin = 10000.0;
	int init_add = 0;

	Eigen::Matrix3d R = Eigen::Matrix3d::Zero();
	Eigen::Vector3d t = Eigen::Vector3d::Zero();
	Eigen::Matrix3d R_candidates;
	Eigen::Vector3d t_candidates;
	int idx_temp = 0;
	for (int i = 0; i < 9; ++i)
	{
		if (last_opti_result)
		{
			R = res_R_last;
			t = res_T_last;
			last_opti_result = false;
			init_add = 1;
		}
		else
		{
			idx_temp = i - init_add;
			tie(R, t) = calcExtrinsicPara(V, idx_temp);
		}

		std::array<double, 2> direction = { 0, PI };
		for (const auto& dir : direction)
		{
			auto rotX = [&dir]() {
				Eigen::Matrix3d rot;
				rot << 1, 0, 0,
					0, cos(dir), -sin(dir),
					0, sin(dir), cos(dir);

				return rot;
			};
			R = R * rotX();

			// relative pose
			double init_pose[6] = { 0, 0, 0, 0, 0, 0 };
			auto [R_relative, t_relative, cost] = opti(R, t, M_points, init_pose);

			R = R * R_relative;
			t = t + t_relative;

			// judge
			Eigen::Vector3d point_cam = R * point_mid;

			if (point_cam.z() < 0 && t.z() > 0)
			{
				if (cost < OPTI_COST_THRESHOLD / 2)
				{
					last_opti_result = true;
					res_R_last = R;
					res_T_last = t;
					return { R, t, cost };
				}
				else
				{
					if (cost < costMin)
					{
						costMin = cost;
						R_candidates = R;
						t_candidates = t;
					}
				}
			}
		}
	}
	if (costMin < OPTI_COST_THRESHOLD)
	{
		last_opti_result = true;
	}


	if (costMin < 10000.0)
	{
		return { R_candidates, t_candidates, costMin };
	}
	else
	{
		return { Eigen::Matrix3d::Identity(), Eigen::Vector3d::Zero(), costMin };
	}
}

struct ReprojectionError
{
	ReprojectionError(const Eigen::Matrix3d& R,
		const Eigen::Vector3d& t,
		const Corner& m,
		const Eigen::Vector3d& M)
		: R(R)
		, t(t)
		, m(m)
		, M(M)
	{
	}

	template <typename T>
	bool operator()(const T* const pose, T* residuals) const
	{
		T R_relative[9];
		T t_relative[3];
		Pose2RT(pose, R_relative, t_relative);

		T R_now[9];
		T t_now[3];
		T RR[9];
		for (int i = 0; i < 9; ++i)
			RR[i] = T(R(i / 3, i % 3));
		T tt[3];
		for (int i = 0; i < 3; ++i)
			tt[i] = T(t(i));
		MatMulMat(RR, R_relative, R_now);
		VecAddVec(tt, t_relative, t_now);

		T E[9];
		[&R_now, &t_now](T E[9]) {
			for (int i = 0; i < 9; ++i)
				E[i] = R_now[i];

			E[2] = t_now[0];
			E[5] = t_now[1];
			E[8] = t_now[2];
		}(E);

		T H[9];
		T A[9];
		for (int i = 0; i < 9; ++i)
			A[i] = T(A_cam(i / 3, i % 3));
		MatMulMat(A, E, H);

		T MM[3];
		MM[0] = T(M.x());
		MM[1] = T(M.y());
		MM[2] = T(M.z());
		T MMM[3];
		MatMulVec(H, MM, MMM);

		residuals[0] = T(m.x) - MMM[0] / MMM[2];
		residuals[1] = T(m.y) - MMM[1] / MMM[2];
		return true;
	}

	const Eigen::Matrix3d R;
	const Eigen::Vector3d t;
	const Corner m;
	const Eigen::Vector3d M;
};

std::tuple<Eigen::Matrix3d, Eigen::Vector3d, double>
Optimizator::opti(const Eigen::Matrix3d& R, const Eigen::Vector3d& t, const std::vector<Eigen::Vector3d>& M_points, double pose[6]) const
{
	ceres::Problem problem;
	auto problem_count = 0;
	for (int i = 0; i < left_corners.size(); ++i)
	{
		problem.AddResidualBlock(
			new ceres::AutoDiffCostFunction<ReprojectionError, 2, 6>(
				new ReprojectionError(R, t, left_corners.at(i), M_points.at(i))),
			nullptr,
			pose);
		++problem_count;
	}
	for (int i = 0; i < right_corners.size(); ++i)
	{
		Eigen::Vector3d bias(-b_dis, 0, 0);
		problem.AddResidualBlock(
			new ceres::AutoDiffCostFunction<ReprojectionError, 2, 6>(
				new ReprojectionError(R, t + bias, right_corners.at(i), M_points.at(i))),
			nullptr,
			pose);
		++problem_count;
	}

	ceres::Solver::Options options;
	options.trust_region_strategy_type = ceres::LEVENBERG_MARQUARDT;
	options.linear_solver_type = ceres::DENSE_QR;
	options.minimizer_progress_to_stdout = false;
	options.num_threads = 8;
	options.function_tolerance = 1e-10;
	ceres::Solver::Summary summary;
	ceres::Solve(options, &problem, &summary);

	//std::cout << summary.BriefReport() << "\n";
	//std::cout << "a= " << pose[0] << std::endl;
	//std::cout << "b= " << pose[1] << std::endl;
	//std::cout << "c= " << pose[2] << std::endl;
	//std::cout << "x= " << pose[3] << std::endl;
	//std::cout << "y= " << pose[4] << std::endl;
	//std::cout << "z= " << pose[5] << std::endl;
	//std::cout << "cost= " << summary.final_cost / (2 * M_points.size()) << std::endl;

	auto rotRPY = [](const double phi, const double theta, const double psi) {
		Eigen::Matrix3d rot;
		rot << cos(phi) * cos(theta), -sin(phi) * cos(psi) + cos(phi) * sin(theta) * sin(psi), sin(phi)* sin(psi) + cos(phi) * sin(theta) * cos(psi),
			sin(phi)* cos(theta), cos(phi)* cos(psi) + sin(phi) * sin(theta) * sin(psi), -cos(phi) * sin(psi) + sin(phi) * sin(theta) * cos(psi),
			-sin(theta), cos(theta)* sin(psi), cos(theta)* cos(psi);

		return rot;
	};
	return { rotRPY(pose[0], pose[1], pose[2]), Eigen::Vector3d(pose[3], pose[4], pose[5]), sqrt(summary.final_cost / problem_count) };
}

void Optimizator::verify(const Eigen::Matrix3d& R, const Eigen::Vector3d& t, const std::vector<Eigen::Vector3d>& M_points) const
{
	std::cout << R << std::endl;
	std::cout << t << std::endl;

	// verify
	Eigen::Matrix3d RR = R;
	RR.col(2) = t;
	Eigen::Matrix3d H_left = A_cam * RR;
	RR.col(2) += Eigen::Vector3d(-b_dis, 0, 0);
	Eigen::Matrix3d H_right = A_cam * RR;

	Corners left_after, right_after;
	for (const auto& c : M_points)
	{
		Eigen::Vector3d ml = H_left * c;
		left_after.emplace_back(ml.x() / ml.z(), ml.y() / ml.z());
		Eigen::Vector3d mr = H_right * c;
		right_after.emplace_back(mr.x() / mr.z(), mr.y() / mr.z());
	}

	std::cout << "left_error: \n";
	for (int i = 0; i < left_after.size(); ++i)
	{
		auto c = left_after.at(i) - left_corners.at(i);
		std::cout << c.x << '\t' << c.y << std::endl;
	}

	std::cout << "right_error: \n";
	for (int i = 0; i < right_after.size(); ++i)
	{
		auto c = right_after.at(i) - right_corners.at(i);
		std::cout << c.x << '\t' << c.y << std::endl;
	}
}
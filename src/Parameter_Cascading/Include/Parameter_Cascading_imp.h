#ifndef __PARAMETER_CASCADING_IMP_H__
#define __PARAMETER_CASCADING_IMP_H__

#include "../../Lambda_Optimization/Include/Optimization_Data.h"
#include "../../Lambda_Optimization/Include/Function_Variadic.h"
#include "../../Lambda_Optimization/Include/Newton.h"
#include "../../Lambda_Optimization/Include/Optimization_Methods_Factory.h"
#include "Optimization_Algorithm.h"

#include <memory>
#include <functional>
#include <limits>

template <UInt ORDER, UInt mydim, UInt ndim>
Real Parameter_Cascading<ORDER, mydim, ndim>::compute_optimal_lambda(Carrier<RegressionDataElliptic>& carrier, GCV_Stochastic<Carrier<RegressionDataElliptic>, 1>& GS) const
{	

	Function_Wrapper<Real, Real, Real, Real, GCV_Stochastic<Carrier<RegressionDataElliptic>, 1>> Fun(GS);

	const OptimizationData optr = H.getModel().getOptimizationData();

	std::unique_ptr<Opt_methods<Real,Real,GCV_Stochastic<Carrier<RegressionDataElliptic>, 1>>>
	optim_p = Opt_method_factory<Real, Real, GCV_Stochastic<Carrier<RegressionDataElliptic>, 1>>::create_Opt_method(optr.get_criterion(), Fun);

	// Choose initial lambda
	Real lambda_init = optr.get_initial_lambda_S();

	Rprintf("initial lambda for compute_opt_lambda: %e\n", lambda_init);

	// Start from 6 lambda and find the minimum value of GCV to pick a good initialization for Newton method
	std::vector<Real> lambda_grid = {5.000000e-05, 1.442700e-03, 4.162766e-02, 1.201124e+00, 3.465724e+01, 1.000000e+03};
	UInt dim = lambda_grid.size();
	Real lambda_min;
	Real GCV_min = -1.0;
	for (UInt i = 0; i < dim; i++)
	{
		Real evaluation = Fun.evaluate_f(lambda_grid[i]); //only scalar functions;
		if (evaluation < GCV_min || i == 0)
		{
			GCV_min = evaluation;
			lambda_min = lambda_grid[i];
		}
	}
	// If lambda_init <= 0, use the one from the grid
	if (lambda_init > lambda_min/4 || lambda_init <= 0)
		lambda_init = lambda_min/8;

	Rprintf("new initial lambda for compute_opt_lambda: %e\n", lambda_init);

	Checker ch;
	std::vector<Real> lambda_values;
	std::vector<Real> GCV_values;

	// Compute optimal lambda
	std::pair<Real, UInt> lambda_couple = 
	optim_p->compute(lambda_init, optr.get_stopping_criterion_tol(), 40, ch, GCV_values, lambda_values);

	Rprintf("optimal lambda is = %e\n", lambda_couple.first);

	return lambda_couple.first;
}


template <UInt ORDER, UInt mydim, UInt ndim>
void Parameter_Cascading<ORDER, mydim, ndim>::step_K(void)
{
	// Vectors to store the optimal values for each lambda in lambdas
	VectorXr angles(lambdas.size() + 1);
	VectorXr intensities(lambdas.size() + 1);

	// Initialization
	angles(0) = angle;
	intensities(0) = intensity;
	Rprintf("Initial angle and intensity %f , %f\n", angle, intensity);

	// Vectors to store the GCV values for each lambda in lambdas
	VectorXr GCV_values(lambdas.size());

	// Parameters for optimization algorithm
	Eigen::Vector2d lower_bound(0.0, 0.0);
	Eigen::Vector2d upper_bound(EIGEN_PI, std::numeric_limits<Real>::infinity());
	Parameter_Genetic_Algorithm<Eigen::Vector2d> param = {100, lower_bound, upper_bound}; // set number of iter
	
	// Optimal lambda to compute GCV
	Real lambda_opt;

	for (UInt iter = 0; iter < lambdas.size(); ++iter)
	{
		// Optimization step
		auto F = [this, &iter](Eigen::Vector2d x){return this -> H.eval_K(x(0),x(1), this -> lambdas(iter));};

		Eigen::Vector2d x1(2);
		x1 << 0.5, 5.0;
		Eigen::Vector2d x2(2);
		x2 << 1.3, 12.0;
		Rprintf("Prova di eval_K with lambda = %f: ", lambdas(iter));
		Real sol1=F(x1);
		Real sol2=F(x2);
		Real sol3 = H.eval_K(0.5, 5.0, lambdas(iter));
		Real sol4 = H.eval_K(1.3, 12.0, lambdas(iter));
		
		Eigen::Vector2d init(angles(iter), intensities(iter));

		Rprintf("Optimization Algorithm started\n");

		Genetic_Algorithm<Eigen::Vector2d, Real> opt(F, init, param);
		opt.apply();

		Rprintf("Optimization Algorithm done\n");

		
		// Store the optimal solution
		Eigen::Vector2d opt_sol = opt.get_solution();
		angles(iter + 1) = opt_sol(0);
		intensities(iter + 1) = opt_sol(1);

		// Compute GCV with the new parameters
		H.set_K(angles(iter + 1), intensities(iter + 1));

		Carrier<RegressionDataElliptic> carrier = CarrierBuilder<RegressionDataElliptic>::build_plain_carrier(H.getModel().getRegressionData(), H.getModel(), H.getModel().getOptimizationData());
		GCV_Stochastic<Carrier<RegressionDataElliptic>, 1> GS(carrier, true);

		lambda_opt = compute_optimal_lambda(carrier, GS);

		GS.update_parameters(lambda_opt); //Done altready in "compute_f" below by the Gof updater?
		GCV_values(iter) = GS.compute_f(lambda_opt);

		Rprintf("GCV: %f", GCV_values(iter));
		
		iter += 1;
	}

	// Find the minimum GCV and save the related parameters
	UInt min_GCV_pos;
	GCV_values.minCoeff(&min_GCV_pos);

	angle = angles(min_GCV_pos + 1); // GCV_values is shorter than angles due to initialization => index shifted
	intensity = intensities(min_GCV_pos + 1); // GCV_values is shorter than intensities due to initialization => index shifted

	Rprintf("New K found: angle and intensity are %f, %f\n", angle, intensity);

	H.set_K(angle, intensity);

	Rprintf("Parameters updated\n");
	
	// Compute increment
	increment += std::sqrt((angle - angles(0))*(angle - angles(0)));
	increment += std::sqrt((intensity - intensities(0))*(intensity - intensities(0)));
	
	return;
}

template <UInt ORDER, UInt mydim, UInt ndim>
void Parameter_Cascading<ORDER, mydim, ndim>::step_b(void)
{
	// Vectors to store the optimal values for each lambda in lambdas
	VectorXr b1_values(lambdas.size() + 1);
	VectorXr b2_values(lambdas.size() + 1);

	// Initialization
	b1_values(0) = b1;
	b2_values(0) = b2;

	// Vectors to store the GCV values for each lambda in lambdas_
	VectorXr GCV_values(lambdas.size());

	// Parameters for optimization algorithm
	Eigen::Vector2d lower_bound(-std::numeric_limits<Real>::infinity(), -std::numeric_limits<Real>::infinity());
	Eigen::Vector2d upper_bound(std::numeric_limits<Real>::infinity(), std::numeric_limits<Real>::infinity());
	Parameter_Genetic_Algorithm<Eigen::Vector2d> param = {100, lower_bound, upper_bound};

	// Optimal lambda to compute GCV
	Real lambda_opt;

	for (UInt iter = 0; iter < lambdas.size(); ++iter)
	{
		// Optimization step
		auto F = [this, &iter](Eigen::Vector2d x){return H.eval_b(x(0),x(1), lambdas(iter));};
		Eigen::Vector2d init(b1_values(iter), b2_values(iter));

		Genetic_Algorithm<Eigen::Vector2d, Real> opt(F, init, param);
		opt.apply();
		
		// Store the optimal solution
		Eigen::Vector2d opt_sol = opt.get_solution();
		b1_values(iter + 1) = opt_sol(0);
		b2_values(iter + 1) = opt_sol(1);
		
		// Compute GCV with the new parameters
		// Compute GCV with the new parameters
		H.set_b(b1_values(iter + 1), b2_values(iter + 1));

		Carrier<RegressionDataElliptic> carrier = CarrierBuilder<RegressionDataElliptic>::build_plain_carrier(H.getModel().getRegressionData(), H.getModel(), H.getModel().getOptimizationData());
		GCV_Stochastic<Carrier<RegressionDataElliptic>, 1> GS(carrier, true);

		lambda_opt = compute_optimal_lambda(carrier, GS);

		GS.update_parameters(lambda_opt); //Done altready in "compute_f" below by the Gof updater?
		GCV_values(iter) = GS.compute_f(lambda_opt);

		Rprintf("GCV: %f", GCV_values(iter));
		
		iter += 1;
	}

	// Find the minimum GCV and save the related parameters
	UInt min_GCV_pos;
	GCV_values.minCoeff(&min_GCV_pos);

	b1 = b1_values(min_GCV_pos + 1); // GCV_values is shorter than b1_values due to initialization => index shifted
	b2 = b2_values(min_GCV_pos + 1); // GCV_values is shorter than b2_values due to initialization => index shifted

	H.set_b(b1, b2);
	
	// Compute increment
	increment += std::sqrt((b1 - b1_values(0))*(b1 - b1_values(0)));
	increment += std::sqrt((b2 - b2_values(0))*(b2 - b2_values(0)));
	
	return;
}

template <UInt ORDER, UInt mydim, UInt ndim>
void Parameter_Cascading<ORDER, mydim, ndim>::step_c(void)
{
	// Vector to store the optimal values for each lambda in lambdas
	VectorXr c_values(lambdas.size() + 1);

	// Initialization
	c_values(0) = c;

	// Vectors to store the GCV values for each lambda in lambdas
	VectorXr GCV_values(lambdas.size());

	// Parameters for optimization algorithm
	Real lower_bound(-std::numeric_limits<Real>::infinity());
	Real upper_bound(std::numeric_limits<Real>::infinity());
	Parameter_Genetic_Algorithm<Real> param = {100, lower_bound, upper_bound};

	// Optimal lambda to compute GCV
	Real lambda_opt;

	for (UInt iter = 0; iter < lambdas.size(); ++iter)
	{
		// Optimization step
		auto F = [this, &iter](Real x){return H.eval_c(x,lambdas(iter));};
		Real init{c_values(iter)};

		Genetic_Algorithm<Real, Real> opt(F, init, param);
		opt.apply();

		// Store the optimal solution
		Real opt_sol = opt.get_solution();
		c_values(iter + 1) = opt_sol;
		
		// Compute GCV with the new parameters
		H.set_c(c_values(iter + 1));

		Carrier<RegressionDataElliptic> carrier = CarrierBuilder<RegressionDataElliptic>::build_plain_carrier(H.getModel().getRegressionData(), H.getModel(), H.getModel().getOptimizationData());
		GCV_Stochastic<Carrier<RegressionDataElliptic>, 1> GS(carrier, true);

		lambda_opt = compute_optimal_lambda(carrier, GS);

		GS.update_parameters(lambda_opt); //Done altready in "compute_f" below by the Gof updater?
		GCV_values(iter) = GS.compute_f(lambda_opt);

		Rprintf("GCV: %f", GCV_values(iter));
		
		iter += 1;
		
	}

	// Find the minimum GCV and save the related parameters
	UInt min_GCV_pos;
	GCV_values.minCoeff(&min_GCV_pos);

	c = c_values(min_GCV_pos + 1); // GCV_values is shorter than c_values due to initialization => index shifted
	
	H.set_c(c);
	
	// Compute increment
	increment += std::sqrt(c - c_values(0));
	
	return;

}


template <UInt ORDER, UInt mydim, UInt ndim>
void Parameter_Cascading<ORDER, mydim, ndim>::apply(void)
{
	//unsigned int iter = 0u;

	//while(iter < max_iter_parameter_cascading && goOn)
	//{	
	//	++iter;
		
	//	increment = 0.0;

		Rprintf("Step K\n");		
		if(update_K)
			step_K();
		
		Rprintf("Step b\n");
		if(update_b)
			step_b();
			
		Rprintf("Step c\n");
		if(update_c)
			step_c();
		
	//	goOn = increment > tol_parameter_cascading;
		
	//}
	
	//return (iter < max_iter_parameter_cascading);
	return;
}

#endif
#ifndef CHITECH_POINT_REACTOR_KINETICS_H
#define CHITECH_POINT_REACTOR_KINETICS_H

#include "physics/SolverBase/chi_solver.h"
#include "math/chi_math.h"
#include "math/dynamic_matrix.h"
#include "math/dynamic_vector.h"

namespace prk
{
/**General transient solver for point kinetics.

* */
class TransientSolver : public chi_physics::Solver
{
private:
  std::vector<double> lambdas_;
  std::vector<double> betas_;
  double gen_time_;
  double rho_;
  double source_strength_;
  std::string time_integration_;

  size_t num_precursors_;
  chi_math::DynamicMatrix<double> A_, I_;
  chi_math::DynamicVector<double> x_t_, x_tp1_, q_;
  double beta_ = 1.0;
  double period_tph_ = 0.0;

public:
  static chi::InputParameters GetInputParameters();
  explicit TransientSolver(const chi::InputParameters& params);

  void Initialize() override;
  void Execute() override;
  void Step() override;
  void Advance() override;

  chi::ParameterBlock GetInfo(const chi::ParameterBlock& params) const override;

  // Getters and Setters
  double PopulationPrev() const;
  double PopulationNew() const;
  double Period() const;
  double TimePrev() const;
  double TimeNew() const;
  std::vector<double> SolutionPrev() const;
  std::vector<double> SolutionNew() const;

  void SetRho(double value);
};
} // namespace prk

#endif // CHITECH_POINT_REACTOR_KINETICS_H

#include "B_DO_SteadyState/lbs_DO_steady_state.h"
#include "B_DO_SteadyState/SweepChunks/lbs_sweepchunk_pwl.h"
#include "LinearBoltzmannSolvers/A_LBSSolver/Groupset/lbs_groupset.h"

typedef chi_mesh::sweep_management::SweepChunk SweepChunk;

//###################################################################
/**Sets up the sweek chunk for the given discretization_ method.*/
std::shared_ptr<SweepChunk> lbs::DiscOrdSteadyStateSolver::
  SetSweepChunk(LBSGroupset& groupset)
{
  //================================================== Setting up required
  //                                                   sweep chunks
  auto sweep_chunk = std::make_shared<SweepChunkPWL>(
    grid_ptr_,                                    //Spatial grid_ptr_ of cells
    *discretization_,                             //Spatial discretization_
    unit_cell_matrices_,                          //Unit cell matrices
    cell_transport_views_,                        //Cell transport views
    phi_new_local_,                               //Destination phi
    psi_new_local_[groupset.id],                  //Destination psi
    q_moments_local_,                             //Source moments
    groupset,                                     //Reference groupset
    matid_to_xs_map_,                             //Material cross-sections
    num_moments_,
    max_cell_dof_count_);

  return sweep_chunk;
}

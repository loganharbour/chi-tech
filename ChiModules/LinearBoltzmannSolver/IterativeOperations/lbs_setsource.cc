#include "../lbs_linear_boltzmann_solver.h"

#include "ChiTimer/chi_timer.h"

#include <chi_mpi.h>
#include <chi_log.h>

extern ChiMPI& chi_mpi;
extern ChiLog& chi_log;

//###################################################################
/**Sets the source moments for the groups in the current group set.
 *
 * \param group_set_num Identifies the groupset under consideration.
 * \param apply_mat_src Flag indicating whether the material source needs
 *        to be applied to the routine. This is useful for GMRES
 *        since the material source only features during the computing b.
 *        On this note we also need to treat inscattering this way.
 * \param suppress_phi_old Flag indicating whether to suppress phi_old.
 *
 * */
void LinearBoltzmann::Solver::
  SetSource(LBSGroupset& groupset,
            std::vector<double>& destination_q,
            SourceFlags source_flags)
{
  chi_log.LogEvent(source_event_tag,ChiLog::EventType::EVENT_BEGIN);

  const bool apply_mat_src         = (source_flags & APPLY_MATERIAL_SOURCE);
  const bool apply_wgs_scatter_src = (source_flags & APPLY_WGS_SCATTER_SOURCE);
  const bool apply_ags_scatter_src = (source_flags & APPLY_AGS_SCATTER_SOURCE);
  const bool apply_fission_src     = (source_flags & APPLY_WGS_FISSION_SOURCE);

  //================================================== Get group setup
  int gs_i = groupset.groups[0].id;
  int gs_f = groupset.groups.back().id;

  int first_grp = groups.front().id;
  int last_grp = groups.back().id;

  const auto& m_to_ell_em_map = groupset.quadrature->GetMomentToHarmonicsIndexMap();

  std::vector<double> default_zero_src(groups.size(),0.0);

  //================================================== Loop over local cells
  for (const auto& cell : grid->local_cells)
  {
    auto& full_cell_view = cell_transport_views[cell.local_id];

    //=========================================== Obtain cross-section and src
    int cell_matid = cell.material_id;

    int xs_id = matid_to_xs_map[cell_matid];
    int src_id= matid_to_src_map[cell_matid];

    if ((xs_id<0) || (xs_id>=material_xs.size()))
    {
      chi_log.Log(LOG_ALLERROR)
      << "Cross-section lookup error\n";
      exit(EXIT_FAILURE);
    }

    auto xs = material_xs[xs_id];

    //=========================================== Obtain material source
    double* src = default_zero_src.data();
    if ( (src_id >= 0) && (apply_mat_src) )
      src = material_srcs[src_id]->source_value_g.data();

    //=========================================== Loop over dofs
    int num_nodes = full_cell_view.NumNodes();
    for (int i=0; i <num_nodes; i++)
    {
      for (int m=0; m<num_moments; ++m)
      {
        unsigned int ell = m_to_ell_em_map[m].ell;

        size_t ir      = full_cell_view.MapDOF(i,m,0);
        double* q_mom    = &destination_q[ir];
        double* phi_oldp = &phi_old_local[ir];

        //============================= Loop over groupset groups
        for (int g=gs_i; g<=gs_f; g++)
        {
          if ( apply_mat_src && (ell == 0) )
            q_mom[g] += src[g];

          double inscat_g = 0.0;
          //====================== Apply across-groupset scattering
          if ((ell < xs->transfer_matrices.size()) && (apply_ags_scatter_src) )
          {
            size_t num_transfers = xs->transfer_matrices[ell].rowI_indices[g].size();
            for (int t=0; t<num_transfers; t++)
            {
              size_t gprime = xs->transfer_matrices[ell].rowI_indices[g][t];
              if ((gprime < gs_i) || (gprime > gs_f))
              {
                double sigma_sm = xs->transfer_matrices[ell].rowI_values[g][t];
                inscat_g += sigma_sm * phi_oldp[gprime];
              }
            }
          }//if moment avail

          //====================== Apply within-groupset scattering
          if ((ell < xs->transfer_matrices.size()) && (apply_wgs_scatter_src) )
          {
            size_t num_transfers = xs->transfer_matrices[ell].rowI_indices[g].size();
            for (int t=0; t<num_transfers; t++)
            {
              size_t gprime = xs->transfer_matrices[ell].rowI_indices[g][t];
              if ((gprime >= gs_i) && (gprime<=gs_f))
              {
                double sigma_sm = xs->transfer_matrices[ell].rowI_values[g][t];
                inscat_g += sigma_sm * phi_oldp[gprime];
              }
            }
          }//if moment avail

          q_mom[g] += inscat_g;

          //====================== Apply accross-groupset fission
          if ( (ell == 0) and (apply_fission_src) )
          {
            for (size_t gprime=first_grp; gprime<=last_grp; ++gprime)
            {
              if ((gprime < gs_i) || (gprime > gs_f))
              {
                q_mom[g] += xs->chi[g] *
                            xs->nu_sigma_f[gprime] *
                            phi_oldp[gprime];
              }
            }//for gprime
          }//if zeroth moment

          //====================== Apply within-groupset fission
          if ( (ell == 0) and (apply_fission_src) )
          {
            for (size_t gprime=first_grp; gprime<=last_grp; ++gprime)
            {
              if ((gprime >= gs_i) && (gprime<=gs_f))
              {
                q_mom[g] += xs->chi[g] *
                            xs->nu_sigma_f[gprime] *
                            phi_oldp[gprime];
              }
            }//for gprime
          }//if zeroth moment
        }//for g
      }//for m
    }//for dof i
  }//for cell

  chi_log.LogEvent(source_event_tag,ChiLog::EventType::EVENT_END);
}
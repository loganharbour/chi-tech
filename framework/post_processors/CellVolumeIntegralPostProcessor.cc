#include "CellVolumeIntegralPostProcessor.h"

#include "event_system/Event.h"

#include "physics/FieldFunction/fieldfunction_gridbased.h"
#include "math/SpatialDiscretization/spatial_discretization.h"
#include "math/SpatialDiscretization/FiniteElement/finite_element.h"
#include "mesh/MeshContinuum/chi_meshcontinuum.h"
#include "mesh/LogicalVolume/LogicalVolume.h"

#include "ChiObjectFactory.h"

namespace chi
{

RegisterChiObject(chi, CellVolumeIntegralPostProcessor);

// ##################################################################
InputParameters CellVolumeIntegralPostProcessor::GetInputParameters()
{
  InputParameters params = PostProcessor::GetInputParameters();
  params += chi_physics::GridBasedFieldFunctionInterface::GetInputParameters();
  params += chi_mesh::LogicalVolumeInterface::GetInputParameters();

  // clang-format off
  params.SetGeneralDescription(
  "Computes the volumetric integral of a field-function as a scalar.");
  // clang-format on
  params.SetDocGroup("doc_PostProcessors");

  params.AddOptionalParameter(
    "compute_volume_average",
    false,
    "Flag, when true will compute the volume average of the post-processor.");

  return params;
}

// ##################################################################
CellVolumeIntegralPostProcessor::CellVolumeIntegralPostProcessor(
  const InputParameters& params)
  : PostProcessor(params, PPType::SCALAR),
    chi_physics::GridBasedFieldFunctionInterface(params),
    chi_mesh::LogicalVolumeInterface(params),
    compute_volume_average_(
      params.GetParamValue<bool>("compute_volume_average"))
{
  value_ = ParameterBlock("", 0.0);
}

// ##################################################################
void CellVolumeIntegralPostProcessor::Initialize()
{
  const auto* grid_field_function = GetGridBasedFieldFunction();

  ChiLogicalErrorIf(not grid_field_function,
                    "Attempted to access invalid field"
                    "function");

  const auto& grid = grid_field_function->SDM().Grid();

  const auto* logical_volume_ptr_ = GetLogicalVolume();
  if (logical_volume_ptr_ == nullptr)
  {
    cell_local_ids_.reserve(grid.local_cells.size());
    for (const auto& cell : grid.local_cells)
      cell_local_ids_.push_back(cell.local_id_);
  }
  else
  {
    for (const auto& cell : grid.local_cells)
      if (logical_volume_ptr_->Inside(cell.centroid_))
        cell_local_ids_.push_back(cell.local_id_);
  }

  initialized_ = true;
}

// ##################################################################
void CellVolumeIntegralPostProcessor::Execute(const Event& event_context)
{
  if (not initialized_) Initialize();

  const auto* grid_field_function = GetGridBasedFieldFunction();

  ChiLogicalErrorIf(not grid_field_function,
                    "Attempted to access invalid field"
                    "function");

  const auto& ref_ff = *grid_field_function;
  const auto& sdm = ref_ff.SDM();
  const auto& grid = sdm.Grid();

  const auto& uk_man = ref_ff.UnkManager();
  const auto uid = 0;
  const auto cid = 0;

  const auto field_data = ref_ff.GetGhostedFieldVector();

  double local_integral = 0.0;
  double local_volume = 0.0;
  for (const uint64_t cell_local_id : cell_local_ids_)
  {
    const auto& cell = grid.local_cells[cell_local_id];
    const auto& cell_mapping = sdm.GetCellMapping(cell);
    const size_t num_nodes = cell_mapping.NumNodes();
    const auto qp_data = cell_mapping.MakeVolumeQuadraturePointData();

    std::vector<double> node_dof_values(num_nodes, 0.0);
    for (size_t i = 0; i < num_nodes; ++i)
    {
      const int64_t imap = sdm.MapDOFLocal(cell, i, uk_man, uid, cid);
      node_dof_values[i] = field_data[imap];
    } // for i

    for (const size_t qp : qp_data.QuadraturePointIndices())
    {
      // phi_h = sum_j b_j phi_j
      double ff_value = 0.0;
      for (size_t j = 0; j < num_nodes; ++j)
        ff_value += qp_data.ShapeValue(j, qp) * node_dof_values[j];

      local_integral += ff_value * qp_data.JxW(qp);
      local_volume += qp_data.JxW(qp);
    } // for qp
  }   // for cell-id

  double globl_integral;
  MPI_Allreduce(&local_integral, // sendbuf
                &globl_integral, // recvbuf
                1,
                MPI_DOUBLE,     // count + datatype
                MPI_SUM,        // operation
                Chi::mpi.comm); // communicator
  if (not compute_volume_average_) value_ = ParameterBlock("", globl_integral);
  else
  {
    double globl_volume;
    MPI_Allreduce(&local_volume, // sendbuf
                  &globl_volume, // recvbuf
                  1,
                  MPI_DOUBLE,     // count + datatype
                  MPI_SUM,        // operation
                  Chi::mpi.comm); // communicator

    value_ = ParameterBlock("", globl_integral / globl_volume);
  }
}

} // namespace chi
#include "pwl_base.h"

#include "mesh/MeshContinuum/chi_meshcontinuum.h"

#include "math/PETScUtils/petsc_utils.h"

//###################################################################
/**Get the number of local degrees-of-freedom.*/
size_t chi_math::SpatialDiscretization_PWLBase::
GetNumLocalDOFs(const chi_math::UnknownManager& unknown_manager) const
{
  unsigned int N = unknown_manager.GetTotalUnknownStructureSize();

  return local_base_block_size_ * N;
}

//###################################################################
/**Get the number of global degrees-of-freedom.*/
size_t chi_math::SpatialDiscretization_PWLBase::
GetNumGlobalDOFs(const chi_math::UnknownManager& unknown_manager) const
{
  unsigned int N = unknown_manager.GetTotalUnknownStructureSize();

  return globl_base_block_size_ * N;
}

size_t chi_math::SpatialDiscretization_PWLBase::
  GetCellNumNodes(const chi_mesh::Cell& cell) const
{
  return cell.vertex_ids_.size();
}

std::vector<chi_mesh::Vector3> chi_math::SpatialDiscretization_PWLBase::
  GetCellNodeLocations(const chi_mesh::Cell& cell) const
{
  std::vector<chi_mesh::Vector3> node_locations;
  node_locations.reserve(cell.vertex_ids_.size());

  for (auto& vid : cell.vertex_ids_)
    node_locations.emplace_back(ref_grid_.vertices[vid]);

  return node_locations;
}

const chi_math::finite_element::UnitIntegralData&
chi_math::SpatialDiscretization_PWLBase::
  GetUnitIntegrals(const chi_mesh::Cell& cell)
{
  if (ref_grid_.IsCellLocal(cell.global_id_))
  {
    if (integral_data_initialized_)
      return fe_unit_integrals_.at(cell.local_id_);
    else
    {
      const auto& cell_mapping = GetCellMapping(cell);
      scratch_intgl_data_.Reset();
      cell_mapping.ComputeUnitIntegrals(scratch_intgl_data_);
      return scratch_intgl_data_;
    }
  }
  else
  {
    if (nb_integral_data_initialized_)
      return nb_fe_unit_integrals_.at(cell.global_id_);
    else
    {
      const auto& cell_mapping = GetCellMapping(cell);
      cell_mapping.ComputeUnitIntegrals(scratch_intgl_data_);
      return scratch_intgl_data_;
    }
  }
}

const chi_math::finite_element::InternalQuadraturePointData&
chi_math::SpatialDiscretization_PWLBase::
  GetQPData_Volumetric(const chi_mesh::Cell& cell)
{
  if (ref_grid_.IsCellLocal(cell.global_id_))
  {
    if (qp_data_initialized_)
      return fe_vol_qp_data_.at(cell.local_id_);
    else
    {
      const auto& cell_mapping = GetCellMapping(cell);
      cell_mapping.InitializeVolumeQuadraturePointData(scratch_vol_qp_data_);
      return scratch_vol_qp_data_;
    }
  }
  else
  {
    if (nb_qp_data_initialized_)
      return nb_fe_vol_qp_data_.at(cell.global_id_);
    else
    {
      const auto& cell_mapping = GetCellMapping(cell);
      cell_mapping.InitializeVolumeQuadraturePointData(scratch_vol_qp_data_);
      return scratch_vol_qp_data_;
    }
  }
}

const chi_math::finite_element::FaceQuadraturePointData&
chi_math::SpatialDiscretization_PWLBase::
  GetQPData_Surface(const chi_mesh::Cell& cell,
                    const unsigned int face)
{
  if (ref_grid_.IsCellLocal(cell.global_id_))
  {
    if (qp_data_initialized_)
    {
      const auto& face_data = fe_srf_qp_data_.at(cell.local_id_);

      return face_data.at(face);
    }
    else
    {
      const auto& cell_mapping = GetCellMapping(cell);
      cell_mapping.InitializeFaceQuadraturePointData(face, scratch_face_qp_data_);
      return scratch_face_qp_data_;
    }
  }
  else
  {
    if (nb_qp_data_initialized_)
    {
      const auto& face_data = nb_fe_srf_qp_data_.at(cell.global_id_);

      return face_data.at(face);
    }
    else
    {
      const auto& cell_mapping = GetCellMapping(cell);
      cell_mapping.InitializeFaceQuadraturePointData(face, scratch_face_qp_data_);
      return scratch_face_qp_data_;
    }
  }
}
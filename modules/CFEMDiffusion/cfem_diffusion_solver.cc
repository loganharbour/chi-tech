#include "cfem_diffusion_solver.h"

#include "chi_runtime.h"
#include "chi_log.h"
#include "utils/chi_timer.h"

#include "mesh/MeshHandler/chi_meshhandler.h"
#include "mesh/MeshContinuum/chi_meshcontinuum.h"

#include "cfem_diffusion_bndry.h"

#include "physics/FieldFunction/fieldfunction_gridbased.h"

#include "math/SpatialDiscretization/FiniteElement/PiecewiseLinear/pwlc.h"

//============================================= constructor
cfem_diffusion::Solver::Solver(const std::string& in_solver_name):
  chi_physics::Solver(in_solver_name, { {"max_iters", int64_t(500)   },
                                        {"residual_tolerance", 1.0e-2}})
{}

//============================================= destructor
cfem_diffusion::Solver::~Solver()
{
  VecDestroy(&x_);
  VecDestroy(&b_);
  MatDestroy(&A_);
}

//============================================= Initialize
void cfem_diffusion::Solver::Initialize()
{
  const std::string fname = "cfem_diffusion::Solver::Initialize";
  Chi::log.Log() << "\n"
                     << Chi::program_timer.GetTimeString() << " "
                     << TextName() << ": Initializing CFEM Diffusion solver ";

  //============================================= Get grid
  grid_ptr_ = chi_mesh::GetCurrentHandler().GetGrid();
  const auto& grid = *grid_ptr_;
  if (grid_ptr_ == nullptr)
    throw std::logic_error(std::string(__PRETTY_FUNCTION__) +
                           " No grid defined.");

  Chi::log.Log() << "Global num cells: " << grid.GetGlobalNumberOfCells();

  //============================================= BIDs
  auto globl_unique_bndry_ids = grid.GetDomainUniqueBoundaryIDs();

  const auto& grid_boundary_id_map = grid_ptr_->GetBoundaryIDMap();
  for (uint64_t bndry_id : globl_unique_bndry_ids)
  {
    if (grid_boundary_id_map.count(bndry_id) == 0)
      throw std::logic_error(fname + ": Boundary id " +
                             std::to_string(bndry_id) + " does not have a name-assignment.");

    const auto& bndry_name = grid_boundary_id_map.at(bndry_id);
    if (boundary_preferences_.find(bndry_name) != boundary_preferences_.end())
    {
      BoundaryInfo bndry_info = boundary_preferences_.at(bndry_name);
      auto& bndry_vals = bndry_info.second;
      switch (bndry_info.first)
      {
        case BoundaryType::Reflecting:
        {
          boundaries_.insert(std::make_pair(
            bndry_id,Boundary{BoundaryType::Reflecting, {0., 0., 0.}}));
          Chi::log.Log() << "Boundary " << bndry_name << " set to reflecting.";
          break;
        }
        case BoundaryType::Dirichlet:
        {
          if (bndry_vals.empty()) bndry_vals.resize(1,0.0);
          boundaries_.insert(std::make_pair(
            bndry_id,Boundary{BoundaryType::Dirichlet, {bndry_vals[0], 0., 0.}}));
          Chi::log.Log() << "Boundary " << bndry_name << " set to dirichlet.";
          break;
        }
        case BoundaryType::Robin:
        {
          if (bndry_vals.size()!=3)
            throw std::logic_error(std::string(__PRETTY_FUNCTION__) +
                           " Robin needs 3 values in bndry vals.");
          boundaries_.insert(std::make_pair(
            bndry_id,Boundary{BoundaryType::Robin, {bndry_vals[0],
                                                    bndry_vals[1],
                                                    bndry_vals[2]}}));
          Chi::log.Log() << "Boundary " << bndry_name
          << " set to robin." << bndry_vals[0]<<","
                              << bndry_vals[1]<<","<<bndry_vals[2];
          break;
        }
        case BoundaryType::Vacuum:
        {
          boundaries_.insert(std::make_pair(
            bndry_id,Boundary{BoundaryType::Robin, {0.25, 0.5, 0.}}));
          Chi::log.Log() << "Boundary " << bndry_name << " set to vacuum.";
          break;
        }
        case BoundaryType::Neumann:
        {
          if (bndry_vals.size()!=3) 
            throw std::logic_error(std::string(__PRETTY_FUNCTION__) +
                           " Neumann needs 3 values in bndry vals.");
          boundaries_.insert(std::make_pair(
            bndry_id,Boundary{BoundaryType::Robin, {0., bndry_vals[0],
                                                       bndry_vals[1]}}));
          Chi::log.Log() << "Boundary " << bndry_name
          << " set to neumann." << bndry_vals[0];
          break;
        }
      }//switch boundary type
    }
    else
    {
      boundaries_.insert(std::make_pair(
        bndry_id,Boundary{BoundaryType::Dirichlet, {0., 0., 0.}}));
      Chi::log.Log0Verbose1()
        << "No boundary preference found for boundary index " << bndry_name
        << "Dirichlet boundary added with zero boundary value.";
    }
  }//for bndry
  
  //============================================= Make SDM
  sdm_ptr_ = chi_math::SpatialDiscretization_PWLC::New(*grid_ptr_);
  const auto& sdm = *sdm_ptr_;
 
  const auto& OneDofPerNode = sdm.UNITARY_UNKNOWN_MANAGER;
  num_local_dofs_ = sdm.GetNumLocalDOFs(OneDofPerNode);
  num_globl_dofs_ = sdm.GetNumGlobalDOFs(OneDofPerNode);

  Chi::log.Log() << "Num local DOFs: " << num_local_dofs_;
  Chi::log.Log() << "Num globl DOFs: " << num_globl_dofs_;

  //============================================= Initializes Mats and Vecs
  const auto n = static_cast<int64_t>(num_local_dofs_);
  const auto N = static_cast<int64_t>(num_globl_dofs_);

  A_ = chi_math::PETScUtils::CreateSquareMatrix(n, N);
  x_ = chi_math::PETScUtils::CreateVector(n, N);
  b_ = chi_math::PETScUtils::CreateVector(n, N);
 
  std::vector<int64_t> nodal_nnz_in_diag;
  std::vector<int64_t> nodal_nnz_off_diag;
  sdm.BuildSparsityPattern(nodal_nnz_in_diag,nodal_nnz_off_diag, OneDofPerNode);
 
  chi_math::PETScUtils::InitMatrixSparsity(A_,
                                           nodal_nnz_in_diag,
                                           nodal_nnz_off_diag);

  if (field_functions_.empty())
  {
    std::string solver_name;
    if (not TextName().empty()) solver_name = TextName() + "-";

    std::string text_name = solver_name + "phi";

    using namespace chi_math;
    auto initial_field_function =
      std::make_shared<chi_physics::FieldFunctionGridBased>(
          text_name,                     //Text name
          sdm_ptr_,                      //Spatial Discretization
          Unknown(UnknownType::SCALAR)); //Unknown/Variable

    field_functions_.push_back(initial_field_function);
    Chi::field_function_stack.push_back(initial_field_function);
  }//if not ff set

}//end initialize

//========================================================== Execute
void cfem_diffusion::Solver::Execute()
{
  Chi::log.Log() << "\nExecuting CFEM Diffusion solver";

  const auto& grid = *grid_ptr_;
  const auto& sdm  = *sdm_ptr_;

  lua_State* L = Chi::console.GetConsoleState();

  //============================================= Assemble the system
  Chi::log.Log() << "Assembling system: ";
  for (const auto& cell : grid.local_cells)
  {
    const auto& cell_mapping = sdm.GetCellMapping(cell);
    const auto  qp_data      = cell_mapping.MakeVolumeQuadraturePointData();
 
    const auto imat  = cell.material_id_;
    const size_t num_nodes = cell_mapping.NumNodes();
    MatDbl Acell(num_nodes, VecDbl(num_nodes, 0.0));
    VecDbl cell_rhs(num_nodes, 0.0);
 
    for (size_t i=0; i<num_nodes; ++i)
    {
      for (size_t j=0; j<num_nodes; ++j)
      {
        double entry_aij = 0.0;
        for (size_t qp : qp_data.QuadraturePointIndices())
        {
          entry_aij +=
            (
              CallLua_iXYZFunction(L, "D_coef",imat,qp_data.QPointXYZ(qp))  *
              qp_data.ShapeGrad(i, qp).Dot(qp_data.ShapeGrad(j, qp))
              +
              CallLua_iXYZFunction(L, "Sigma_a",imat,qp_data.QPointXYZ(qp))  *
              qp_data.ShapeValue(i, qp) * qp_data.ShapeValue(j, qp)
            )
            *
            qp_data.JxW(qp);
        }//for qp
        Acell[i][j] = entry_aij;
      }//for j
      for (size_t qp : qp_data.QuadraturePointIndices())
        cell_rhs[i] += CallLua_iXYZFunction(L,"Q_ext",imat,qp_data.QPointXYZ(qp)) * qp_data.ShapeValue(i, qp) * qp_data.JxW(qp);
    }//for i
 
    //======================= Flag nodes for being on a boundary
    std::vector<int> dirichlet_count(num_nodes, 0);
    std::vector<double> dirichlet_value(num_nodes, 0.0);

    const size_t num_faces = cell.faces_.size();
    for (size_t f=0; f<num_faces; ++f)
    {
      const auto& face = cell.faces_[f];
      // not a boundary face
	    if (face.has_neighbor_) continue;
	  
      const auto& bndry = boundaries_[face.neighbor_id_];

      // Robin boundary
      if (bndry.type_ == BoundaryType::Robin)
      { 
        const auto  qp_face_data = cell_mapping.MakeFaceQuadraturePointData( f );
        const size_t num_face_nodes = face.vertex_ids_.size();

        const auto& aval = bndry.values_[0];
        const auto& bval = bndry.values_[1];
        const auto& fval = bndry.values_[2];

        Chi::log.Log0Verbose1() << "Boundary  set as Robin with a,b,f = ("
                    << aval << ","
                    << bval << ","
                    << fval << ") ";
        // true Robin when a!=0, otherwise, it is a Neumann:
        // Assert if b=0
        if (std::fabs(bval) < 1e-8)
          throw std::logic_error("if b=0, this is a Dirichlet BC, not a Robin BC");
        
        // loop over nodes of that face
        for (size_t fi=0; fi<num_face_nodes; ++fi)
        {
          const uint i = cell_mapping.MapFaceNode(f,fi);
            
          double entry_rhsi = 0.0;
          for (size_t qp : qp_face_data.QuadraturePointIndices() )
            entry_rhsi +=  qp_face_data.ShapeValue(i, qp) * qp_face_data.JxW(qp);
          cell_rhs[i] +=  fval / bval * entry_rhsi;
            
          // only do this part if true Robin (i.e., a!=0)
          if (std::fabs(aval) > 1.0e-8)
          {
            for (size_t fj=0; fj<num_face_nodes; ++fj)
            {
              const uint j = cell_mapping.MapFaceNode(f,fj);
          
              double entry_aij = 0.0;
              for (size_t qp : qp_face_data.QuadraturePointIndices())
                entry_aij +=  qp_face_data.ShapeValue(i, qp) *qp_face_data.ShapeValue(j, qp)
                                * qp_face_data.JxW(qp);
              Acell[i][j] += aval / bval * entry_aij;
            }//for fj
          }//end true Robin
        }//for fi
      }//if Robin
	  
	    // Dirichlet boundary
	    if (bndry.type_ == BoundaryType::Dirichlet)
      {
 		    const size_t num_face_nodes = face.vertex_ids_.size();

        const auto& boundary_value = bndry.values_[0];

        // loop over nodes of that face
        for (size_t fi=0; fi<num_face_nodes; ++fi)
        {
          const uint i = cell_mapping.MapFaceNode(f,fi);
          dirichlet_count[i] += 1;
          dirichlet_value[i] += boundary_value;
        }//for fi
      }//if Dirichlet
	  
    }//for face f
 
    //======================= Develop node mapping
    std::vector<int64_t> imap(num_nodes, 0); //node-mapping
    for (size_t i=0; i<num_nodes; ++i)
      imap[i] = sdm.MapDOF(cell, i);
 
    //======================= Assembly into system
    for (size_t i=0; i<num_nodes; ++i)
    {
      if (dirichlet_count[i]>0) //if Dirichlet boundary node
      {
        MatSetValue(A_, imap[i], imap[i], 1.0, ADD_VALUES);
		    // because we use CFEM, a given node is common to several faces
		    const double aux = dirichlet_value[i]/dirichlet_count[i];
        VecSetValue(b_, imap[i], aux, ADD_VALUES);
      }
      else
      {
        for (size_t j=0; j<num_nodes; ++j)
        {
          if (dirichlet_count[j]==0) // not related to a dirichlet node
            MatSetValue(A_, imap[i], imap[j], Acell[i][j], ADD_VALUES);
		      else
          {
		        const double aux = dirichlet_value[j]/dirichlet_count[j];
			      cell_rhs[i] -= Acell[i][j]*aux;
          }
        }//for j
        VecSetValue(b_, imap[i], cell_rhs[i], ADD_VALUES);
      }
    }//for i
  }//for cell

  Chi::log.Log() << "Global assembly";
 
  MatAssemblyBegin(A_, MAT_FINAL_ASSEMBLY);
  MatAssemblyEnd(A_, MAT_FINAL_ASSEMBLY);
  VecAssemblyBegin(b_);
  VecAssemblyEnd(b_);

  Chi::log.Log() << "Done global assembly";

  //============================================= Create Krylov Solver
  Chi::log.Log() << "Solving: ";
  auto petsc_solver =
    chi_math::PETScUtils::CreateCommonKrylovSolverSetup(
        A_,              //Matrix
        TextName(),      //Solver name
        KSPCG,           //Solver type
        PCGAMG,          //Preconditioner type
        basic_options_("residual_tolerance").FloatValue(),  //Relative residual tolerance
        basic_options_("max_iters").IntegerValue()          //Max iterations
    );
 
  //============================================= Solve
  KSPSolve(petsc_solver.ksp, b_, x_);

  UpdateFieldFunctions();

  Chi::log.Log() << "Done solving";

}
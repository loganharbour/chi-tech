--############################################### Setup mesh
chiMeshHandlerCreate()
 
mesh={}
N=40
L=1
xmin = 0
dx = L/N
for i=1,(N+1) do
    k=i-1
    mesh[i] = xmin + k*dx
end
 
chiMeshCreateUnpartitioned2DOrthoMesh(mesh,mesh)
chiVolumeMesherExecute();
 
--############################################### Set Material IDs
chiVolumeMesherSetMatIDToAll(0)

-- governing law: -(u_xx + u_yy) = q, on domain [0,1]x[0,1]
-- when the exact solution is chosen u(x,y) = sin(pi.x) * sin(pi.y)
-- this automatically gives:
--    boundary = zero-Dirichlet on all 4 sides
--    volumetric source term: q(,x) = 2*pi*pi * sin(pi.x) * sin(pi.y)
-- the factor 2 is the dim of the problem

function D_coef(i,x,y,z)
    return 1.0
end
function Q_ext(i,x,y,z)
    return 2.*math.pi*math.pi * math.sin(math.pi*x) * math.sin(math.pi*y)
end
function Sigma_a(i,x,y,z)
    return 0.0
end

-- Setboundary IDs
-- xmin,xmax,ymin,ymax,zmin,zmax
e_vol = chi_mesh.RPPLogicalVolume.Create({xmin=0.99999,xmax=1000.0  , infy=true, infz=true})
w_vol = chi_mesh.RPPLogicalVolume.Create({xmin=-1000.0,xmax=-0.99999, infy=true, infz=true})
n_vol = chi_mesh.RPPLogicalVolume.Create({ymin=0.99999,ymax=1000.0  , infx=true, infz=true})
s_vol = chi_mesh.RPPLogicalVolume.Create({ymin=-1000.0,ymax=-0.99999, infx=true, infz=true})

e_bndry = 0
w_bndry = 1
n_bndry = 2
s_bndry = 3

chiVolumeMesherSetProperty(BNDRYID_FROMLOGICAL,e_vol,e_bndry)
chiVolumeMesherSetProperty(BNDRYID_FROMLOGICAL,w_vol,w_bndry)
chiVolumeMesherSetProperty(BNDRYID_FROMLOGICAL,n_vol,n_bndry)
chiVolumeMesherSetProperty(BNDRYID_FROMLOGICAL,s_vol,s_bndry)

--chiMeshHandlerExportMeshToVTK("Mesh")

--############################################### Add material properties
--#### CFEM solver
phys1 = chiCFEMDiffusionSolverCreate()

chiCFEMDiffusionSetBCProperty(phys1,"boundary_type",e_bndry,"dirichlet",0.0)
chiCFEMDiffusionSetBCProperty(phys1,"boundary_type",w_bndry,"dirichlet",0.0)
chiCFEMDiffusionSetBCProperty(phys1,"boundary_type",n_bndry,"dirichlet",0.0)
chiCFEMDiffusionSetBCProperty(phys1,"boundary_type",s_bndry,"dirichlet",0.0)

chiSolverInitialize(phys1)
chiSolverExecute(phys1)

--############################################### Get field functions
fflist,count = chiSolverGetFieldFunctionList(phys1)

--############################################### Export VTU
if (master_export == nil) then
    chiExportFieldFunctionToVTK(fflist[1],"CFEMDiff2D_analytic_coef2","flux")
end

--############################################### Volume integrations

--############################################### PostProcessors
chi.AggregateNodalValuePostProcessor.Create
({
    name = "maxval",
    field_function = math.floor(fflist[1]),
    operation = "max"
})
chi.ExecutePostProcessors({"maxval"})


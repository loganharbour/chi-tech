#include "lbs_linear_boltzman_solver.h"

#include <sys/stat.h>
#include <fstream>

#include <chi_log.h>
#include <chi_mpi.h>
extern ChiLog chi_log;
extern ChiMPI chi_mpi;

//###################################################################
/**Writes phi_old to restart file.*/
void LinearBoltzman::Solver::WriteRestartData(std::string folder_name,
                                              std::string file_base)
{
  typedef struct stat Stat;
  Stat st;

  //======================================== Make sure folder exists
  if (chi_mpi.location_id == 0)
  {
    if (stat(folder_name.c_str(),&st) != 0) //if not exist, make it
      if ( (mkdir(folder_name.c_str(),S_IRWXU | S_IRWXG | S_IRWXO) != 0) and
           (errno != EEXIST) )
      {
        chi_log.Log(LOG_0WARNING)
          << "Failed to create restart directory: " << folder_name;
        return;
      }
  }

  MPI_Barrier(MPI_COMM_WORLD);

  //======================================== Create files
  //This step might fail for specific locations and
  //can create quite a messy output if we print it all.
  //We also need to consolidate the error to determine if
  //the process as whole succeeded.
  bool location_succeeded = true;
  char location_cstr[20];
  sprintf(location_cstr,"%d.r",chi_mpi.location_id);

  std::string file_name = folder_name + std::string("/") +
                          file_base + std::string(location_cstr);

  std::ofstream ofile;
  ofile.open(file_name, std::ios::out | std::ios::binary | std::ios::trunc);

  if (not ofile.is_open())
  {
    chi_log.Log(LOG_ALLERROR)
      << "Failed to create restart file: " << file_name;
    ofile.close();
    location_succeeded = false;
  }
  else
  {
    size_t phi_old_size = phi_old_local.size();
    ofile.write((char*)&phi_old_size, sizeof(size_t));
    for (auto val : phi_old_local)
      ofile.write((char*)&val, sizeof(double));

    ofile.close();
  }

  //======================================== Wait for all processes
  //                                         then check success status
  MPI_Barrier(MPI_COMM_WORLD);
  bool global_succeeded = true;
  MPI_Allreduce(&location_succeeded,   //Send buffer
                &global_succeeded,     //Recv buffer
                1,                     //count
                MPI_CXX_BOOL,          //Data type
                MPI_LAND,              //Operation - Logical and
                MPI_COMM_WORLD);       //Communicator

  chi_log.Log(LOG_0) << "Successfully wrote restart data: " << file_name;
}

//###################################################################
/**Writes phi_old to restart file.*/
void LinearBoltzman::Solver::ReadRestartData(std::string folder_name,
                                              std::string file_base)
{
  MPI_Barrier(MPI_COMM_WORLD);
  char location_cstr[20];
  sprintf(location_cstr,"%d.r",chi_mpi.location_id);

  std::string file_name = folder_name + std::string("/") +
                          file_base + std::string(location_cstr);

  std::ifstream ifile;
  ifile.open(file_name, std::ios::in | std::ios::binary );

  if (not ifile.is_open())
  {
    chi_log.Log(LOG_ALLERROR)
      << "Failed to read restart file: " << file_name;
    ifile.close();
    return;
  }

  size_t number_of_unknowns;
  ifile.read((char*)&number_of_unknowns, sizeof(size_t));

  if (number_of_unknowns != phi_old_local.size())
  {
    chi_log.Log(LOG_ALLERROR)
      << "Failed to read restart file: " << file_name
      << " number of unknowns not equal. Expected "
      << phi_old_local.size() << ". Available: " << number_of_unknowns;
    ifile.close();
    return;
  }

  std::vector<double> temp_phi_old(phi_old_local.size(),0.0);

  size_t v=0;
  while (not ifile.eof())
  {
    ifile.read((char*)&temp_phi_old[v], sizeof(double));
    ++v;
  }

  if (v != (number_of_unknowns+1))
  {
    chi_log.Log(LOG_ALLERROR)
      << "Failed to read restart file: " << file_name
      << " number unknowns read " << v
      << " not equal to desired amount " << number_of_unknowns;
    ifile.close();
    return;
  } else
  {
    phi_old_local = std::move(temp_phi_old);
  }

  ifile.close();
  chi_log.Log(LOG_0) << "Successfully read restart data.";
}
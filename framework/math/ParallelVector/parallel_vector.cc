#include "parallel_vector.h"

#include "chi_mpi_utils_map_all2all.h"
#include "data_types/byte_array.h"

#include "chi_log.h"
#include "chi_log_exceptions.h"

#include <sstream>
#include <stdexcept>


namespace chi_math
{

ParallelVector::ParallelVector(const uint64_t local_size,
                               const uint64_t global_size,
                               const MPI_Comm communicator)
  : local_size_(local_size),
    global_size_(global_size),
    comm_(communicator)
{
  // Get the processor ID and the number of processors
  MPI_Comm_rank(comm_, &location_id_);
  MPI_Comm_size(comm_, &process_count_);

  DefineParallelStructure();
  values_.assign(local_size_, 0.0);
}


std::vector<double> ParallelVector::MakeLocalVector()
{
  return std::vector<double>(values_.begin(),
                             values_.begin() + local_size_);
}


double ParallelVector::operator[](const int64_t local_id) const
{
  ChiInvalidArgumentIf(
      local_id < 0 or local_id >= values_.size(),
      "Invalid local index provided.");

  return values_[local_id];
}


double& ParallelVector::operator[](const int64_t local_id)
{
  ChiInvalidArgumentIf(
      local_id < 0 or local_id >= values_.size(),
      "Invalid local index provided.");

  return values_[local_id];
}


void ParallelVector::Set(const std::vector<double>& local_vector)
{
  ChiInvalidArgumentIf(local_vector.size() != local_size_,
                       "Incompatible local vector size.");

  values_ = local_vector;
}


void ParallelVector::SetValue(const int64_t global_id,
                              const double value,
                              const VecOpType op_type)
{
  ChiInvalidArgumentIf(
      global_id < 0 or global_id >= global_size_,
      "Invalid global index encountered. Global indices "
      "must be in the range [0, this->GlobalSize()].");

  auto& op_cache = op_type == VecOpType::SET_VALUE ?
                   set_cache_ : add_cache_;
  op_cache.emplace_back(global_id, value);

}


void ParallelVector::SetValues(const std::vector<int64_t>& global_ids,
                               const std::vector<double>& values,
                               const VecOpType op_type)
{
  ChiInvalidArgumentIf(global_ids.size() != values.size(),
                       "Size mismatch between indices and values.");

  auto& op_cache = op_type == VecOpType::SET_VALUE ?
                   set_cache_ : add_cache_;
  for (size_t i = 0; i < global_ids.size(); ++i)
  {
    const auto& global_id = global_ids[i];
    ChiInvalidArgumentIf(
        global_id < 0 or global_id >= global_size_,
        "Invalid global index encountered. Global indices "
        "must be in the range [0, this->GlobalSize()].");
    op_cache.emplace_back(global_id, values[i]);
  }
}


void ParallelVector::Assemble()
{
  // Define the local operation mode.
  // 0=Do Nothing, 1=Set, 2=Add, 3=INVALID (mixed set/add ops)
  const short local_mode =
      short(not set_cache_.empty()) + short(not add_cache_.empty()) * 2;
  ChiLogicalErrorIf(local_mode == 3, "Invalid operation mode.");

  // Now, determine the global operation mode
  short global_mode;
  MPI_Allreduce(&local_mode, &global_mode, 1,
                MPI_SHORT, MPI_MAX, comm_);

  // If the mode is to do nothing, exit
  if (global_mode == 0)
    return;

  // Next, ensure that all operation types are compatible
  ChiLogicalErrorIf(
      local_mode != 0 and local_mode != global_mode,
      "The operation on each process must be either 0 (do nothing),"
      "or the same across all processes.");

  // Now, store the global operation type and get the appropriate cache
  using OpType = VecOpType;
  const auto global_op_type = static_cast<OpType>(global_mode);
  auto& op_cache = global_op_type == OpType ::SET_VALUE ?
                   set_cache_ : add_cache_;


  // First, segregate the local and non-local operations
  std::vector<std::pair<int64_t, double>> local_cache;
  std::vector<std::pair<int64_t, double>> nonlocal_cache;
  for (const auto& op : op_cache)
  {
    const int op_pid = FindOwnerPID(op.first);
    if (op_pid == location_id_) local_cache.emplace_back(op);
    else nonlocal_cache.emplace_back(op);
  }

  // The local operations can be handled immediately
  for (const auto& [global_id, value] : local_cache)
  {
    const int64_t local_id = global_id - extents_[location_id_];
    ChiLogicalErrorIf(
        local_id < 0 or local_id >= local_size_,
        "Invalid mapping from global to local.");

    if (global_op_type == OpType::SET_VALUE) values_[local_id] = value;
    else values_[local_id] += value;
  }

  // With this, the data that needs to be sent to other processes must be
  // determined. Here, a mapping is developed between the processes that
  // need to be sent information, and the serialized operations that need
  // to be sent. The operations are serialized by converting the
  // int64_t-double pair to bytes.
  std::map<int, chi_data_types::ByteArray> pid_send_map;
  for (const auto& [global_id, value] : nonlocal_cache)
  {
    const int pid = FindOwnerPID(global_id);
    auto& byte_array = pid_send_map[pid];
    byte_array.Write(global_id);
    byte_array.Write(value);
  }

  // For use with MPI, the byte arrays from above is converted to an
  // STL vector of bytes.
  std::map<int, std::vector<std::byte>> pid_send_map_bytes;
  for (const auto& [pid, byte_array] : pid_send_map)
    pid_send_map_bytes[pid] = byte_array.Data();

  // With the information that needs to be sent to other processes obtained,
  // now, the information to be received from other processes is needed.
  // To do this, each process must send to each other process the information
  // that it needs. With each process knowing what each other process needs
  // from it, a map of information to be sent is obtained.
  std::map<int, std::vector<std::byte>> pid_recv_map_bytes =
      chi_mpi_utils::MapAllToAll(pid_send_map_bytes, MPI_BYTE);

  // The received information is now processed, unpacked, and the
  // necessary operations performed
  for (const auto& [pid, byte_vector] : pid_recv_map_bytes)
  {
    const auto packet_size = sizeof(std::pair<int64_t, double>);

    ChiLogicalErrorIf(
        byte_vector.size() % packet_size != 0,
        "Unrecognized received operations. Operations are serialized with "
        "an int64_t and double, but the received packet from process " +
        std::to_string(pid) + " on process " + std::to_string(location_id_) +
        " is not an integer multiple of the size of an int64_t and double.");

    const size_t num_ops = byte_vector.size() / packet_size;
    chi_data_types::ByteArray byte_array(byte_vector);
    for (size_t k = 0; k < num_ops; ++k)
    {
      const int64_t global_id = byte_array.Read<int64_t>();
      const double value = byte_array.Read<double>();

      // Check that the global ID is in fact valid for this process
      const int64_t local_id = global_id - extents_[location_id_];

      ChiLogicalErrorIf(
          local_id < 0 or local_id >= local_size_,
          "A non-local global ID was received by process " +
          std::to_string(location_id_) + " by process " + std::to_string(pid) +
          " during vector assembly.");

      // Contribute to the local vector
      if (global_op_type == OpType ::SET_VALUE) values_[local_id] = value;
      else values_[local_id] += value;
    }
  }

  // Finally, clear the operation cache
  op_cache.clear();
}


void ParallelVector::DefineParallelStructure()
{
  // Get the local vector sizes per processor
  std::vector<uint64_t> local_sizes(process_count_, 0);
  MPI_Allgather(&local_size_,            //sendbuf
                1, MPI_UINT64_T,        //sendcount + sendtype
                local_sizes.data(), //recvbuf
                1, MPI_UINT64_T,        //recvcount + recvtype
                comm_);         //communicator

  // With the vector sizes per processor, now the offsets for each
  // processor can be defined using a cumulative sum per processor.
  // This allows for the determination of whether a global index is
  // locally owned or not.
  extents_.assign(process_count_ + 1, 0);
  for (size_t p = 1; p < process_count_; ++p)
    extents_[p] = extents_[p - 1] + local_sizes[p - 1];
  extents_[process_count_] =
      extents_[process_count_ - 1] + local_sizes.back();
}


int ParallelVector::FindOwnerPID(const uint64_t global_id) const
{
  ChiInvalidArgumentIf(global_id >= global_size_,
                       "Invalid global id specified.");

  for (int p = 0; p < process_count_; ++p)
    if (global_id >= extents_[p] and
        global_id < extents_[p + 1])
      return p;
  return -1;

}


std::string ParallelVector::PrintStr() const
{
  std::stringstream ss;
  for (size_t i = 0; i < values_.size(); ++i)
    ss << (i == 0 ? "[" : "") << values_[i]
       << (i < values_.size() - 1 ? " " : "]");
  return ss.str();
}

}

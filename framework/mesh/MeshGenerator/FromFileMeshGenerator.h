#ifndef CHITECH_FROMFILEMESHGENERATOR_H
#define CHITECH_FROMFILEMESHGENERATOR_H

#include "MeshGenerator.h"

namespace chi_mesh
{

class FromFileMeshGenerator : public MeshGenerator
{
public:
  static chi::InputParameters GetInputParameters();
  explicit FromFileMeshGenerator(const chi::InputParameters& params);

protected:
  std::unique_ptr<UnpartitionedMesh> GenerateUnpartitionedMesh(
    std::unique_ptr<UnpartitionedMesh> input_umesh) override;
  const std::string filename_;
  const std::string material_id_fieldname_;
  const std::string boundary_id_fieldname_;
};

} // namespace chi_mesh

#endif // CHITECH_FROMFILEMESHGENERATOR_H

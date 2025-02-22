#include <gflags/gflags.h>
#include <yaml-cpp/yaml.h>

#include "drake/common/yaml/yaml_write_archive.h"
#include "drake/systems/analysis/simulator_config_functions.h"
#include "drake/systems/analysis/simulator_gflags.h"
#include "drake/systems/primitives/constant_vector_source.h"

namespace drake {
namespace systems {
template <typename T>
void ResetIntegratorFromGflagsTest() {
  ConstantVectorSource<T> source(2);
  auto simulator = MakeSimulatorFromGflags(source);
  ResetIntegratorFromGflags(simulator.get());
  const auto simulator_config = ExtractSimulatorConfig(*simulator);
  drake::yaml::YamlWriteArchive writer;
  writer.Accept(simulator_config);
  drake::log()->info(writer.EmitString());
}
}  // namespace systems
}  // namespace drake

int main(int argc, char* argv[]) {
  gflags::SetUsageMessage("A stub main() program to test simulator_gflags.h");
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  drake::systems::ResetIntegratorFromGflagsTest<double>();
  drake::systems::ResetIntegratorFromGflagsTest<drake::AutoDiffXd>();
}

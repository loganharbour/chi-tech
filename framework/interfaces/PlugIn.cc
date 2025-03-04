#include "PlugIn.h"

#include "ChiObjectFactory.h"

#include "chi_runtime.h"
#include "chi_log.h"
#include "utils/chi_utils.h"
#include "console/chi_console.h"

#include <dlfcn.h>

namespace chi
{

RegisterChiObject(chi, Plugin);

InputParameters Plugin::GetInputParameters()
{
  InputParameters params = ChiObject::GetInputParameters();

  // clang-format off
  params.SetGeneralDescription(
  "Object to handle the loading of shared libraries as plug-ins");
  // clang-format on
  params.SetDocGroup("DocInterfaces");

  params.AddRequiredParameter<std::string>(
    "plugin_path", "Path to the shared library containing the plug-in.");
  params.AddOptionalParameter("entry_function", "", "Entry function to call.");

  return params;
}

Plugin::Plugin(const InputParameters& params)
  : ChiObject(params),
    plugin_path_(params.GetParamValue<std::string>("plugin_path"))
{
  Chi::log.Log0Verbose1() << "Loading plugin \"" << plugin_path_ << "\"";
  chi::RegistryStatuses registry_statuses = Chi::GetStatusOfRegistries();

  chi::AssertReadibleFile(plugin_path_);
  library_handle_ = dlopen(plugin_path_.c_str(), RTLD_LAZY);

  ChiLogicalErrorIf(not library_handle_,
                    "Failure loading \"" + plugin_path_ + "\"");

  const auto& user_params = params.ParametersAtAssignment();
  if (user_params.Has("entry_function"))
  {
    const auto& entry_function =
      user_params.GetParamValue<std::string>("entry_function");
    typedef void(some_func)();

    auto func = (some_func*)dlsym(library_handle_, entry_function.c_str());

    ChiLogicalErrorIf(
      not func, "Failed to call entry function \"" + entry_function + "\"");

    // Calling the function
    func();
  }

  Chi::console.UpdateConsoleBindings(registry_statuses);
}

Plugin::~Plugin() { dlclose(library_handle_); }

} // namespace chi
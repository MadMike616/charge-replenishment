#pragma once
#include <D2RLPlugin/api.h>

namespace Recharge::Setup {
// Runs before runtime configuration is read. Completed installs only read TOML.
// Failure leaves the completion token unset and disables this plugin load.
auto Ensure(const D2RL::PluginContext* context) noexcept -> bool;
}

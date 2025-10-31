#pragma once

#include <string>

#include "telemux/metrics.h"

namespace telemux {

// Renders a MetricsRegistry snapshot as a small JSON object, suitable for
// the CLI's `stats` subcommand or an external monitoring scrape.
std::string render_stats_json(const MetricsRegistry& registry);

}  // namespace telemux

#pragma once
#include <string>

namespace agent_tasks {
struct Config { std::string url; std::string token; };
bool load_config(Config& config);
bool save_config(const Config& config);
}

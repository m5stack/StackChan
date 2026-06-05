#pragma once

#include <memory>

#include <model_path.h>

class WakeWord;

std::unique_ptr<WakeWord> CreateWakeWordEngine(srmodel_list_t* models_list);

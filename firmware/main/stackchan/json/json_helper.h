/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "../avatar/avatar.h"
#include "../motion/motion.h"
#include "../animation/animation.h"
#include "../addons/neon_light/neon_light.h"

namespace stackchan {

namespace avatar {
void update_from_json(Avatar* avatar, const char* jsonContent);
}

namespace motion {
void update_from_json(Motion* motion, const char* jsonContent);
}

namespace animation {
KeyframeSequence parse_sequence_from_json(const char* jsonContent);
}

namespace addon {
void update_neon_light_from_json(NeonLight* left, NeonLight* right, const char* jsonContent);
}

}  // namespace stackchan

/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "../avatar/avatar.h"
#include "../motion/motion.h"
#include "../animation/animation.h"

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

}  // namespace stackchan

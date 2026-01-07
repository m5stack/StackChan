/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "avatar/avatar.h"
#include "motion/motion.h"
#include "utils/object_pool.h"

namespace stackchan {

/**
 * @brief Modifiable base class, expose modifiable APIs to the modifiers
 *
 */
class Modifiable {
public:
    virtual ~Modifiable() = default;

    virtual motion::Motion& motion() = 0;

    virtual avatar::Avatar& avatar() = 0;

    virtual bool hasAvatar() = 0;
};

/**
 * @brief Modifier base class
 *
 */
class Modifier : public Poolable {
public:
    virtual void _update(Modifiable& stackchan)
    {
    }
};

}  // namespace stackchan

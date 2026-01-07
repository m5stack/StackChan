/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "modifiable.h"
#include "modifiers/modifiers.h"
#include "json/json_helper.h"
#include <memory>

namespace stackchan {

/**
 * @brief
 *
 */
class StackChan : public Modifiable {
public:
    /**
     * @brief Attach motion instance
     *
     * @param yawServo
     * @param pitchServo
     */
    void attachMotion(std::unique_ptr<motion::Motion> motion)
    {
        _motion = std::move(motion);
    }

    /**
     * @brief Reset and destroy attached motion instance
     *
     */
    void resetMotion()
    {
        _motion.reset();
    }

    /**
     * @brief Attach avatar instance
     *
     * @param avatar
     */
    void attachAvatar(std::unique_ptr<avatar::Avatar> avatar)
    {
        _avatar = std::move(avatar);
    }

    /**
     * @brief Reset and destroy attached avatar instance
     *
     */
    void resetAvatar()
    {
        _avatar.reset();
    }

    /**
     * @brief Get motion instance
     *
     * @return motion::Motion&
     */
    motion::Motion& motion() override
    {
        return *_motion;
    }

    /**
     * @brief Get avatar instance
     *
     * @return avatar::Avatar&
     */
    avatar::Avatar& avatar() override
    {
        return *_avatar;
    }

    /**
     * @brief Check if avatar is attached
     *
     * @return true
     * @return false
     */
    bool hasAvatar() override
    {
        if (_avatar) {
            return true;
        }
        return false;
    }

    /**
     * @brief Check if motion is attached
     *
     * @return true
     * @return false
     */
    bool hasMotion()
    {
        if (_motion) {
            return true;
        }
        return false;
    }

    /**
     * @brief Add modifier
     *
     * @param modifier
     * @return int
     */
    int addModifier(std::unique_ptr<Modifier> modifier)
    {
        return _modifier_pool.create(std::move(modifier));
    }
    Modifier* getModifier(int id)
    {
        return _modifier_pool.get(id);
    }
    bool removeModifier(int id)
    {
        return _modifier_pool.destroy(id);
    }
    void clearModifiers()
    {
        _modifier_pool.clear();
    }

    /**
     * @brief Update
     *
     */
    void update()
    {
        _modifier_pool.forEach([this](Modifier* modifier, int id) { modifier->_update(*this); });
        _modifier_pool.cleanup();

        if (_avatar) {
            _avatar->update();
        }

        if (_motion) {
            _motion->update();
        }
    }

    /**
     * @brief
     *
     * @param jsonContent
     */
    void updateAvatarFromJson(const char* jsonContent)
    {
        if (_avatar) {
            avatar::update_from_json(_avatar.get(), jsonContent);
        }
    }

    /**
     * @brief
     *
     * @param jsonContent
     */
    void updateMotionFromJson(const char* jsonContent)
    {
        if (_motion) {
            motion::update_from_json(_motion.get(), jsonContent);
        }
    }

private:
    std::unique_ptr<avatar::Avatar> _avatar;
    std::unique_ptr<motion::Motion> _motion;
    ObjectPool<Modifier> _modifier_pool;
};

}  // namespace stackchan

stackchan::StackChan& GetStackChan();

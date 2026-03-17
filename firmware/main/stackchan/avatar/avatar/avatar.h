/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "elements/key_elements.h"
#include "decorator.h"
#include <memory>

namespace stackchan::avatar {

/**
 * @brief Avatar base class
 *
 */
class Avatar {
public:
    /**
     * @brief Update avatar, trigger all elements, decorators and modifiers to update
     *
     */
    virtual void update()
    {
        _key_elements.forEach([](Element* element) {
            // Update all elements
            element->_update();
        });

        _decorator_pool.forEach([this](Decorator* decorator, int id) {
            // Update all decorators
            decorator->_update();
        });

        // Cleanup pools
        _decorator_pool.cleanup();
    }

    const KeyElements_t& getKeyElements()
    {
        return _key_elements;
    }

    virtual void setEmotion(const Emotion& emotion)
    {
        _emotion = emotion;

        _key_elements.forEach([&emotion](Element* element) {
            // Set for all elements
            element->setEmotion(emotion);
        });

        _decorator_pool.forEach([&emotion](Decorator* decorator, int id) {
            // Set for all decorators
            decorator->setEmotion(emotion);
        });
    }

    Emotion getEmotion() const
    {
        return _emotion;
    }

    Feature& leftEye()
    {
        return *getKeyElements().leftEye;
    }

    Feature& rightEye()
    {
        return *getKeyElements().rightEye;
    }

    Feature& mouth()
    {
        return *getKeyElements().mouth;
    }

    void setSpeech(std::string_view text)
    {
        if (getKeyElements().speechBubble) {
            getKeyElements().speechBubble->setSpeech(text);
        }
    }

    void clearSpeech()
    {
        if (getKeyElements().speechBubble) {
            getKeyElements().speechBubble->clearSpeech();
        }
    }

    void setSpeechTextFont(void* font)
    {
        if (getKeyElements().speechBubble) {
            getKeyElements().speechBubble->setTextFont(font);
        }
    }

    void setModifyLock(bool locked)
    {
        _is_modify_locked = locked;
    }

    bool isModifyLocked()
    {
        return _is_modify_locked;
    }

    /* ---------------------------- Decorator helpers --------------------------- */

    int addDecorator(std::unique_ptr<Decorator> decorator)
    {
        return _decorator_pool.create(std::move(decorator));
    }

    bool removeDecorator(int id)
    {
        return _decorator_pool.destroy(id);
    }

    void clearDecorators()
    {
        _decorator_pool.clear();
    }

protected:
    Avatar() = default;

    Emotion _emotion = Emotion::Neutral;
    KeyElements_t _key_elements;
    ObjectPool<Decorator> _decorator_pool;

    bool _is_modify_locked = false;
};

}  // namespace stackchan::avatar

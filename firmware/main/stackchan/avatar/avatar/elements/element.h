/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "emotion.h"
#include <smooth_ui_toolkit.hpp>
#include <uitk/short_namespace.hpp>

namespace stackchan::avatar {

/**
 * @brief Renderable element interface
 *
 */
class Element {
public:
    virtual ~Element() = default;

    /**
     * @brief (-100 ~ 100, -100 ~ 100)
     *
     * @param position
     */
    virtual void setPosition(const uitk::Vector2i& position)
    {
        _position = position;
        _position.clamp({-100, -100}, {100, 100});
    }
    virtual uitk::Vector2i getPosition()
    {
        return _position;
    }

    /**
     * @brief 0~3600
     *
     * @param rotation
     */
    virtual void setRotation(int rotation)
    {
        _rotation = uitk::clamp(rotation, 0, 3600);
    }
    virtual int getRotation()
    {
        return _rotation;
    }

    /**
     * @brief
     *
     * @param emotion
     */
    virtual void setEmotion(const Emotion& emotion)
    {
    }
    virtual Emotion getEmotion() const
    {
        return Emotion::Neutral;
    }

    /**
     * @brief Ignore emotion update
     *
     * @param ignore
     */
    virtual void setIgnoreEmotion(bool ignore)
    {
        _ignore_emotion = ignore;
    }
    virtual bool getIgnoreEmotion()
    {
        return _ignore_emotion;
    }

    /**
     * @brief true: visible, false: invisible
     *
     * @param visible
     */
    virtual void setVisible(bool visible)
    {
        _visible = visible;
    }
    virtual bool getVisible()
    {
        return _visible;
    }

    /**
     * @brief Invoked in every update loop
     *
     */
    virtual void _update()
    {
    }

protected:
    uitk::Vector2i _position;
    int _rotation        = 0;
    bool _visible        = true;
    bool _ignore_emotion = false;
};

}  // namespace stackchan::avatar

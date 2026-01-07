/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "element.h"

namespace stackchan::avatar {

/**
 * @brief Feature base class
 *
 */
class Feature : public Element {
public:
    /**
     * @brief A normalized value representing the intensity of eyes, mouth, or other
     *
     * @param weight 0~100
     */
    virtual void setWeight(int weight)
    {
        _weight = uitk::clamp(weight, 0, 100);
    }
    virtual int getWeight()
    {
        return _weight;
    }

    /**
     * @brief A normalized value representing the size of eyes, 0 for normal size
     *
     * @param size -100~100
     */
    virtual void setSize(int size)
    {
        _size = uitk::clamp(size, -100, 100);
    }
    virtual int getSize()
    {
        return _size;
    }

protected:
    int _weight = 0;
    int _size   = 0;
};

}  // namespace stackchan::avatar

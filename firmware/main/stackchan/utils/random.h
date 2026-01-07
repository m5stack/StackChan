/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <mutex>
#include <random>
#include <vector>
#include <cassert>

#ifdef ESP_PLATFORM
#include <esp_random.h>
#endif

namespace stackchan {

class Random {
public:
    static Random& getInstance()
    {
        static Random instance;
        return instance;
    }

    int getInt(int a, int b)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (a > b) {
            std::swap(a, b);
        }
        return std::uniform_int_distribution<int>{a, b}(_rng);
    }

    float getFloat(float a, float b)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (a > b) {
            std::swap(a, b);
        }
        return std::uniform_real_distribution<float>{a, b}(_rng);
    }

    double getDouble(double a, double b)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (a > b) {
            std::swap(a, b);
        }
        return std::uniform_real_distribution<double>{a, b}(_rng);
    }

    template <typename T>
    const T& choice(const std::vector<T>& vec)
    {
        assert(!vec.empty());
        std::lock_guard<std::mutex> lock(_mutex);
        std::uniform_int_distribution<size_t> dist(0, vec.size() - 1);
        return vec[dist(_rng)];
    }

private:
    std::mt19937 _rng;
    std::mutex _mutex;

    Random()
    {
#ifdef ESP_PLATFORM
        _rng.seed(esp_random());
#else
        std::random_device rd;
        _rng.seed(rd());
#endif
    }
};

}  // namespace stackchan

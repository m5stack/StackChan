#pragma once

#include <cstdlib>

namespace view {
class BottomSwipe {
public:
    bool update(bool pressed, int x, int y) {
        bool triggered = false;
        if (pressed && !was_pressed_) {
            if (valid_point(y) && y >= screen_height_ - edge_height_) {
                start_x_ = x;
                start_y_ = y;
                tracking_ = true;
                triggered_ = false;
            }
        } else if (tracking_) {
            if (!triggered_ && valid_point(y) && start_y_ - y >= min_distance_ &&
                start_y_ - y > std::abs(x - start_x_)) {
                triggered_ = true;
                triggered = true;
            }
            if (!pressed) {
                tracking_ = false;
                triggered_ = false;
            }
        }
        was_pressed_ = pressed;
        return triggered;
    }

private:
    static constexpr int screen_height_ = 240;
    static constexpr int edge_height_ = 40;
    static constexpr int min_distance_ = 40;
    int start_x_ = 0;
    int start_y_ = 0;
    bool tracking_ = false;
    bool triggered_ = false;
    bool was_pressed_ = false;

    static bool valid_point(int y) { return y >= 0 && y < screen_height_; }
};
}

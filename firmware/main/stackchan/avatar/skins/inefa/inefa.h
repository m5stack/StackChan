#pragma once
#include "../../avatar/avatar.h"
#include "../../avatar/elements/feature.h"
#include <lvgl.h>
#include <smooth_lvgl.hpp>
#include <memory>

namespace stackchan::avatar {

/**
 * @brief
 *
 */
class InefaAvatar : public Avatar {
public:
    lv_color_t primaryColor   = lv_color_white();
    lv_color_t secondaryColor = lv_color_black();

    void init(lv_obj_t* parent, const lv_font_t* font = &lv_font_montserrat_16) override;
    uitk::lvgl_cpp::Container* getPanel() const override;

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _pannel;
};

/**
 * @brief
 *
 */
class InefaEyes : public Feature {
public:
    InefaEyes(lv_obj_t* parent, lv_color_t primaryColor, lv_color_t secondaryColor, bool isLeftEye);
    ~InefaEyes();

    void setPosition(const uitk::Vector2i& position) override;
    void setWeight(int weight) override;
    void setRotation(int rotation) override;
    void setEmotion(const Emotion& emotion) override;
    void setVisible(bool visible) override;
    void setSize(int size) override;
    void _update() override;

private:
    bool _is_left_eye    = false;
    bool _is_angry       = false;
    bool _is_happy       = false;
    int _eyelid_offset_y = 0;
    int _eye_width       = 0;
    int _eye_height      = 0;

    void applyPosition(int extra_y = 0);

    std::unique_ptr<uitk::lvgl_cpp::Container> _container;
    std::unique_ptr<uitk::lvgl_cpp::Container> _eye;
    std::unique_ptr<uitk::lvgl_cpp::Container> _angry_upper;
    std::unique_ptr<uitk::lvgl_cpp::Container> _angry_lower;
    std::unique_ptr<uitk::lvgl_cpp::Container> _angry_brow;
    std::unique_ptr<uitk::lvgl_cpp::Container> _eyelid;
    std::unique_ptr<uitk::lvgl_cpp::Container> _lower_eyelid;
};

/**
 * @brief
 *
 */
class InefaMouth : public Feature {
public:
    InefaMouth(lv_obj_t* parent, lv_color_t primaryColor, lv_color_t secondaryColor);
    ~InefaMouth();

    void setPosition(const uitk::Vector2i& position) override;
    void setWeight(int weight) override;
    void setEmotion(const Emotion& emotion) override;
    void setRotation(int rotation) override;
    void setVisible(bool visible) override;

private:
    bool _is_angry = false;

    std::unique_ptr<uitk::lvgl_cpp::Container> _container;
    std::unique_ptr<uitk::lvgl_cpp::Container> _mouth;
    std::unique_ptr<uitk::lvgl_cpp::Container> _mouth_hole;
    std::unique_ptr<uitk::lvgl_cpp::Container> _mouth_tooth_left;
    std::unique_ptr<uitk::lvgl_cpp::Container> _mouth_tooth_mid;
    std::unique_ptr<uitk::lvgl_cpp::Container> _mouth_tooth_right;
};

/**
 * @brief
 *
 */
class InefaSpeechBubble : public SpeechBubble {
public:
    InefaSpeechBubble(lv_obj_t* parent, lv_color_t primaryColor, lv_color_t secondaryColor, const lv_font_t* font);
    ~InefaSpeechBubble();

    void setSpeech(std::string_view text) override;
    void clearSpeech() override;
    void setVisible(bool visible) override;
    void setTextFont(void* font) override;

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _container;
    std::unique_ptr<uitk::lvgl_cpp::Image> _arrow;
    std::unique_ptr<uitk::lvgl_cpp::Container> _bubble;
    std::unique_ptr<uitk::lvgl_cpp::Label> _text;
};

}  // namespace stackchan::avatar

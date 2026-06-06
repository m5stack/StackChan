#include "inefa.h"
#include <hal/hal.h>

using namespace uitk;
using namespace uitk::lvgl_cpp;
using namespace stackchan::avatar;

static const lv_color_t _inefa_eye    = lv_color_hex(0xD8B3B6);
static const lv_color_t _angry_eye    = lv_color_hex(0xEF5C70);
static const Vector2i _eye_pos        = Vector2i(-62, -10);
static const Vector2i _eye_min_offset = Vector2i(-16, -16);
static const Vector2i _eye_max_offset = Vector2i(16, 16);
static const Vector2i _eye_min_size   = Vector2i(18, 42);
static const Vector2i _eye_base_size  = Vector2i(28, 88);
static const Vector2i _eye_max_size   = Vector2i(36, 104);
static const Vector2i _eye_container_size = Vector2i(84, 122);
static const int _eye_shadow_width    = 4;
static const int _eye_shadow_spread   = 0;
static const int _eyelid_mask_margin  = _eye_shadow_width + _eye_shadow_spread + 6;
static const int _angry_piece_width   = 42;
static const int _angry_piece_height  = 8;
static const int _happy_eye_lift      = -16;
static const int _happy_anim_period_ms = 1200;

InefaEyes::InefaEyes(lv_obj_t* parent, lv_color_t primaryColor, lv_color_t secondaryColor, bool isLeftEye)
{
    (void)primaryColor;

    _is_left_eye = isLeftEye;

    _container = std::make_unique<Container>(parent);
    _container->setRadius(0);
    _container->setAlign(LV_ALIGN_CENTER);
    _container->setBorderWidth(0);
    _container->setBgOpa(0);
    _container->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _container->setPadding(0, 0, 0, 0);
    _container->setTransformPivot(_eye_container_size.x / 2, _eye_container_size.y / 2);
    _container->setSize(_eye_container_size.x, _eye_container_size.y);

    _eye = std::make_unique<Container>(_container->get());
    _eye->setRadius(1);
    _eye->align(LV_ALIGN_CENTER, 0, 0);
    _eye->setBorderWidth(0);
    _eye->setBgColor(_inefa_eye);
    _eye->setShadowColor(_inefa_eye);
    _eye->setShadowWidth(_eye_shadow_width);
    _eye->setShadowSpread(_eye_shadow_spread);
    _eye->setShadowOpa(LV_OPA_80);
    _eye->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    auto make_angry_piece = [this](int width, int height, int x, int y, int rotation) {
        auto piece = std::make_unique<Container>(_container->get());
        piece->setRadius(0);
        piece->align(LV_ALIGN_CENTER, x, y);
        piece->setSize(width, height);
        piece->setTransformPivot(width / 2, height / 2);
        piece->setRotation(rotation);
        piece->setBorderWidth(0);
        piece->setBgColor(_angry_eye);
        piece->setShadowColor(_angry_eye);
        piece->setShadowWidth(4);
        piece->setShadowSpread(0);
        piece->setShadowOpa(LV_OPA_70);
        piece->setHidden(true);
        piece->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
        return piece;
    };

    const int angry_x      = _is_left_eye ? 8 : -8;
    const int upper_angle  = _is_left_eye ? 450 : 3150;
    const int lower_angle  = _is_left_eye ? 3150 : 450;
    const int brow_angle   = _is_left_eye ? 450 : 3150;
    _angry_upper = make_angry_piece(_angry_piece_width, _angry_piece_height, angry_x, -14, upper_angle);
    _angry_lower = make_angry_piece(_angry_piece_width, _angry_piece_height, angry_x, 14, lower_angle);
    _angry_brow  = make_angry_piece(18, 5, _is_left_eye ? 8 : -8, -42, brow_angle);

    _eyelid = std::make_unique<Container>(_container->get());
    _eyelid->setRadius(0);
    _eyelid->align(LV_ALIGN_CENTER, 0, 0);
    _eyelid->setBorderWidth(0);
    _eyelid->setBgColor(secondaryColor);
    _eyelid->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _lower_eyelid = std::make_unique<Container>(_container->get());
    _lower_eyelid->setRadius(0);
    _lower_eyelid->align(LV_ALIGN_CENTER, 0, 0);
    _lower_eyelid->setBorderWidth(0);
    _lower_eyelid->setBgColor(secondaryColor);
    _lower_eyelid->setHidden(true);
    _lower_eyelid->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    setSize(0);
    setWeight(100);
    setPosition(_position);
    setRotation(0);
}

InefaEyes::~InefaEyes()
{
    _lower_eyelid.reset();
    _eyelid.reset();
    _angry_brow.reset();
    _angry_lower.reset();
    _angry_upper.reset();
    _eye.reset();
    _container.reset();
}

void InefaEyes::applyPosition(int extra_y)
{
    auto pos_x = _is_left_eye ? _eye_pos.x : -_eye_pos.x;
    pos_x += map_range(_position.x, -100, 100, _eye_min_offset.x, _eye_max_offset.x);
    auto pos_y = _eye_pos.y + map_range(_position.y, -100, 100, _eye_min_offset.y, _eye_max_offset.y);
    if (_is_happy) {
        pos_y += _happy_eye_lift;
    }
    pos_y += extra_y;

    _container->setPos(pos_x, pos_y);
}

void InefaEyes::setPosition(const Vector2i& position)
{
    Element::setPosition(position);

    applyPosition();
    _eyelid->setY(_eyelid_offset_y);
}

void InefaEyes::setWeight(int weight)
{
    Feature::setWeight(weight);

    if (_is_angry || _is_happy) {
        _eyelid->setHidden(true);
        _lower_eyelid->setHidden(!_is_happy);
        return;
    }

    _eyelid->setHidden(false);
    _lower_eyelid->setHidden(true);
    _eyelid_offset_y = -map_range(_weight, 0, 100, 0, (int)_eyelid->getHeight());

    _eyelid->setY(_eyelid_offset_y);
}

void InefaEyes::setRotation(int rotation)
{
    Element::setRotation(rotation);

    _container->setRotation(rotation);
}

void InefaEyes::setEmotion(const Emotion& emotion)
{
    if (getIgnoreEmotion()) {
        return;
    }

    _is_angry = emotion == Emotion::Angry;
    _is_happy = emotion == Emotion::Happy;
    _eye->setHidden(_is_angry);
    _eyelid->setHidden(_is_angry);
    _lower_eyelid->setHidden(!_is_happy);
    _angry_upper->setHidden(!_is_angry);
    _angry_lower->setHidden(!_is_angry);
    _angry_brow->setHidden(!_is_angry);

    if (_is_angry) {
        setRotation(0);
        setWeight(100);
        return;
    }

    if (_is_happy) {
        setRotation(0);
        setWeight(100);
        applyPosition();
        return;
    }

    applyPosition();

    auto apply_style = [this](int weight, int rotation) {
        setWeight(weight);
        if (_is_left_eye) {
            setRotation(rotation);
        } else {
            setRotation(-rotation);
        }
    };

    switch (emotion) {
        case Emotion::Neutral:
            apply_style(100, 0);
            break;
        case Emotion::Sad:
            apply_style(70, -400);
            break;
        case Emotion::Doubt:
            apply_style(75, 0);
            break;
        case Emotion::Sleepy:
            apply_style(35, -50);
            break;
        default:
            break;
    }
}

void InefaEyes::setVisible(bool visible)
{
    Element::setVisible(visible);

    _container->setHidden(!visible);
}

void InefaEyes::setSize(int size)
{
    Feature::setSize(size);

    if (_size >= 0) {
        _eye_width  = map_range(_size, 0, 100, _eye_base_size.x, _eye_max_size.x);
        _eye_height = map_range(_size, 0, 100, _eye_base_size.y, _eye_max_size.y);
    } else {
        _eye_width  = map_range(_size, -100, 0, _eye_min_size.x, _eye_base_size.x);
        _eye_height = map_range(_size, -100, 0, _eye_min_size.y, _eye_base_size.y);
    }

    _eye->setSize(_eye_width, _eye_height);
    _eyelid->setSize(_eye_width + _eyelid_mask_margin * 2, _eye_height + _eyelid_mask_margin * 2);
    _lower_eyelid->setSize(_eye_width + _eyelid_mask_margin * 2, _eye_height / 2 + _eyelid_mask_margin * 2);

    // Force eyelid update
    setWeight(getWeight());
}

void InefaEyes::_update()
{
    if (!_is_happy || _is_angry || !_lower_eyelid || _eye_height <= 0) {
        return;
    }

    const int phase = GetHAL().millis() % _happy_anim_period_ms;
    const int half_period = _happy_anim_period_ms / 2;
    const int t = phase < half_period ? phase : _happy_anim_period_ms - phase;
    const int eyelid_cover = map_range(t, 0, half_period, 18, 28);
    const int bob_y = map_range(t, 0, half_period, 0, -2);
    const int lower_height = _eye_height / 2 + _eyelid_mask_margin * 2;
    const int lower_y = _eye_height / 2 - eyelid_cover + lower_height / 2;

    applyPosition(bob_y);
    _lower_eyelid->setHidden(false);
    _lower_eyelid->align(LV_ALIGN_CENTER, 0, lower_y);
}

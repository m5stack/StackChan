#include "inefa.h"

using namespace uitk;
using namespace uitk::lvgl_cpp;
using namespace stackchan::avatar;

static const lv_color_t _inefa_mouth    = lv_color_hex(0xD8B3B6);
static const lv_color_t _angry_mouth    = lv_color_hex(0xEF5C70);
static const Vector2i _mouth_pos        = Vector2i(0, 18);
static const Vector2i _mouth_min_offset = Vector2i(-16, -16);
static const Vector2i _mouth_max_offset = Vector2i(16, 16);
static const Vector2i _mouth_canvas_size = Vector2i(76, 56);

InefaMouth::InefaMouth(lv_obj_t* parent, lv_color_t primaryColor, lv_color_t secondaryColor)
{
    (void)primaryColor;

    _container = std::make_unique<Container>(parent);
    _container->setAlign(LV_ALIGN_CENTER);
    _container->setBorderWidth(0);
    _container->setBgOpa(0);
    _container->setPadding(0, 0, 0, 0);
    _container->setSize(_mouth_canvas_size.x, _mouth_canvas_size.y);
    _container->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _mouth = std::make_unique<Container>(_container->get());
    _mouth->setAlign(LV_ALIGN_CENTER);
    _mouth->setBorderWidth(0);
    _mouth->setBgColor(_inefa_mouth);
    _mouth->setShadowColor(_inefa_mouth);
    _mouth->setShadowWidth(4);
    _mouth->setShadowSpread(0);
    _mouth->setShadowOpa(LV_OPA_70);
    _mouth->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _mouth_hole = std::make_unique<Container>(_container->get());
    _mouth_hole->setAlign(LV_ALIGN_CENTER);
    _mouth_hole->setBorderWidth(0);
    _mouth_hole->setBgColor(secondaryColor);
    _mouth_hole->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    auto make_tooth = [this]() {
        auto tooth = std::make_unique<Container>(_container->get());
        tooth->setSize(4, 5);
        tooth->setRadius(0);
        tooth->setBorderWidth(0);
        tooth->setBgColor(_angry_mouth);
        tooth->setShadowColor(_angry_mouth);
        tooth->setShadowWidth(3);
        tooth->setShadowSpread(0);
        tooth->setShadowOpa(LV_OPA_70);
        tooth->setHidden(true);
        tooth->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
        return tooth;
    };
    _mouth_tooth_left  = make_tooth();
    _mouth_tooth_mid   = make_tooth();
    _mouth_tooth_right = make_tooth();

    setPosition(_position);
    setWeight(0);
    setRotation(0);
}

InefaMouth::~InefaMouth()
{
    _mouth_tooth_right.reset();
    _mouth_tooth_mid.reset();
    _mouth_tooth_left.reset();
    _mouth_hole.reset();
    _mouth.reset();
    _container.reset();
}

void InefaMouth::setPosition(const Vector2i& position)
{
    Element::setPosition(position);

    auto pos_x = _mouth_pos.x + map_range(_position.x, -100, 100, _mouth_min_offset.x, _mouth_max_offset.x);
    auto pos_y = _mouth_pos.y + map_range(_position.y, -100, 100, _mouth_min_offset.y, _mouth_max_offset.y);

    _container->setPos(pos_x, pos_y);
}

void InefaMouth::setWeight(int weight)
{
    Feature::setWeight(weight);

    _mouth_tooth_left->setHidden(true);
    _mouth_tooth_mid->setHidden(true);
    _mouth_tooth_right->setHidden(true);

    if (_is_angry) {
        _mouth->setBgColor(_angry_mouth);
        _mouth->setShadowColor(_angry_mouth);
        _mouth->setSize(24, 22);
        _mouth->setRadius(1);

        _mouth_hole->setSize(12, 10);
        _mouth_hole->setRadius(1);
        _mouth_hole->align(LV_ALIGN_CENTER, 0, -2);
        _mouth_hole->setHidden(false);

        _mouth_tooth_left->align(LV_ALIGN_CENTER, -8, 12);
        _mouth_tooth_mid->align(LV_ALIGN_CENTER, 0, 12);
        _mouth_tooth_right->align(LV_ALIGN_CENTER, 8, 12);
        _mouth_tooth_left->setHidden(false);
        _mouth_tooth_mid->setHidden(false);
        _mouth_tooth_right->setHidden(false);
        return;
    }

    _mouth->setBgColor(_inefa_mouth);
    _mouth->setShadowColor(_inefa_mouth);
    _mouth_hole->align(LV_ALIGN_CENTER, 0, 0);

    int size_x = 28;
    int size_y = 3;
    int radius = 1;

    if (_weight < 35) {
        size_x = map_range(_weight, 0, 34, 28, 36);
        size_y = map_range(_weight, 0, 34, 3, 5);
        radius = 1;
        _mouth_hole->setHidden(true);
    } else if (_weight < 70) {
        size_x = map_range(_weight, 35, 69, 36, 42);
        size_y = map_range(_weight, 35, 69, 6, 12);
        radius = 2;
        _mouth_hole->setHidden(true);
    } else {
        size_x = map_range(_weight, 70, 100, 30, 36);
        size_y = map_range(_weight, 70, 100, 30, 40);
        radius = LV_RADIUS_CIRCLE;
        _mouth_hole->setSize(map_range(_weight, 70, 100, 12, 16), map_range(_weight, 70, 100, 14, 20));
        _mouth_hole->setRadius(LV_RADIUS_CIRCLE);
        _mouth_hole->setHidden(false);
    }

    _mouth->setSize(size_x, size_y);
    _mouth->setRadius(radius);
}

void InefaMouth::setEmotion(const Emotion& emotion)
{
    if (getIgnoreEmotion()) {
        return;
    }

    _is_angry = emotion == Emotion::Angry;
    setWeight(_is_angry ? 100 : 0);
    setRotation(0);
}

void InefaMouth::setRotation(int rotation)
{
    Element::setRotation(rotation);

    _container->setTransformPivot(_mouth_canvas_size.x / 2, _mouth_canvas_size.y / 2);
    _container->setRotation(rotation);
}

void InefaMouth::setVisible(bool visible)
{
    Element::setVisible(visible);

    _container->setHidden(!visible);
}

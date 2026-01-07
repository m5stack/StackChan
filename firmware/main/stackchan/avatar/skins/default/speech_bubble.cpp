/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "default.h"

using namespace uitk;
using namespace uitk::lvgl_cpp;
using namespace stackchan::avatar;

static const Vector2i _container_pos  = Vector2i(0, 89);
static const Vector2i _container_size = Vector2i(320, 74);
static const Vector2i _arrow_offset   = Vector2i(40, -15);
static const int _text_mx             = 20;
static const int _bubble_min_width    = 90;
static const int _bubble_max_width    = 340;
static const int _bubble_height       = 52;
static const int _bubble_min_offset_x = 66;
static const int _bubble_max_offset_x = 0;

LV_IMAGE_DECLARE(default_bubble_arrow);

DefaultSpeechBubble::DefaultSpeechBubble(lv_obj_t* parent, lv_color_t primaryColor, lv_color_t secondaryColor,
                                         const lv_font_t* font)
{
    _container = std::make_unique<Container>(parent);
    _container->setRadius(0);
    _container->setAlign(LV_ALIGN_CENTER);
    _container->setBorderWidth(0);
    _container->setBgOpa(0);
    _container->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _container->setSize(_container_size.x, _container_size.y);
    _container->setPos(_container_pos.x, _container_pos.y);
    _container->setPadding(0, 0, 0, 0);

    _arrow = std::make_unique<Image>(_container->get());
    _arrow->setSrc(&default_bubble_arrow);
    _arrow->setAlign(LV_ALIGN_CENTER);
    _arrow->setPos(_arrow_offset.x, _arrow_offset.y);
    _arrow->setImageRecolorOpa(LV_OPA_COVER);
    _arrow->setImageRecolor(primaryColor);

    _bubble = std::make_unique<Container>(_container->get());
    _bubble->setRadius(LV_RADIUS_CIRCLE);
    _bubble->setAlign(LV_ALIGN_CENTER);
    _bubble->setBorderWidth(0);
    _bubble->setBgColor(primaryColor);
    _bubble->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _bubble->setSize(_bubble_max_width, _bubble_height);
    _bubble->setPos(0, 11);

    _text = std::make_unique<Label>(_bubble->get());
    _text->setTextColor(secondaryColor);
    _text->setTextFont(font);
    _text->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _text->setAlign(LV_ALIGN_CENTER);
    _text->setPos(0, 0);
    _text->setWidth(320 - _text_mx * 2);
    _text->setLongMode(LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);

    clearSpeech();
}

DefaultSpeechBubble::~DefaultSpeechBubble()
{
    _text.reset();
    _bubble.reset();
    _arrow.reset();
    _container.reset();
}

void DefaultSpeechBubble::setSpeech(std::string_view text)
{
    if (text.empty()) {
        clearSpeech();
        return;
    }

    _text->setText(text);

    lv_point_t text_size;
    lv_text_get_size(&text_size, text.data(), _text->getTextFont(), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);

    int bubble_width = min(text_size.x + _text_mx * 2, _bubble_max_width);
    bubble_width     = max(bubble_width, _bubble_min_width);

    auto bubble_offset_x =
        map_range(bubble_width, _bubble_min_width, _bubble_max_width, _bubble_min_offset_x, _bubble_max_offset_x);

    _bubble->setWidth(bubble_width);
    _bubble->setX(bubble_offset_x);

    setVisible(true);
}

void DefaultSpeechBubble::clearSpeech()
{
    _text->setText("");
    setVisible(false);
}

void DefaultSpeechBubble::setVisible(bool visible)
{
    SpeechBubble::setVisible(visible);

    _container->setHidden(!visible);
}

void DefaultSpeechBubble::setTextFont(void* font)
{
    if (_text && font) {
        _text->setTextFont((lv_font_t*)font);
    }
}

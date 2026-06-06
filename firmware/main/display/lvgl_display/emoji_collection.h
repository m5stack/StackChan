#pragma once

#include <map>
#include <memory>
#include <string>

#include "lvgl_image.h"

class EmojiCollection {
public:
    void AddEmoji(const std::string& name, LvglImage* image);
    const LvglImage* GetEmojiImage(const char* name) const;

private:
    std::map<std::string, std::unique_ptr<LvglImage>> emoji_collection_;
};

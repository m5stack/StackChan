#include "emoji_collection.h"

#include <esp_log.h>

namespace {

constexpr char kTag[] = "EmojiCollection";

}  // namespace

void EmojiCollection::AddEmoji(const std::string& name, LvglImage* image)
{
    emoji_collection_[name] = std::unique_ptr<LvglImage>(image);
}

const LvglImage* EmojiCollection::GetEmojiImage(const char* name) const
{
    const auto it = emoji_collection_.find(name != nullptr ? name : "");
    if (it != emoji_collection_.end()) {
        return it->second.get();
    }

    ESP_LOGW(kTag, "Emoji not found: %s", name != nullptr ? name : "(null)");
    return nullptr;
}

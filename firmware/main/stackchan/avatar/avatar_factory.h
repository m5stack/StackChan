#pragma once

#include "avatar/avatar.h"
#include <memory>
#include <string_view>

namespace stackchan::avatar {

enum class AvatarSkin {
    Default,
    Inefa,
};

const char* to_string(AvatarSkin skin);
bool parse_avatar_skin(std::string_view value, AvatarSkin& skin);
std::unique_ptr<Avatar> create_avatar(AvatarSkin skin);
std::unique_ptr<Avatar> create_avatar(std::string_view skin);

}  // namespace stackchan::avatar

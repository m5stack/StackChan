#include "avatar_factory.h"
#include "skins/default/default.h"
#include "skins/inefa/inefa.h"

using namespace stackchan::avatar;

const char* stackchan::avatar::to_string(AvatarSkin skin)
{
    switch (skin) {
        case AvatarSkin::Default:
            return "default";
        case AvatarSkin::Inefa:
            return "ineffa";
    }
    return "default";
}

bool stackchan::avatar::parse_avatar_skin(std::string_view value, AvatarSkin& skin)
{
    if (value == "default") {
        skin = AvatarSkin::Default;
        return true;
    }
    if (value == "ineffa" || value == "inefa") {
        skin = AvatarSkin::Inefa;
        return true;
    }
    return false;
}

std::unique_ptr<Avatar> stackchan::avatar::create_avatar(AvatarSkin skin)
{
    switch (skin) {
        case AvatarSkin::Default:
            return std::make_unique<DefaultAvatar>();
        case AvatarSkin::Inefa:
            return std::make_unique<InefaAvatar>();
    }
    return std::make_unique<DefaultAvatar>();
}

std::unique_ptr<Avatar> stackchan::avatar::create_avatar(std::string_view skin)
{
    AvatarSkin parsed_skin = AvatarSkin::Inefa;
    parse_avatar_skin(skin, parsed_skin);
    return create_avatar(parsed_skin);
}

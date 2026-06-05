#include <application.h>

#include <assets/lang_config.h>
#include <board.h>
#include <display.h>

void Application::HandleNetworkEvent(NetworkEvent event, const std::string& data)
{
    auto* display = Board::GetInstance().GetDisplay();

    switch (event) {
        case NetworkEvent::Scanning:
            display->ShowNotification(Lang::Strings::SCANNING_WIFI, 30000);
            xEventGroupSetBits(event_group_, MAIN_EVENT_NETWORK_DISCONNECTED);
            break;
        case NetworkEvent::Connecting: {
            if (data.empty()) {
                display->SetStatus(Lang::Strings::REGISTERING_NETWORK);
            } else {
                std::string msg = Lang::Strings::CONNECT_TO;
                msg += data;
                msg += "...";
                display->ShowNotification(msg.c_str(), 30000);
            }
            break;
        }
        case NetworkEvent::Connected: {
            std::string msg = Lang::Strings::CONNECTED_TO;
            msg += data;
            display->ShowNotification(msg.c_str(), 30000);
            xEventGroupSetBits(event_group_, MAIN_EVENT_NETWORK_CONNECTED);
            break;
        }
        case NetworkEvent::Disconnected:
            xEventGroupSetBits(event_group_, MAIN_EVENT_NETWORK_DISCONNECTED);
            break;
        case NetworkEvent::WifiConfigModeEnter:
        case NetworkEvent::WifiConfigModeExit:
            break;
        case NetworkEvent::ModemDetecting:
            display->SetStatus(Lang::Strings::DETECTING_MODULE);
            break;
        case NetworkEvent::ModemErrorNoSim:
            Alert(Lang::Strings::ERROR, Lang::Strings::PIN_ERROR, "triangle_exclamation", Lang::Sounds::OGG_ERR_PIN);
            break;
        case NetworkEvent::ModemErrorRegDenied:
            Alert(Lang::Strings::ERROR, Lang::Strings::REG_ERROR, "triangle_exclamation", Lang::Sounds::OGG_ERR_REG);
            break;
        case NetworkEvent::ModemErrorInitFailed:
            Alert(Lang::Strings::ERROR, Lang::Strings::MODEM_INIT_ERROR, "triangle_exclamation",
                  Lang::Sounds::OGG_EXCLAMATION);
            break;
        case NetworkEvent::ModemErrorTimeout:
            display->SetStatus(Lang::Strings::REGISTERING_NETWORK);
            break;
    }
}

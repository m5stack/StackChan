#include "stackchan_camera.h"
#include "assets/assets.h"
#include "hal_bridge.h"

bool StackChanCamera::Capture()
{
    hal_bridge::app_play_sound(OGG_CAMERA_SHUTTER);
    return EspVideo::Capture();
}

bool StackChanCamera::StreamCaptures()
{
    return CaptureFrame(false, 0);
}

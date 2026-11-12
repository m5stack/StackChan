#include "apps/common/home_indicator/bottom_swipe.h"
#include <cassert>

int main() {
    view::BottomSwipe swipe;
    assert(!swipe.update(true, 160, 205));
    assert(swipe.update(true, 165, 160));
    assert(!swipe.update(true, 165, 130));  // Once per touch.
    assert(!swipe.update(false, 165, 130));

    assert(!swipe.update(true, 160, 180));  // Normal content scroll.
    assert(!swipe.update(true, 160, 230));  // Entering bottom later must not arm it.
    assert(!swipe.update(true, 160, 100));
    assert(!swipe.update(false, 160, 100));

    assert(!swipe.update(true, 160, 230));
    assert(!swipe.update(false, 160, 205)); // Short tap/drag.
    assert(!swipe.update(true, 160, 230));
    assert(!swipe.update(false, 230, 180)); // Horizontal-dominant.
    assert(!swipe.update(true, 160, 230));
    assert(swipe.update(false, 165, 170));  // Fast swipe, last sample at release.
    assert(!swipe.update(true, 160, -1));
    assert(!swipe.update(false, 160, 100));
}

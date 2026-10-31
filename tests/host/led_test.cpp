#include "utils.h"
#include <cassert>

int main() {
    Utils::setLed(30, 10, 5);
    assert(red == 30 && green == 10 && blue == 5);
    assert(rmtLocks == 0 && releases == 1);
    assert(gpioMode == OUTPUT && gpioLevel == LOW);
    // Turning the LED off must also release RMT, as must repeated updates.
    for (int i = 0; i < 10; ++i) {
        Utils::setLed(0, 0, 0);
        assert(rmtLocks == 0 && releases == writes);
    }
    Utils::flashLed(0, 30, 0, 1000);
    assert(red == 0 && green == 0 && blue == 0);
    assert(rmtLocks == 0 && releases == writes);
}

#pragma once
#include "Arduino.h"
inline bool rmtDeinit(int) {
    assert(rmtLocks == 1);
    --rmtLocks;
    ++releases;
    return true;
}

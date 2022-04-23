#include "util.h"

namespace util {

int64_t NowMs() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return int64_t(tv.tv_sec * 1000) + tv.tv_usec / 1000;
}

}
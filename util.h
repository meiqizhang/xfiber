#ifndef UTIL_H_
#define UTIL_H_

#include <unistd.h>
#include <inttypes.h>
#include <sys/time.h>

namespace util {

int64_t NowMs();

}

#endif
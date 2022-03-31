#include "fd.h"


uint32_t BaseFD::next_seq_ = 0;


BaseFD::BaseFD() {
    seq_ = BaseFD::next_seq_++;
}

BaseFD::~BaseFD() {

}


int BaseFD::RawFd() {
    return fd_;
}

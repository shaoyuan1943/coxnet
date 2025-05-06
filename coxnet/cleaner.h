#ifndef CLEANER_H
#define CLEANER_H

#include "io_def.h"

#include <set>

namespace coxnet {
    class Poller;
    struct Cleaner {
    public:
        void push_handle(socket_t handle) { clean_handles_.emplace(handle); }
    private:
        friend class Poller;
        std::set<socket_t> clean_handles_;
    };
}

#endif // CLEANER_H
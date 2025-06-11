#ifndef POLLER_MAC_H
#define POLLER_MAC_H

#ifdef __APPLE__

#include "io_def.h"
#include "poller.h"

namespace coxnet {
  class Poller final : public IPoller {
  private:
    int kqueue_fd_ = -1;
    struct kevent* kqueue_events_ = nullptr;
  };
}

#endif // __APPLE__

#endif // POLLER_MAC_H
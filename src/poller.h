#ifndef POLLER_H
#define POLLER_H

namespace coxnet {
    class Poller {
    public:
        Poller();
        ~Poller();

        Poller(const Poller&) = delete;
        Poller& operator=(const Poller&) = delete;
        Poller(Poller&& other) = delete;
        Poller& operator=(Poller&& other) = delete;



    };
}
#endif //POLLER_H

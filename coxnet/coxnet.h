#ifndef COXNET_H
#define COXNET_H

#include "buffer.h"
#include "io_def.h"

#ifdef _WIN32
#include "poller_windows.h"
#endif

#ifdef __linux__
#include "poller_linux.h"
#endif

#endif
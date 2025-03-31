#ifndef COXNET_H
#define COXNET_H

#include "buffer.h"
#include "co_type.h"
#include "io_def.h"

#ifdef _WIN32
#include "poller.h"
#endif

#ifdef __linux__
#include "poller_linux.h"
#endif

#endif
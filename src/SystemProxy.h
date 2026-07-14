#ifndef SYSTEMPROXY_H
#define SYSTEMPROXY_H

#if defined(Q_OS_WIN)
#include "WindowsSystemProxy.h"
using SystemProxy = WindowsSystemProxy;
#elif defined(Q_OS_MACOS)
#include "MacSystemProxy.h"
using SystemProxy = MacSystemProxy;
#endif

#endif

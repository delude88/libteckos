#pragma once

#ifdef USE_IX_WEBSOCKET
#include <ixwebsocket/IXNetSystem.h>
#endif

namespace teckos {
class global {
 private:
  global() {
#ifdef USE_IX_WEBSOCKET
    ix::initNetSystem();
#endif
  }
  ~global() {
#ifdef USE_IX_WEBSOCKET
    ix::uninitNetSystem();
#endif
  }

 public:
  static global &init() {
    static global instance;
    return instance;
  }
  global(const global &) = delete;
  void operator=(const global &) = delete;
};
}
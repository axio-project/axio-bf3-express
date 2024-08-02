/**
 * @file app_context.h
 * @brief The context of an application contains the app registered functions and states.
 */

#pragma once
#include "common.h"

namespace nicc
{
/**
 * ----------------------General definations----------------------
 */ 

class AppContext {
  /**
   * ----------------------Parameters in Context level----------------------
   */ 

  /**
   * ----------------------Context internal structures----------------------
   */ 
  struct AppState {
    // App states
    char* mem_pool;
    uint32_t mem_pool_size;
  };
  struct AppDataPathFunc {
    // callback when receiving a packet/message
  };
  struct AppStateUpdateFunc {
    // update app states in runtime
  };

  /**
   * ----------------------Context methods----------------------
   */ 

  public:
    AppContext();
    ~AppContext();

  /**
   * ----------------------Util methods----------------------
   */ 

  /**
   * ----------------------Internal Parameters----------------------
   */   
  private:
  /**
   * ----------------------Internal Methods----------------------
   */   
};
} // namespace nicc

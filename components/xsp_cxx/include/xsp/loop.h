// Copyright 2019 Tricot Inc.
// Use of this source code is governed by the license in the LICENSE file.

#ifndef XSP_CXX_INCLUDE_XSP_LOOP_H_
#define XSP_CXX_INCLUDE_XSP_LOOP_H_

#include "xsp/loop_event_handler.h"
#include "xsp_loop.h"

namespace xsp {

class Loop final {
public:
    explicit Loop(LoopEventHandler* loop_event_handler);
    ~Loop();

    // Copy and move not supported.
    Loop(const Loop&) = delete;
    Loop& operator=(const Loop&) = delete;

    void Run();
    void Stop();

    LoopEventHandler* loop_event_handler() const { return loop_event_handler_; }
    void set_loop_event_handler(LoopEventHandler* loop_event_handler) {
        loop_event_handler_ = loop_event_handler;
    }

    xsp_loop_handle_t handle() { return handle_; }

private:
    static void OnLoopStartThunk(xsp_loop_handle_t loop, void* ctx);
    void OnLoopStart();

    static void OnLoopStopThunk(xsp_loop_handle_t loop, void* ctx);
    void OnLoopStop();

    static void OnLoopIdleThunk(xsp_loop_handle_t loop, void* ctx);
    void OnLoopIdle();

    xsp_loop_handle_t handle_ = nullptr;
    LoopEventHandler* loop_event_handler_;
};

}  // namespace xsp

#endif  // XSP_CXX_INCLUDE_XSP_LOOP_H_

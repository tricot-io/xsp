// Copyright 2019 Tricot Inc.
// Use of this source code is governed by the license in the LICENSE file.

#include "xsp/loop.h"

#include <assert.h>

#include "esp_err.h"

namespace xsp {

Loop::Loop(LoopEventHandler* loop_event_handler) : loop_event_handler_(loop_event_handler) {
    xsp_loop_event_handler_t loop_evt_handler = {&Loop::OnLoopStartThunk, &Loop::OnLoopStopThunk,
                                                 &Loop::OnLoopIdleThunk, this};
    handle_ = xsp_loop_init(nullptr, &loop_evt_handler);
    assert(handle_);
}

Loop::~Loop() {
    auto err = xsp_loop_cleanup(handle_);
    assert(err == ESP_OK);
}

void Loop::Run() {
    auto err = xsp_loop_run(handle_);
    assert(err == ESP_OK);
}

void Loop::Stop() {
    auto err = xsp_loop_stop(handle_);
    assert(err == ESP_OK);
}

// static
void Loop::OnLoopStartThunk(xsp_loop_handle_t loop, void* ctx) {
    static_cast<Loop*>(ctx)->OnLoopStart();
}

void Loop::OnLoopStart() {
    if (loop_event_handler_)
        loop_event_handler_->OnLoopStarted();
}

// static
void Loop::OnLoopStopThunk(xsp_loop_handle_t loop, void* ctx) {
    static_cast<Loop*>(ctx)->OnLoopStop();
}

void Loop::OnLoopStop() {
    if (loop_event_handler_)
        loop_event_handler_->OnLoopStopped();
}

// static
void Loop::OnLoopIdleThunk(xsp_loop_handle_t loop, void* ctx) {
    static_cast<Loop*>(ctx)->OnLoopIdle();
}

void Loop::OnLoopIdle() {
    if (loop_event_handler_)
        loop_event_handler_->OnLoopIdle();
}

}  // namespace xsp

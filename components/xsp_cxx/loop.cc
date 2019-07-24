// Copyright 2019 Tricot Inc.
// Use of this source code is governed by the license in the LICENSE file.

#include "xsp/loop.h"

#include <assert.h>
#include <string.h>

#include <utility>

#include "esp_err.h"

#include "xsp_eventfd.h"

namespace xsp {

Loop::Loop(LoopEventHandler* loop_event_handler, size_t task_queue_size)
        : loop_event_handler_(loop_event_handler) {
    xsp_loop_event_handler_t loop_evt_handler = {&Loop::OnLoopStartThunk, &Loop::OnLoopStopThunk,
                                                 &Loop::OnLoopIdleThunk, this};
    handle_ = xsp_loop_init(nullptr, &loop_evt_handler);
    assert(handle_);

    if (task_queue_size > 0) {
        xsp_loop_events_config_t loop_events_config = {
                static_cast<int>(sizeof(std::function<void()>*)),
                static_cast<int>(task_queue_size),
        };
        xsp_loop_events_event_handler_t loop_events_evt_handler = {&Loop::OnLoopEventsEventThunk,
                                                                   this};
        loop_events_ = xsp_loop_events_init(&loop_events_config, &loop_events_evt_handler, handle_);
        assert(loop_events_);
    }
}

Loop::~Loop() {
    if (loop_events_) {
        auto err = xsp_loop_events_cleanup(loop_events_);
        assert(err == ESP_OK);
    }

    auto err = xsp_loop_cleanup(handle_);
    assert(err == ESP_OK);
}

// static
void Loop::InitEventfd() {
    xsp_eventfd_register();
}

void Loop::Run() {
    auto err = xsp_loop_run(handle_);
    assert(err == ESP_OK);
}

void Loop::Stop() {
    auto err = xsp_loop_stop(handle_);
    assert(err == ESP_OK);
}

bool Loop::PostTask(std::function<void()>&& task) {
    if (!loop_events_)
        return false;

    std::function<void()>* f = new std::function<void()>(std::move(task));
    if (xsp_loop_events_post_event(loop_events_, &f) != ESP_OK) {
        delete f;
        return false;
    }

    return true;
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

// static
void Loop::OnLoopEventsEventThunk(xsp_loop_events_handle_t loop_events, void* ctx, void* data) {
    static_cast<Loop*>(ctx)->OnLoopEventsEvent(data);
}

void Loop::OnLoopEventsEvent(void* data) {
    std::function<void()>* f;
    memcpy(&f, data, sizeof(f));
    (*f)();
    delete f;
}

}  // namespace xsp

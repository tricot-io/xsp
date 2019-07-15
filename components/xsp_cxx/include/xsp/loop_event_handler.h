// Copyright 2019 Tricot Inc.
// Use of this source code is governed by the license in the LICENSE file.

#ifndef XSP_CXX_INCLUDE_XSP_LOOP_EVENT_HANDLER_H_
#define XSP_CXX_INCLUDE_XSP_LOOP_EVENT_HANDLER_H_

namespace xsp {

class Loop;

class LoopEventHandler {
public:
    virtual void OnLoopStarted() {}
    virtual void OnLoopStopped() {}
    virtual void OnLoopIdle() {}

protected:
    LoopEventHandler() = default;
    ~LoopEventHandler() = default;
};

}  // namespace xsp

#endif  // XSP_CXX_INCLUDE_XSP_LOOP_EVENT_HANDLER_H_

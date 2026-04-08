// SPDX-License-Identifier: MPL-2.0

package zlink

/*
#include <stdint.h>
#include "zlink.h"
*/
import "C"

import (
	"bytes"
	"log"
	"runtime"
	"runtime/cgo"
	"runtime/debug"
	"strconv"
	"sync"
	"sync/atomic"
	"unsafe"
)

type callbackRegistration interface {
	close()
}

type callbackTask struct {
	label   string
	invoke  func()
	cleanup func()
}

type callbackDispatcher struct {
	mu       sync.Mutex
	cond     *sync.Cond
	queue    []*callbackTask
	closed   bool
	done     chan struct{}
	workerID atomic.Uint64
}

func newCallbackDispatcher() *callbackDispatcher {
	dispatcher := &callbackDispatcher{
		done: make(chan struct{}),
	}
	dispatcher.cond = sync.NewCond(&dispatcher.mu)
	go dispatcher.loop()
	return dispatcher
}

func (d *callbackDispatcher) enqueue(task *callbackTask) bool {
	if d == nil || task == nil {
		return false
	}
	d.mu.Lock()
	defer d.mu.Unlock()
	if d.closed {
		return false
	}
	d.queue = append(d.queue, task)
	d.cond.Signal()
	return true
}

func (d *callbackDispatcher) close() {
	if d == nil {
		return
	}
	d.mu.Lock()
	if d.closed {
		d.mu.Unlock()
		<-d.done
		return
	}
	d.closed = true
	d.cond.Broadcast()
	d.mu.Unlock()
	if d.workerID.Load() == currentGoroutineID() {
		return
	}
	<-d.done
}

func (d *callbackDispatcher) loop() {
	defer close(d.done)
	d.workerID.Store(currentGoroutineID())
	for {
		d.mu.Lock()
		for len(d.queue) == 0 && !d.closed {
			d.cond.Wait()
		}
		if len(d.queue) == 0 && d.closed {
			d.mu.Unlock()
			return
		}
		task := d.queue[0]
		copy(d.queue, d.queue[1:])
		d.queue[len(d.queue)-1] = nil
		d.queue = d.queue[:len(d.queue)-1]
		d.mu.Unlock()
		task.run()
	}
}

func currentGoroutineID() uint64 {
	var buf [64]byte
	n := runtime.Stack(buf[:], false)
	fields := bytes.Fields(buf[:n])
	if len(fields) < 2 {
		return 0
	}
	id, err := strconv.ParseUint(string(fields[1]), 10, 64)
	if err != nil {
		return 0
	}
	return id
}

func (t *callbackTask) run() {
	if t == nil || t.invoke == nil {
		return
	}
	defer func() {
		if recovered := recover(); recovered != nil {
			if t.cleanup != nil {
				t.cleanup()
			}
			log.Printf("zlink: recovered panic in %s callback: %v\n%s", t.label, recovered, debug.Stack())
		}
	}()
	t.invoke()
}

type recvCallbackState struct {
	dispatcher *callbackDispatcher
	handler    recvCallback
}

func newRecvCallbackState(handler recvCallback) *recvCallbackState {
	return &recvCallbackState{
		dispatcher: newCallbackDispatcher(),
		handler:    handler,
	}
}

func (s *recvCallbackState) close() {
	if s == nil {
		return
	}
	s.dispatcher.close()
}

type subscribeCallbackState struct {
	dispatcher *callbackDispatcher
	handler    subscribeCallback
}

func newSubscribeCallbackState(handler subscribeCallback) *subscribeCallbackState {
	return &subscribeCallbackState{
		dispatcher: newCallbackDispatcher(),
		handler:    handler,
	}
}

func (s *subscribeCallbackState) close() {
	if s == nil {
		return
	}
	s.dispatcher.close()
}

type sendReadyCallbackState struct {
	dispatcher *callbackDispatcher
	handler    sendReadyCallback
}

func newSendReadyCallbackState(handler sendReadyCallback) *sendReadyCallbackState {
	return &sendReadyCallbackState{
		dispatcher: newCallbackDispatcher(),
		handler:    handler,
	}
}

func (s *sendReadyCallbackState) close() {
	if s == nil {
		return
	}
	s.dispatcher.close()
}

type monitorCallbackState struct {
	dispatcher *callbackDispatcher
	handler    func(*MonitorEvent)
}

func newMonitorCallbackState(handler func(*MonitorEvent)) *monitorCallbackState {
	return &monitorCallbackState{
		dispatcher: newCallbackDispatcher(),
		handler:    handler,
	}
}

func (s *monitorCallbackState) close() {
	if s == nil {
		return
	}
	s.dispatcher.close()
}

type serviceMonitorCallbackState struct {
	dispatcher *callbackDispatcher
	handler    func(*ServiceMonitorEvent)
}

func newServiceMonitorCallbackState(handler func(*ServiceMonitorEvent)) *serviceMonitorCallbackState {
	return &serviceMonitorCallbackState{
		dispatcher: newCallbackDispatcher(),
		handler:    handler,
	}
}

func (s *serviceMonitorCallbackState) close() {
	if s == nil {
		return
	}
	s.dispatcher.close()
}

func releaseCallbackHandle(handle cgo.Handle) {
	if handle == 0 {
		return
	}
	if registration, ok := handle.Value().(callbackRegistration); ok {
		registration.close()
	}
	handle.Delete()
}

//export goZlinkRecvTrampoline
func goZlinkRecvTrampoline(sourceRID *C.zlink_routing_id_t, parts *C.zlink_msg_t, partCount C.size_t, userdata C.uintptr_t) {
	state := cgo.Handle(userdata).Value().(*recvCallbackState)
	received := &Received{
		routingID: routingIDFromC(*sourceRID),
		parts:     mustTakeParts(parts, partCount),
	}
	if state.dispatcher.enqueue(&callbackTask{
		label: "receive",
		invoke: func() {
			state.handler(received)
		},
		cleanup: func() {
			_ = received.Close()
		},
	}) {
		return
	}
	_ = received.Close()
}

//export goZlinkSubscribeTrampoline
func goZlinkSubscribeTrampoline(sourceRID *C.zlink_routing_id_t, topic *C.char, topicLen C.size_t, parts *C.zlink_msg_t, partCount C.size_t, userdata C.uintptr_t) {
	state := cgo.Handle(userdata).Value().(*subscribeCallbackState)
	message := &TopicMessage{
		routingID: routingIDFromC(*sourceRID),
		topic:     C.GoStringN(topic, C.int(topicLen)),
		parts:     mustTakeParts(parts, partCount),
	}
	if state.dispatcher.enqueue(&callbackTask{
		label: "subscribe",
		invoke: func() {
			state.handler(message)
		},
		cleanup: func() {
			_ = message.Close()
		},
	}) {
		return
	}
	_ = message.Close()
}

//export goZlinkSendReadyTrampoline
func goZlinkSendReadyTrampoline(_ unsafe.Pointer, userdata C.uintptr_t) {
	state := cgo.Handle(userdata).Value().(*sendReadyCallbackState)
	state.dispatcher.enqueue(&callbackTask{
		label: "send-ready",
		invoke: func() {
			state.handler()
		},
	})
}

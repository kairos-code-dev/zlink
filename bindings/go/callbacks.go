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
	reply      func(RoutingID, RoutingID, uint64) func(SendFlags, []*Message) error
}

func newRecvCallbackState(handler recvCallback, reply func(RoutingID, RoutingID, uint64) func(SendFlags, []*Message) error) *recvCallbackState {
	return &recvCallbackState{
		dispatcher: newCallbackDispatcher(),
		handler:    handler,
		reply:      reply,
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

type spotRoutedCallbackState struct {
	dispatcher *callbackDispatcher
	handler    func(RoutingID, RoutingID, uint64, []*Message)
}

func newSpotRoutedCallbackState(handler func(RoutingID, RoutingID, uint64, []*Message)) *spotRoutedCallbackState {
	return &spotRoutedCallbackState{
		dispatcher: newCallbackDispatcher(),
		handler:    handler,
	}
}

func (s *spotRoutedCallbackState) close() {
	if s == nil {
		return
	}
	s.dispatcher.close()
}

type spotDispatchCallbackState struct {
	dispatcher *callbackDispatcher
	spot       *Spot
	handler    func(*Spot, SpotDispatchInfo)
}

func newSpotDispatchCallbackState(spot *Spot, handler func(*Spot, SpotDispatchInfo)) *spotDispatchCallbackState {
	return &spotDispatchCallbackState{
		dispatcher: newCallbackDispatcher(),
		spot:       spot,
		handler:    handler,
	}
}

func (s *spotDispatchCallbackState) close() {
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

type streamPacketCallbackState struct {
	dispatcher *callbackDispatcher
	handler    func(RoutingID, *Message, *Message)
}

func newStreamPacketCallbackState(handler func(RoutingID, *Message, *Message)) *streamPacketCallbackState {
	return &streamPacketCallbackState{
		dispatcher: newCallbackDispatcher(),
		handler:    handler,
	}
}

func (s *streamPacketCallbackState) close() {
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

func safeHandleValue(userdata C.uintptr_t) (value any, ok bool) {
	defer func() {
		if recover() != nil {
			value = nil
			ok = false
		}
	}()
	return cgo.Handle(userdata).Value(), true
}

//export goZlinkRecvTrampoline
func goZlinkRecvTrampoline(sourceRID *C.zlink_routing_id_t, parts *C.zlink_msg_t, partCount C.size_t, userdata C.uintptr_t) {
	value, ok := safeHandleValue(userdata)
	if !ok {
		return
	}
	state := value.(*recvCallbackState)
	received := &Received{
		routingID: routingIDFromCPtr(sourceRID),
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

//export goZlinkRouterRecvTrampoline
func goZlinkRouterRecvTrampoline(sourceNodeRID *C.zlink_routing_id_t, sourceSpotRID *C.zlink_routing_id_t, requestSeq C.uint64_t, parts *C.zlink_msg_t, partCount C.size_t, userdata C.uintptr_t) {
	value, ok := safeHandleValue(userdata)
	if !ok {
		return
	}
	state := value.(*recvCallbackState)
	received := &Received{
		routingID:     routingIDFromCPtr(sourceNodeRID),
		spotRID:       routingIDFromCPtr(sourceSpotRID),
		parts:         mustTakeParts(parts, partCount),
		requestSeq:    uint64(requestSeq),
		hasRequestSeq: requestSeq != 0,
	}
	if state.reply != nil && requestSeq != 0 {
		received.reply = state.reply(received.routingID, received.spotRID, received.requestSeq)
	}
	if state.dispatcher.enqueue(&callbackTask{
		label: "router receive",
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
	value, ok := safeHandleValue(userdata)
	if !ok {
		return
	}
	state := value.(*subscribeCallbackState)
	message := &TopicMessage{
		routingID: routingIDFromCPtr(sourceRID),
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
	value, ok := safeHandleValue(userdata)
	if !ok {
		return
	}
	state := value.(*sendReadyCallbackState)
	state.dispatcher.enqueue(&callbackTask{
		label: "send-ready",
		invoke: func() {
			state.handler()
		},
	})
}

//export goZlinkStreamPacketTrampoline
func goZlinkStreamPacketTrampoline(_ unsafe.Pointer, sourceRID *C.zlink_routing_id_t, header *C.zlink_msg_t, body *C.zlink_msg_t, userdata C.uintptr_t) {
	value, ok := safeHandleValue(userdata)
	if !ok {
		return
	}
	state := value.(*streamPacketCallbackState)
	headerParts, err := takeParts(header, 1)
	if err != nil {
		return
	}
	bodyParts, err := takeParts(body, 1)
	if err != nil {
		MultipartClose(headerParts)
		return
	}
	if len(headerParts) != 1 || len(bodyParts) != 1 {
		MultipartClose(headerParts)
		MultipartClose(bodyParts)
		return
	}
	source := routingIDFromCPtr(sourceRID)
	headerMessage := headerParts[0]
	bodyMessage := bodyParts[0]
	if state.dispatcher.enqueue(&callbackTask{
		label: "stream-packet",
		invoke: func() {
			state.handler(source, headerMessage, bodyMessage)
		},
		cleanup: func() {
			MultipartClose([]*Message{headerMessage, bodyMessage})
		},
	}) {
		return
	}
	MultipartClose(headerParts)
	MultipartClose(bodyParts)
}

//export goZlinkSpotRoutedTrampoline
func goZlinkSpotRoutedTrampoline(sourceNodeRID *C.zlink_routing_id_t, sourceSpotRID *C.zlink_routing_id_t, requestSeq C.uint64_t, parts *C.zlink_msg_t, partCount C.size_t, userdata C.uintptr_t) {
	value, ok := safeHandleValue(userdata)
	if !ok {
		return
	}
	state := value.(*spotRoutedCallbackState)
	clonedParts, err := takeParts(parts, partCount)
	if err != nil {
		_ = err
		return
	}
	sourceNode := routingIDFromCPtr(sourceNodeRID)
	sourceSpot := routingIDFromCPtr(sourceSpotRID)
	if state.dispatcher.enqueue(&callbackTask{
		label: "spot-routed",
		invoke: func() {
			defer MultipartClose(clonedParts)
			state.handler(sourceNode, sourceSpot, uint64(requestSeq), clonedParts)
		},
	}) {
		return
	}
	MultipartClose(clonedParts)
}

//export goZlinkSpotDispatchEventTrampoline
func goZlinkSpotDispatchEventTrampoline(_ unsafe.Pointer, info *C.zlink_spot_dispatch_info_t, userdata C.uintptr_t) {
	value, ok := safeHandleValue(userdata)
	if !ok {
		return
	}
	state, ok := value.(*spotDispatchCallbackState)
	if !ok || state == nil || info == nil {
		return
	}
	dispatchInfo := SpotDispatchInfo{
		Event:       SpotDispatchEvent(info.event),
		SubjectKind: SpotDispatchSubjectKind(info.subject_kind),
		Subject:     info.subject,
	}
	// Spot dispatch callbacks must run in the native dispatch callback context.
	// Core only permits spot recv/subscribe drains while the dispatch callback
	// is active, so bouncing through an async Go worker breaks the contract and
	// surfaces EBUSY from Spot.Subscribe/RecvRouted inside the callback.
	defer func() {
		if recovered := recover(); recovered != nil {
			log.Printf(
				"zlink: recovered panic in %s callback: %v\n%s",
				"spot-dispatch",
				recovered,
				debug.Stack(),
			)
		}
	}()
	state.handler(state.spot, dispatchInfo)
}

//export goZlinkTimerTrampoline
func goZlinkTimerTrampoline(timer unsafe.Pointer, fireCount C.uint64_t, userdata C.uintptr_t) {
	value, ok := safeHandleValue(userdata)
	if !ok {
		return
	}
	state := value.(*timerCallbackState)
	state.dispatcher.enqueue(&callbackTask{
		label: "timer-fire",
		invoke: func() {
			state.handler(state.timer, uint64(fireCount))
		},
	})
}

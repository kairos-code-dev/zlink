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
	spot       *Spot
	handler    func(*Received)
}

func newSpotRoutedCallbackState(spot *Spot, handler func(*Received)) *spotRoutedCallbackState {
	return &spotRoutedCallbackState{
		dispatcher: newCallbackDispatcher(),
		spot:       spot,
		handler:    handler,
	}
}

func (s *spotRoutedCallbackState) close() {
	if s == nil {
		return
	}
	s.dispatcher.close()
}

type spotActorLifecycleCallbackState struct {
	dispatcher *callbackDispatcher
	spot       *Spot
	onJoin     func(spot *Spot, info SpotActorLifecycleInfo)
	onLeave    func(spot *Spot, info SpotActorLifecycleInfo)
}

func newSpotActorLifecycleCallbackState(spot *Spot, onJoin, onLeave func(spot *Spot, info SpotActorLifecycleInfo)) *spotActorLifecycleCallbackState {
	return &spotActorLifecycleCallbackState{
		dispatcher: newCallbackDispatcher(),
		spot:       spot,
		onJoin:     onJoin,
		onLeave:    onLeave,
	}
}

func (s *spotActorLifecycleCallbackState) close() {
	if s == nil {
		return
	}
	s.dispatcher.close()
}

func (s *spotActorLifecycleCallbackState) dispatch(isJoin bool, info SpotActorLifecycleInfo) {
	if s == nil {
		return
	}
	var handler func(*Spot, SpotActorLifecycleInfo)
	label := "spot-actor-lifecycle-leave"
	if isJoin {
		handler = s.onJoin
		label = "spot-actor-lifecycle-join"
	} else {
		handler = s.onLeave
	}
	if handler == nil {
		return
	}
	spot := s.spot
	s.dispatcher.enqueue(&callbackTask{
		label: label,
		invoke: func() {
			handler(spot, info)
		},
	})
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

// safeHandleAs looks up a cgo.Handle and type-asserts it to T in one step.
//
// The previous design returned `value any` from safeHandleValue and forced
// every trampoline to do its own `value.(*X)` assertion immediately after,
// which is on the hot path of every recv/subscribe/send-ready/packet
// callback. Folding the assertion in here removes the extra interface
// variable per call and lets the compiler see the concrete type at the
// call site.
//
// The defer/recover guard is preserved because cgo.Handle.Value panics if
// the handle was already deleted (e.g. socket close racing an in-flight
// callback dispatch); we want a swallowed callback instead of a process
// crash in that window.
func safeHandleAs[T any](userdata C.uintptr_t) (value T, ok bool) {
	defer func() {
		if recover() != nil {
			var zero T
			value = zero
			ok = false
		}
	}()
	raw := cgo.Handle(userdata).Value()
	typed, ok := raw.(T)
	if !ok {
		var zero T
		return zero, false
	}
	return typed, true
}

//export goZlinkRecvTrampoline
func goZlinkRecvTrampoline(sourceRID *C.zlink_routing_id_t, parts *C.zlink_msg_t, partCount C.size_t, userdata C.uintptr_t) {
	state, ok := safeHandleAs[*recvCallbackState](userdata)
	if !ok {
		return
	}
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
	state, ok := safeHandleAs[*recvCallbackState](userdata)
	if !ok {
		return
	}
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
	state, ok := safeHandleAs[*subscribeCallbackState](userdata)
	if !ok {
		return
	}
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
	state, ok := safeHandleAs[*sendReadyCallbackState](userdata)
	if !ok {
		return
	}
	state.dispatcher.enqueue(&callbackTask{
		label: "send-ready",
		invoke: func() {
			state.handler()
		},
	})
}

//export goZlinkStreamPacketTrampoline
func goZlinkStreamPacketTrampoline(_ unsafe.Pointer, sourceRID *C.zlink_routing_id_t, header *C.zlink_msg_t, body *C.zlink_msg_t, userdata C.uintptr_t) {
	state, ok := safeHandleAs[*streamPacketCallbackState](userdata)
	if !ok {
		return
	}
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
	state, ok := safeHandleAs[*spotRoutedCallbackState](userdata)
	if !ok {
		return
	}
	clonedParts, err := takeParts(parts, partCount)
	if err != nil {
		_ = err
		return
	}
	sourceNode := routingIDFromCPtr(sourceNodeRID)
	sourceSpot := routingIDFromCPtr(sourceSpotRID)
	received := &Received{
		routingID:     sourceNode,
		spotRID:       sourceSpot,
		parts:         clonedParts,
		requestSeq:    uint64(requestSeq),
		hasRequestSeq: requestSeq != 0,
	}
	if received.hasRequestSeq && state.spot != nil {
		received.reply = receivedReplyToSpot(state.spot, sourceNode, sourceSpot, uint64(requestSeq))
	}
	if state.dispatcher.enqueue(&callbackTask{
		label: "spot-routed",
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

//export goZlinkSpotDispatchEventTrampoline
func goZlinkSpotDispatchEventTrampoline(_ unsafe.Pointer, info *C.zlink_spot_dispatch_info_t, userdata C.uintptr_t) {
	if info == nil {
		return
	}
	state, ok := safeHandleAs[*spotDispatchCallbackState](userdata)
	if !ok || state == nil {
		return
	}
	nodeHandle := state.spot.core.owner.handle
	subjectKind := SpotDispatchSubjectKind(info.subject_kind)
	dispatchInfo := SpotDispatchInfo{
		Event:       SpotDispatchEvent(info.event),
		SubjectKind: subjectKind,
		nodeHandle:  nodeHandle,
	}
	switch subjectKind {
	case SpotDispatchSubjectTimer:
		if info.subject != nil {
			dispatchInfo.Timer = borrowedTimer(info.subject)
		}
	case SpotDispatchSubjectChannelDealer:
		if info.subject != nil {
			if d := state.spot.core.owner.lookupDealer(info.subject); d != nil {
				dispatchInfo.ChannelDealer = d
			} else {
				dispatchInfo.ChannelDealer = borrowedDealerSocket(info.subject)
			}
		}
	case SpotDispatchSubjectActor:
		if info.subject != nil {
			ref := actorRefFromC(*(*C.zlink_actor_ref_t)(info.subject))
			dispatchInfo.Actor = &ref
		}
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
	state, ok := safeHandleAs[*timerCallbackState](userdata)
	if !ok {
		return
	}
	state.dispatcher.enqueue(&callbackTask{
		label: "timer-fire",
		invoke: func() {
			state.handler(state.timer, uint64(fireCount))
		},
	})
}

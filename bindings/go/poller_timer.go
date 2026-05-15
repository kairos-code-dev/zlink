// SPDX-License-Identifier: MPL-2.0

package zlink

/*
#include <stdint.h>
#include "zlink.h"

extern void goZlinkTimerTrampoline(void *timer_, uint64_t fire_count_, uintptr_t userdata_);

static inline int zlink_timer_handler_go_local(void *timer, uintptr_t userdata) {
	return zlink_timer_handler(timer, (zlink_timer_handler_fn)goZlinkTimerTrampoline, (void *)userdata);
}

static inline void *zlink_userdata_from_handle(uintptr_t handle) {
	return (void *)handle;
}

static inline uintptr_t zlink_handle_from_userdata(void *userdata) {
	return (uintptr_t)userdata;
}
*/
import "C"

import (
	"runtime/cgo"
	"sync"
	"time"
	"unsafe"
)

// PollEventFlag is a bitmask of poll readiness event flags.
type PollEventFlag int16

const (
    PollIn         PollEventFlag = 1
    PollOut        PollEventFlag = 2
    PollCompletion PollEventFlag = 32
)

// PollSourceKind identifies the kind of source in a PollEvent.
type PollSourceKind int

type PollItem struct {
	Socket  SocketTarget
	Fd      int
	Events  PollEventFlag
	REvents PollEventFlag
}

type PollEvent struct {
	SourceKind PollSourceKind
	Socket     SocketTarget
	Fd         int
	Timer      *Timer
	UserData   interface{}
	Events     PollEventFlag
}

type pollerEntryKind int

const (
	pollerEntrySocket pollerEntryKind = iota + 1
	pollerEntryFD
	pollerEntryTimer
)

type pollerEntry struct {
	kind   pollerEntryKind
	socket SocketTarget
	fd     int
	timer  *Timer
	handle cgo.Handle
}

type Poller struct {
	handle  unsafe.Pointer
	mu      sync.Mutex
	sockets map[uintptr]*pollerEntry
	fds     map[int]*pollerEntry
	timers  map[uintptr]*pollerEntry
	closed  bool
}

type timerCallbackState struct {
	dispatcher *callbackDispatcher
	timer      *Timer
	handler    func(*Timer, uint64)
}

func newTimerCallbackState(timer *Timer, handler func(*Timer, uint64)) *timerCallbackState {
	return &timerCallbackState{
		dispatcher: newCallbackDispatcher(),
		timer:      timer,
		handler:    handler,
	}
}

func (s *timerCallbackState) close() {
	if s == nil {
		return
	}
	s.dispatcher.close()
}

type Timer struct {
	handle   unsafe.Pointer
	closed   bool
	callback cgo.Handle
}

func NewTimer() (*Timer, error) {
	handle := C.zlink_timer_new()
	if handle == nil {
		return nil, configErrorFromErrno(currentErrno())
	}
	return &Timer{handle: handle}, nil
}

func NewTimerFromSpot(spot *Spot) (*Timer, error) {
	if spot == nil || spot.core == nil || spot.core.closed {
		return nil, &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	handle := C.zlink_spot_timer_new(spot.raw())
	if handle == nil {
		return nil, configErrorFromErrno(currentErrno())
	}
	return &Timer{handle: handle}, nil
}

func (t *Timer) raw() unsafe.Pointer {
	if t == nil {
		return nil
	}
	return t.handle
}

func (t *Timer) Start(intervalNs, repeatCount uint64) error {
	if t == nil || t.closed || t.handle == nil {
		return &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	return configErrorFromResult(C.zlink_timer_start(t.handle, C.uint64_t(intervalNs), C.uint64_t(repeatCount)))
}

func (t *Timer) Stop() error {
	if t == nil || t.closed || t.handle == nil {
		return &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	return configErrorFromResult(C.zlink_timer_stop(t.handle))
}

// Recv drains the next timer fire. Returns (count, true, nil) when data is
// available, (0, false, nil) when no timer fire is pending (EAGAIN), or
// (0, false, err) on error. Value-return form is allowed for monitor/timer
// control-plane APIs by doc/spec/bindings/go/README.md §Receive And Subscribe Shape.
func (t *Timer) Recv() (uint64, bool, error) {
	if t == nil || t.closed || t.handle == nil {
		return 0, false, &RecvError{Result: RecvTerminated, internalErrno: int(C.EFAULT)}
	}
	var fireCount C.uint64_t
	rc := C.zlink_timer_recv(t.handle, &fireCount)
	if rc == C.zlink_recv_result_t(RecvNoData) {
		return 0, false, nil
	}
	if err := recvErrorFromResult(rc); err != nil {
		return 0, false, err
	}
	return uint64(fireCount), true, nil
}

func (t *Timer) OnFire(handler func(timer *Timer, fireCount uint64)) error {
	if handler == nil {
		return &HandlerError{Result: HandlerInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	if t == nil || t.closed || t.handle == nil {
		return &HandlerError{Result: HandlerInvalidArgument, internalErrno: int(C.EFAULT)}
	}
	state := newTimerCallbackState(t, handler)
	handle := cgo.NewHandle(state)
	if err := handlerErrorFromResult(C.zlink_timer_handler_go_local(t.handle, C.uintptr_t(handle))); err != nil {
		state.close()
		handle.Delete()
		return err
	}
	if t.callback != 0 {
		releaseCallbackHandle(t.callback)
	}
	t.callback = handle
	return nil
}

func (t *Timer) Close() error {
	if t == nil || t.closed || t.handle == nil {
		return nil
	}
	handle := t.handle
	if err := closeErrorFromResult(C.zlink_timer_destroy(&handle)); err != nil {
		return err
	}
	if t.callback != 0 {
		releaseCallbackHandle(t.callback)
		t.callback = 0
	}
	t.handle = nil
	t.closed = true
	return nil
}

func NewPoller() (*Poller, error) {
	handle := C.zlink_poller_new()
	if handle == nil {
		return nil, configErrorFromErrno(currentErrno())
	}
	return &Poller{
		handle:  handle,
		sockets: make(map[uintptr]*pollerEntry),
		fds:     make(map[int]*pollerEntry),
		timers:  make(map[uintptr]*pollerEntry),
	}, nil
}

func (p *Poller) raw() unsafe.Pointer {
	if p == nil {
		return nil
	}
	return p.handle
}

func (p *Poller) Size() int {
	if p == nil || p.closed || p.handle == nil {
		return 0
	}
	var err C.zlink_config_result_t
	size := C.zlink_poller_size(p.handle, &err)
	if err != 0 {
		return 0
	}
	return int(size)
}

func (p *Poller) AddSocket(socket SocketTarget, events PollEventFlag, userData ...interface{}) error {
	if p == nil || p.closed || p.handle == nil {
		return &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	raw, err := socketHandle(socket)
	if err != nil {
		return err
	}
	entry := p.makeEntry(pollerEntrySocket, socket, 0, nil, userData)
	if err := configErrorFromResult(C.zlink_poller_add(p.handle, raw, entry.userDataPtr(), C.short(events))); err != nil {
		entry.close()
		return err
	}
	p.mu.Lock()
	defer p.mu.Unlock()
	if p.closed {
		entry.close()
		return &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	if old := p.sockets[uintptr(raw)]; old != nil {
		old.close()
	}
	p.sockets[uintptr(raw)] = entry
	return nil
}

func (p *Poller) ModifySocket(socket SocketTarget, events PollEventFlag) error {
	if p == nil || p.closed || p.handle == nil {
		return &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	raw, err := socketHandle(socket)
	if err != nil {
		return err
	}
	return configErrorFromResult(C.zlink_poller_modify(p.handle, raw, C.short(events)))
}

func (p *Poller) RemoveSocket(socket SocketTarget) error {
	if p == nil || p.closed || p.handle == nil {
		return &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	raw, err := socketHandle(socket)
	if err != nil {
		return err
	}
	if err := configErrorFromResult(C.zlink_poller_remove(p.handle, raw)); err != nil {
		return err
	}
	p.mu.Lock()
	defer p.mu.Unlock()
	if entry := p.sockets[uintptr(raw)]; entry != nil {
		entry.close()
		delete(p.sockets, uintptr(raw))
	}
	return nil
}

func (p *Poller) AddFd(fd int, events PollEventFlag, userData ...interface{}) error {
	if p == nil || p.closed || p.handle == nil {
		return &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	entry := p.makeEntry(pollerEntryFD, nil, fd, nil, userData)
	if err := configErrorFromResult(C.zlink_poller_add_fd(p.handle, C.zlink_fd_t(fd), entry.userDataPtr(), C.short(events))); err != nil {
		entry.close()
		return err
	}
	p.mu.Lock()
	defer p.mu.Unlock()
	if p.closed {
		entry.close()
		return &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	if old := p.fds[fd]; old != nil {
		old.close()
	}
	p.fds[fd] = entry
	return nil
}

func (p *Poller) ModifyFd(fd int, events PollEventFlag) error {
	if p == nil || p.closed || p.handle == nil {
		return &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	return configErrorFromResult(C.zlink_poller_modify_fd(p.handle, C.zlink_fd_t(fd), C.short(events)))
}

func (p *Poller) RemoveFd(fd int) error {
	if p == nil || p.closed || p.handle == nil {
		return &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	if err := configErrorFromResult(C.zlink_poller_remove_fd(p.handle, C.zlink_fd_t(fd))); err != nil {
		return err
	}
	p.mu.Lock()
	defer p.mu.Unlock()
	if entry := p.fds[fd]; entry != nil {
		entry.close()
		delete(p.fds, fd)
	}
	return nil
}

func (p *Poller) AddTimer(timer *Timer, userData ...interface{}) error {
	if p == nil || p.closed || p.handle == nil {
		return &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	if timer == nil || timer.closed || timer.handle == nil {
		return &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	entry := p.makeEntry(pollerEntryTimer, nil, 0, timer, userData)
	if err := configErrorFromResult(C.zlink_poller_add_timer(p.handle, timer.handle, entry.userDataPtr())); err != nil {
		entry.close()
		return err
	}
	p.mu.Lock()
	defer p.mu.Unlock()
	if p.closed {
		entry.close()
		return &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	if old := p.timers[uintptr(timer.handle)]; old != nil {
		old.close()
	}
	p.timers[uintptr(timer.handle)] = entry
	return nil
}

func (p *Poller) RemoveTimer(timer *Timer) error {
	if p == nil || p.closed || p.handle == nil {
		return &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	if timer == nil || timer.handle == nil {
		return &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	if err := configErrorFromResult(C.zlink_poller_remove_timer(p.handle, timer.handle)); err != nil {
		return err
	}
	p.mu.Lock()
	defer p.mu.Unlock()
	if entry := p.timers[uintptr(timer.handle)]; entry != nil {
		entry.close()
		delete(p.timers, uintptr(timer.handle))
	}
	return nil
}

func (p *Poller) Wait(timeout time.Duration) (*PollEvent, error) {
	if p == nil || p.closed || p.handle == nil {
		return nil, &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	var raw C.zlink_poller_event_t
	var errCode C.zlink_config_result_t
	ms, err := durationToMillis(timeout)
	if err != nil {
		return nil, err
	}
	count := C.zlink_poller_wait(p.handle, &raw, 1, C.long(ms), &errCode)
	if count < 0 {
		if errCode != 0 {
			if errCode == C.ZLINK_CONFIG_INTERNAL_ERROR && currentErrno() == int(C.EINTR) {
				return nil, nil
			}
			return nil, configErrorFromResult(errCode)
		}
		if currentErrno() == int(C.EINTR) {
			return nil, nil
		}
		return nil, configErrorFromErrno(currentErrno())
	}
	if count == 0 {
		return nil, nil
	}
	return p.eventFromC(raw), nil
}

func (p *Poller) WaitMany(timeout time.Duration) ([]PollEvent, error) {
	if p == nil || p.closed || p.handle == nil {
		return nil, &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	size := p.Size()
	if size <= 0 {
		return nil, nil
	}
	events := make([]C.zlink_poller_event_t, size)
	var errCode C.zlink_config_result_t
	ms, err := durationToMillis(timeout)
	if err != nil {
		return nil, err
	}
	count := C.zlink_poller_wait(p.handle, &events[0], C.int(size), C.long(ms), &errCode)
	if count < 0 {
		if errCode != 0 {
			if errCode == C.ZLINK_CONFIG_INTERNAL_ERROR && currentErrno() == int(C.EINTR) {
				return []PollEvent{}, nil
			}
			return nil, configErrorFromResult(errCode)
		}
		if currentErrno() == int(C.EINTR) {
			return []PollEvent{}, nil
		}
		return nil, configErrorFromErrno(currentErrno())
	}
	if count == 0 {
		return []PollEvent{}, nil
	}
	out := make([]PollEvent, 0, int(count))
	for i := 0; i < int(count) && i < len(events); i++ {
		out = append(out, *p.eventFromC(events[i]))
	}
	return out, nil
}

func (p *Poller) Close() error {
	if p == nil || p.closed || p.handle == nil {
		return nil
	}
	p.mu.Lock()
	for k, entry := range p.sockets {
		entry.close()
		delete(p.sockets, k)
	}
	for k, entry := range p.fds {
		entry.close()
		delete(p.fds, k)
	}
	for k, entry := range p.timers {
		entry.close()
		delete(p.timers, k)
	}
	p.mu.Unlock()
	handle := p.handle
	if err := closeErrorFromResult(C.zlink_poller_destroy(&handle)); err != nil {
		return err
	}
	p.handle = nil
	p.closed = true
	return nil
}

func (p *Poller) makeEntry(kind pollerEntryKind, socket SocketTarget, fd int, timer *Timer, userData []interface{}) *pollerEntry {
	entry := &pollerEntry{kind: kind, socket: socket, fd: fd, timer: timer}
	if len(userData) > 0 && userData[0] != nil {
		entry.handle = cgo.NewHandle(userData[0])
	}
	return entry
}

func (e *pollerEntry) userDataPtr() unsafe.Pointer {
	if e == nil || e.handle == 0 {
		return nil
	}
	return C.zlink_userdata_from_handle(C.uintptr_t(e.handle))
}

func (e *pollerEntry) close() {
	if e == nil || e.handle == 0 {
		return
	}
	e.handle.Delete()
	e.handle = 0
}

func (p *Poller) eventFromC(raw C.zlink_poller_event_t) *PollEvent {
	event := &PollEvent{
		SourceKind: PollSourceKind(raw.source_kind),
		Fd:         int(raw.fd),
		Events:     PollEventFlag(raw.events),
	}
	if raw.user_data != nil {
		event.UserData = cgo.Handle(C.zlink_handle_from_userdata(raw.user_data)).Value()
	}
	switch raw.source_kind {
	case C.ZLINK_POLLER_SOURCE_SOCKET:
		event.Socket = p.lookupSocket(raw.socket)
	case C.ZLINK_POLLER_SOURCE_FD:
		event.Fd = int(raw.fd)
	case C.ZLINK_POLLER_SOURCE_TIMER:
		event.Timer = p.lookupTimer(raw.timer)
	}
	return event
}

func (p *Poller) lookupSocket(handle unsafe.Pointer) SocketTarget {
	if handle == nil {
		return nil
	}
	p.mu.Lock()
	defer p.mu.Unlock()
	if entry := p.sockets[uintptr(handle)]; entry != nil {
		return entry.socket
	}
	return &pollerSocketTarget{handle: handle}
}

func (p *Poller) lookupTimer(handle unsafe.Pointer) *Timer {
	if handle == nil {
		return nil
	}
	p.mu.Lock()
	defer p.mu.Unlock()
	if entry := p.timers[uintptr(handle)]; entry != nil {
		return entry.timer
	}
	return &Timer{handle: handle}
}

type pollerSocketTarget struct {
	handle unsafe.Pointer
}

func (p *pollerSocketTarget) raw() unsafe.Pointer {
	if p == nil {
		return nil
	}
	return p.handle
}

func Poll(items []PollItem, timeout time.Duration) (int, error) {
	if len(items) == 0 {
		return 0, nil
	}
	converted := make([]C.zlink_pollitem_t, len(items))
	for i, item := range items {
		if item.Socket != nil {
			converted[i].socket = item.Socket.raw()
		}
		converted[i].fd = C.zlink_fd_t(item.Fd)
		converted[i].events = C.short(item.Events)
	}
	var errCode C.zlink_config_result_t
	ms, err := durationToMillis(timeout)
	if err != nil {
		return 0, err
	}
	count := C.zlink_poll(&converted[0], C.int(len(converted)), C.long(ms), &errCode)
	if count < 0 {
		if errCode != 0 {
			return 0, configErrorFromResult(errCode)
		}
		return 0, configErrorFromErrno(currentErrno())
	}
	for i := range items {
		items[i].REvents = PollEventFlag(converted[i].revents)
	}
	return int(count), nil
}

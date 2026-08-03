// SPDX-License-Identifier: MPL-2.0

package native

/*
#include <stdint.h>
#include "zlink.h"
*/
import "C"

import (
	"runtime"
	"sync"
	"sync/atomic"
	"unsafe"
)

// Per-handle progress pump: a single goroutine drives request progress for
// all in-flight requests on the same native handle, using a bounded poll
// interval (matches README.godoc.md: do not
// start one polling thread per request when progress can be shared per handle).
const (
	pollCompletionEvent   = C.short(32)
	progressPollTimeoutMs = C.long(50)
)

type progressPump struct {
	handle      unsafe.Pointer
	activeCount int64
	workerOn    int32
}

var (
	socketProgressPumps  sync.Map // unsafe.Pointer -> *progressPump
	externalProgressRefs sync.Map // unsafe.Pointer -> *int64
)

func startSocketRequestProgress(handle unsafe.Pointer, state *replyCallbackState) {
	if externalRequestProgressActive(handle) {
		return
	}
	getOrCreateProgressPump(&socketProgressPumps, handle).attachDone(state.done)
}

func acquireExternalRequestProgress(handle unsafe.Pointer) {
	if handle == nil {
		return
	}
	counter, _ := externalProgressRefs.LoadOrStore(handle, new(int64))
	atomic.AddInt64(counter.(*int64), 1)
}

func releaseExternalRequestProgress(handle unsafe.Pointer) {
	if handle == nil {
		return
	}
	counter, ok := externalProgressRefs.Load(handle)
	if !ok {
		return
	}
	if atomic.AddInt64(counter.(*int64), -1) <= 0 {
		externalProgressRefs.Delete(handle)
	}
}

func externalRequestProgressActive(handle unsafe.Pointer) bool {
	counter, ok := externalProgressRefs.Load(handle)
	return ok && atomic.LoadInt64(counter.(*int64)) > 0
}

func getOrCreateProgressPump(m *sync.Map, handle unsafe.Pointer) *progressPump {
	if v, ok := m.Load(handle); ok {
		return v.(*progressPump)
	}
	p := &progressPump{handle: handle}
	actual, _ := m.LoadOrStore(handle, p)
	return actual.(*progressPump)
}

func (p *progressPump) attachDone(done <-chan struct{}) {
	atomic.AddInt64(&p.activeCount, 1)
	go func() {
		<-done
		atomic.AddInt64(&p.activeCount, -1)
	}()
	if atomic.CompareAndSwapInt32(&p.workerOn, 0, 1) {
		go p.run()
	}
}

func (p *progressPump) run() {
	defer atomic.StoreInt32(&p.workerOn, 0)
	poller := C.zlink_poller_new()
	if poller == nil {
		return
	}
	defer C.zlink_poller_destroy(&poller)
	if C.zlink_poller_add(poller, p.handle, nil, pollCompletionEvent) != C.ZLINK_CONFIG_OK {
		return
	}
	defer C.zlink_poller_remove(poller, p.handle)
	var event C.zlink_poller_event_t
	var pollError C.zlink_config_result_t
	for atomic.LoadInt64(&p.activeCount) > 0 {
		if C.zlink_poller_wait(poller, &event, 1, progressPollTimeoutMs, &pollError) < 0 {
			break
		}
		runtime.Gosched()
	}
}

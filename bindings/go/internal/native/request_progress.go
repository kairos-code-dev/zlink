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
	mu          sync.Mutex
	activeCount int
	workerOn    bool
}

var (
	externalProgressRefs sync.Map // unsafe.Pointer -> *int64
)

func newProgressPump(handle unsafe.Pointer) *progressPump {
	return &progressPump{handle: handle}
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

func (p *progressPump) attachDone(done <-chan struct{}) {
	p.mu.Lock()
	p.activeCount++
	startWorker := !p.workerOn
	if startWorker {
		p.workerOn = true
	}
	p.mu.Unlock()

	go func() {
		<-done
		p.detach()
	}()
	if startWorker {
		go p.run()
	}
}

func (p *progressPump) detach() {
	p.mu.Lock()
	if p.activeCount > 0 {
		p.activeCount--
	}
	p.mu.Unlock()
}

func (p *progressPump) active() bool {
	p.mu.Lock()
	defer p.mu.Unlock()
	return p.activeCount > 0
}

func (p *progressPump) workerStopped() {
	p.mu.Lock()
	if p.activeCount > 0 {
		// A request arrived while the worker was leaving. Keep ownership of
		// the worker slot and restart it after the native poller is released.
		p.mu.Unlock()
		go p.run()
		return
	}
	p.workerOn = false
	p.mu.Unlock()
}

func (p *progressPump) run() {
	defer p.workerStopped()
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
	for p.active() {
		if C.zlink_poller_wait(poller, &event, 1, progressPollTimeoutMs, &pollError) < 0 {
			break
		}
		runtime.Gosched()
	}
}

// SPDX-License-Identifier: MPL-2.0

package native

import (
	"sync"
	"sync/atomic"
)

// replyCallbackState carries an async request's reply state across the cgo
// callback boundary. The result is stored once and completion is broadcast by
// closing done, which also wakes the per-handle progress pump.
type replyCallbackState struct {
	result atomic.Pointer[requestResult]
	done   chan struct{}
	once   sync.Once
}

func (s *replyCallbackState) complete(result requestResult) {
	s.once.Do(func() {
		stored := result
		s.result.Store(&stored)
		close(s.done)
	})
}

// wait blocks until the reply completes and returns the stored result.
func (s *replyCallbackState) wait() requestResult {
	<-s.done
	return *s.result.Load()
}

func newReplyCallbackState() *replyCallbackState {
	return &replyCallbackState{done: make(chan struct{})}
}

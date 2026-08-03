// SPDX-License-Identifier: MPL-2.0

package native

import (
	"sync"
)

// replyCallbackState carries an async request's reply state across the cgo
// callback boundary. The result is stored once and completion is broadcast by
// closing done, which also wakes the per-handle progress pump.
type replyCallbackState struct {
	result         requestResult
	done           chan struct{}
	once           sync.Once
	progressMu     sync.Mutex
	progressDetach func()
	progressDone   bool
}

func (s *replyCallbackState) complete(result requestResult) {
	s.once.Do(func() {
		s.result = result
		close(s.done)
		s.progressMu.Lock()
		s.progressDone = true
		detach := s.progressDetach
		s.progressDetach = nil
		s.progressMu.Unlock()
		if detach != nil {
			detach()
		}
	})
}

func (s *replyCallbackState) setProgressDetach(detach func()) {
	if s == nil || detach == nil {
		return
	}
	s.progressMu.Lock()
	if s.progressDone {
		s.progressMu.Unlock()
		detach()
		return
	}
	s.progressDetach = detach
	s.progressMu.Unlock()
}

// wait blocks until the reply completes and returns the stored result.
func (s *replyCallbackState) wait() requestResult {
	<-s.done
	return s.result
}

func newReplyCallbackState() *replyCallbackState {
	return &replyCallbackState{done: make(chan struct{})}
}

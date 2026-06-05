// SPDX-License-Identifier: MPL-2.0

package native

/*
#include <errno.h>
#include "zlink.h"
*/
import "C"

import (
	"context"
	"errors"
	"time"
)

// --- payload-less request-style builder (leave / destroy / bind / unbind) ---

type requestPartsBuilderState struct {
	timeout   time.Duration
	submitted bool
	submit    func(timeout time.Duration, cb requestPartsCallback) error
}

func (s *requestPartsBuilderState) doSubmitAsync() (<-chan RequestReplyCompletion, error) {
	resultCh := make(chan RequestReplyCompletion, 1)
	ok, err := s.doSubmitCallback(func(r RequestResult, parts []*Message) {
		completion := RequestReplyCompletion{Result: r, Parts: parts}
		if r != RequestOK {
			completion.Err = &RequestError{Result: r}
		}
		resultCh <- completion
		close(resultCh)
	})
	if err != nil {
		return nil, err
	}
	if !ok {
		return nil, &SubmitError{Result: SubmitBackpressured}
	}
	return resultCh, nil
}

func (s *requestPartsBuilderState) doSubmitCallback(callback requestPartsCallback) (bool, error) {
	if s.submitted {
		return false, &ConfigError{Result: ConfigInvalidState, nativeErrno: int(C.EINVAL)}
	}
	if callback == nil {
		return false, &ConfigError{Result: ConfigInvalidArgument, nativeErrno: int(C.EINVAL)}
	}
	s.submitted = true
	if err := s.submit(s.timeout, callback); err != nil {
		var submitErr *SubmitError
		if errors.As(err, &submitErr) && submitErr.Result == SubmitBackpressured {
			return false, nil
		}
		return false, err
	}
	return true, nil
}

// requestPartsBuilder is the canonical implementation that backs
// ActorLeaveOp, ActorBindOp, and ActorUnbindOp. They all share the same
// timeout+submit shape, so they share one concrete builder; the public
// type aliases above keep the per-operation names on the user API.
type requestPartsBuilder struct {
	state *requestPartsBuilderState
}

func (b *requestPartsBuilder) Timeout(timeout time.Duration) requestPartsAsyncOp {
	b.state.timeout = timeout
	return b
}

func (b *requestPartsBuilder) SubmitAsync(_ context.Context) (<-chan RequestReplyCompletion, error) {
	return b.state.doSubmitAsync()
}

func (b *requestPartsBuilder) Submit(_ context.Context, cb func(RequestResult, []*Message)) (bool, error) {
	return b.state.doSubmitCallback(cb)
}

func newActorLeaveOp(submit func(timeout time.Duration, cb requestPartsCallback) error) ActorLeaveOp {
	return &requestPartsBuilder{state: &requestPartsBuilderState{submit: submit}}
}

// actorDestroyBuilder

type actorDestroyBuilder struct {
	state     *requestPartsBuilderState
	onSuccess func()
}

func newActorDestroyOp(onSuccess func(), submit func(timeout time.Duration, cb requestPartsCallback) error) ActorDestroyOp {
	return &actorDestroyBuilder{state: &requestPartsBuilderState{submit: submit}, onSuccess: onSuccess}
}

func (b *actorDestroyBuilder) Timeout(timeout time.Duration) ActorDestroyOp {
	b.state.timeout = timeout
	return b
}

func (b *actorDestroyBuilder) SubmitAsync(ctx context.Context) (<-chan RequestReplyCompletion, error) {
	resultCh := make(chan RequestReplyCompletion, 1)
	ok, err := b.Submit(ctx, func(r RequestResult, parts []*Message) {
		completion := RequestReplyCompletion{Result: r, Parts: parts}
		if r != RequestOK {
			completion.Err = &RequestError{Result: r}
		}
		resultCh <- completion
		close(resultCh)
	})
	if err != nil {
		return nil, err
	}
	if !ok {
		return nil, &SubmitError{Result: SubmitBackpressured}
	}
	return resultCh, nil
}

func (b *actorDestroyBuilder) Submit(_ context.Context, cb func(RequestResult, []*Message)) (bool, error) {
	wrapped := cb
	if b.onSuccess != nil {
		wrapped = func(r RequestResult, parts []*Message) {
			if r == RequestOK {
				b.onSuccess()
			}
			cb(r, parts)
		}
	}
	return b.state.doSubmitCallback(wrapped)
}

func newActorBindOp(submit func(timeout time.Duration, cb requestPartsCallback) error) ActorBindOp {
	return &requestPartsBuilder{state: &requestPartsBuilderState{submit: submit}}
}

func newActorUnbindOp(submit func(timeout time.Duration, cb requestPartsCallback) error) ActorUnbindOp {
	return &requestPartsBuilder{state: &requestPartsBuilderState{submit: submit}}
}

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

// --- lookup builder ---

type actorLookupBuilderState struct {
	timeout   time.Duration
	submitted bool
	submit    func(timeout time.Duration, cb actorLookupCallback) error
}

type actorLookupBuilder struct {
	state *actorLookupBuilderState
}

func newActorLookupOp(submit func(timeout time.Duration, cb actorLookupCallback) error) ActorLookupOp {
	return &actorLookupBuilder{state: &actorLookupBuilderState{submit: submit}}
}

func (b *actorLookupBuilder) Timeout(timeout time.Duration) ActorLookupOp {
	b.state.timeout = timeout
	return b
}

func (b *actorLookupBuilder) SubmitAsync(ctx context.Context) (<-chan ActorLookupCompletion, error) {
	resultCh := make(chan ActorLookupCompletion, 1)
	ok, err := b.Submit(ctx, func(result ActorLookupResult) {
		completion := ActorLookupCompletion{Result: result}
		if result.Result != RequestOK {
			completion.Err = &RequestError{Result: result.Result}
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

func (b *actorLookupBuilder) Submit(_ context.Context, cb func(ActorLookupResult)) (bool, error) {
	if b.state.submitted {
		return false, &ConfigError{Result: ConfigInvalidState, internalErrno: int(C.EINVAL)}
	}
	if cb == nil {
		return false, &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	b.state.submitted = true
	if err := b.state.submit(b.state.timeout, cb); err != nil {
		var submitErr *SubmitError
		if errors.As(err, &submitErr) && submitErr.Result == SubmitBackpressured {
			return false, nil
		}
		return false, err
	}
	return true, nil
}

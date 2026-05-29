// SPDX-License-Identifier: MPL-2.0

package native

import (
	"context"
	"time"
)

// --- lookup builder ---

type actorLookupBuilderState struct {
	timeout time.Duration
	submitOnce
	submit func(timeout time.Duration, cb actorLookupCallback) error
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
	if cb == nil {
		return false, configInvalidArgumentError()
	}
	if err := b.state.markSubmitted(); err != nil {
		return false, err
	}
	if err := b.state.submit(b.state.timeout, cb); err != nil {
		return submitBackpressureAsNotSubmitted(err)
	}
	return true, nil
}

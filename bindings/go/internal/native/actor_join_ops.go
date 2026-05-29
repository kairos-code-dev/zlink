// SPDX-License-Identifier: MPL-2.0

package native

import (
	"context"
	"time"
)

// --- join builder implementation ---

type actorJoinBuilderState struct {
	parts   []*Message
	flags   SendFlags
	timeout time.Duration
	submitOnce
	submit func(parts []*Message, flags SendFlags, timeout time.Duration, cb actorJoinCallback) error
}

type actorJoinBuilder struct {
	state *actorJoinBuilderState
}

type actorJoinSubmitBuilder struct {
	state *actorJoinBuilderState
}

type actorJoinCallbackBuilder struct {
	state *actorJoinBuilderState
}

func newActorJoinOp(submit func(parts []*Message, flags SendFlags, timeout time.Duration, cb actorJoinCallback) error) ActorJoinOp {
	return &actorJoinBuilder{state: &actorJoinBuilderState{submit: submit}}
}

type actorJoinEntrySpotBuilderState struct {
	timeout time.Duration
	submitOnce
	submit func(timeout time.Duration, cb actorJoinEntrySpotCallback) error
}

type actorJoinEntrySpotBuilder struct {
	state *actorJoinEntrySpotBuilderState
}

func newActorJoinEntrySpotOp(submit func(timeout time.Duration, cb actorJoinEntrySpotCallback) error) ActorJoinEntrySpotOp {
	return &actorJoinEntrySpotBuilder{state: &actorJoinEntrySpotBuilderState{submit: submit}}
}

func (b *actorJoinEntrySpotBuilder) Timeout(timeout time.Duration) ActorJoinEntrySpotOp {
	b.state.timeout = timeout
	return b
}

func (b *actorJoinEntrySpotBuilder) SubmitAsync(_ context.Context) (<-chan ActorJoinEntrySpotCompletion, error) {
	resultCh := make(chan ActorJoinEntrySpotCompletion, 1)
	ok, err := b.Submit(context.Background(), func(result ActorJoinEntrySpotResult) {
		completion := ActorJoinEntrySpotCompletion{Result: result}
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

func (b *actorJoinEntrySpotBuilder) Submit(_ context.Context, callback func(ActorJoinEntrySpotResult)) (bool, error) {
	if callback == nil {
		return false, configInvalidArgumentError()
	}
	if err := b.state.markSubmitted(); err != nil {
		return false, err
	}
	if err := b.state.submit(b.state.timeout, callback); err != nil {
		return false, err
	}
	return true, nil
}

func (b *actorJoinBuilder) Message(msg *Message) ActorJoinSubmitOp {
	b.state.parts = append(b.state.parts, msg)
	return &actorJoinSubmitBuilder{state: b.state}
}

func (b *actorJoinSubmitBuilder) Message(msg *Message) ActorJoinSubmitOp {
	b.state.parts = append(b.state.parts, msg)
	return b
}

func (b *actorJoinSubmitBuilder) Timeout(timeout time.Duration) ActorJoinSubmitOp {
	b.state.timeout = timeout
	return b
}

func (b *actorJoinSubmitBuilder) Flags(flags SendFlags) ActorJoinCallbackSubmitOp {
	b.state.flags = flags
	return &actorJoinCallbackBuilder{state: b.state}
}

func (b *actorJoinSubmitBuilder) SubmitAsync(_ context.Context) (<-chan ActorJoinCompletion, error) {
	return b.state.doSubmitAsync()
}

func (b *actorJoinSubmitBuilder) Submit(_ context.Context, callback func(ActorJoinResult, []*Message)) (bool, error) {
	return b.state.doSubmitCallback(callback)
}

func (b *actorJoinCallbackBuilder) Message(msg *Message) ActorJoinCallbackSubmitOp {
	b.state.parts = append(b.state.parts, msg)
	return b
}

func (b *actorJoinCallbackBuilder) Timeout(timeout time.Duration) ActorJoinCallbackSubmitOp {
	b.state.timeout = timeout
	return b
}

func (b *actorJoinCallbackBuilder) Flags(flags SendFlags) ActorJoinCallbackSubmitOp {
	b.state.flags = flags
	return b
}

func (b *actorJoinCallbackBuilder) Submit(_ context.Context, callback func(ActorJoinResult, []*Message)) (bool, error) {
	return b.state.doSubmitCallback(callback)
}

func (s *actorJoinBuilderState) doSubmitAsync() (<-chan ActorJoinCompletion, error) {
	resultCh := make(chan ActorJoinCompletion, 1)
	ok, err := s.doSubmitCallback(func(result ActorJoinResult, parts []*Message) {
		completion := ActorJoinCompletion{Result: result, Parts: parts}
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

func (s *actorJoinBuilderState) doSubmitCallback(callback func(ActorJoinResult, []*Message)) (bool, error) {
	if len(s.parts) == 0 || callback == nil {
		return false, configInvalidArgumentError()
	}
	if err := s.markSubmitted(); err != nil {
		return false, err
	}
	if err := s.submit(s.parts, s.flags, s.timeout, callback); err != nil {
		return submitBackpressureAsNotSubmitted(err)
	}
	return true, nil
}

// --- join reply builder ---

type actorJoinReplyBuilder struct {
	parts []*Message
	submitOnce
	submit func(parts []*Message) error
}

func newActorJoinReplyOp(submit func(parts []*Message) error) ActorJoinReplyOp {
	return &actorJoinReplyBuilder{submit: submit}
}

func (b *actorJoinReplyBuilder) Message(msg *Message) ActorJoinReplyOp {
	b.parts = append(b.parts, msg)
	return b
}

func (b *actorJoinReplyBuilder) Submit(_ context.Context) error {
	if err := b.markSubmitted(); err != nil {
		return err
	}
	return b.submit(b.parts)
}

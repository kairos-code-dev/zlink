// SPDX-License-Identifier: MPL-2.0

package native

import (
	"context"
	"time"
)

// Builder interfaces for spot send/request/reply operations.

type SendOp interface {
	Message(message *Message) SendSubmitOp
	MoveMessage(message *Message) SendSubmitOp
	Bytes(data []byte) SendSubmitOp
}

type SendSubmitOp interface {
	Message(message *Message) SendSubmitOp
	MoveMessage(message *Message) SendSubmitOp
	Bytes(data []byte) SendSubmitOp
	Flags(flags SendFlags) SendSubmitOp
	Submit(ctx context.Context) (bool, error)
}

type RequestOp interface {
	Message(message *Message) RequestSubmitOp
	Bytes(data []byte) RequestSubmitOp
}

type RequestSubmitOp interface {
	Message(message *Message) RequestSubmitOp
	Bytes(data []byte) RequestSubmitOp
	Timeout(timeout time.Duration) RequestSubmitOp
	Flags(flags SendFlags) RequestCallbackSubmitOp
	SubmitAsync(ctx context.Context) (<-chan RequestReplyCompletion, error)
	Submit(ctx context.Context, callback RequestReplyCallback) (bool, error)
}

type RequestCallbackSubmitOp interface {
	Message(message *Message) RequestCallbackSubmitOp
	Bytes(data []byte) RequestCallbackSubmitOp
	Timeout(timeout time.Duration) RequestCallbackSubmitOp
	Flags(flags SendFlags) RequestCallbackSubmitOp
	Submit(ctx context.Context, callback RequestReplyCallback) (bool, error)
}

type ReplyOp interface {
	Message(message *Message) ReplySubmitOp
}

type ReplySubmitOp interface {
	Message(message *Message) ReplySubmitOp
	Flags(flags SendFlags) ReplySubmitOp
	Submit(ctx context.Context) error
}

// --- sendBuilder ---

type sendBuilder struct {
	spot  *Spot
	parts []sendBuilderPart
	flags SendFlags
	submitOnce
	submit func(parts []sendBuilderPart, flags SendFlags) error
}

type sendBuilderPart struct {
	message *Message
	data    []byte
	move    bool
	bytes   bool
}

func newSendBuilder(spot *Spot, submit func(parts []sendBuilderPart, flags SendFlags) error) SendOp {
	return &sendBuilder{spot: spot, submit: submit}
}

func (b *sendBuilder) Message(message *Message) SendSubmitOp {
	b.parts = append(b.parts, sendBuilderPart{message: message})
	return b
}

// MoveMessage adds a payload part whose ownership is transferred to the
// operation at submit time. Unlike Message, the caller must not reuse the
// message after Submit returns, even when Submit reports a send error.
func (b *sendBuilder) MoveMessage(message *Message) SendSubmitOp {
	b.parts = append(b.parts, sendBuilderPart{message: message, move: true})
	return b
}

// Bytes adds a payload part from caller-owned bytes. The slice is read during
// Submit and is not retained after Submit returns.
func (b *sendBuilder) Bytes(data []byte) SendSubmitOp {
	b.parts = append(b.parts, sendBuilderPart{data: data, bytes: true})
	return b
}

func (b *sendBuilder) Flags(flags SendFlags) SendSubmitOp {
	b.flags = flags
	return b
}

func (b *sendBuilder) Submit(_ context.Context) (bool, error) {
	if len(b.parts) == 0 {
		return false, configInvalidArgumentError()
	}
	if err := b.markSubmitted(); err != nil {
		return false, err
	}
	if err := b.submit(b.parts, b.flags); err != nil {
		return submitBackpressureAsNotSubmitted(err)
	}
	return true, nil
}

// --- requestBuilder ---

type requestBuilderState struct {
	parts   []requestBuilderPart
	flags   SendFlags
	timeout time.Duration
	submitOnce
	submit func(parts []requestBuilderPart, flags SendFlags, timeout time.Duration, callback RequestReplyCallback) error
}

type requestBuilder struct {
	state *requestBuilderState
}

type requestCallbackBuilder struct {
	state *requestBuilderState
}

type requestBuilderPart struct {
	message *Message
	data    []byte
	bytes   bool
}

func newRequestBuilder(spot *Spot, submit func(parts []requestBuilderPart, flags SendFlags, timeout time.Duration, callback RequestReplyCallback) error) RequestOp {
	return &requestBuilder{state: &requestBuilderState{submit: submit}}
}

func (b *requestBuilder) Message(message *Message) RequestSubmitOp {
	b.state.parts = append(b.state.parts, requestBuilderPart{message: message})
	return b
}

func (b *requestBuilder) Bytes(data []byte) RequestSubmitOp {
	b.state.parts = append(b.state.parts, requestBuilderPart{data: data, bytes: true})
	return b
}

func (b *requestBuilder) Timeout(timeout time.Duration) RequestSubmitOp {
	b.state.timeout = timeout
	return b
}

func (b *requestBuilder) Flags(flags SendFlags) RequestCallbackSubmitOp {
	b.state.flags = flags
	return &requestCallbackBuilder{state: b.state}
}

func (b *requestBuilder) SubmitAsync(_ context.Context) (<-chan RequestReplyCompletion, error) {
	return b.state.doSubmitAsync()
}

func (b *requestBuilder) Submit(_ context.Context, callback RequestReplyCallback) (bool, error) {
	return b.state.doSubmitCallback(callback)
}

func (b *requestCallbackBuilder) Message(message *Message) RequestCallbackSubmitOp {
	b.state.parts = append(b.state.parts, requestBuilderPart{message: message})
	return b
}

func (b *requestCallbackBuilder) Bytes(data []byte) RequestCallbackSubmitOp {
	b.state.parts = append(b.state.parts, requestBuilderPart{data: data, bytes: true})
	return b
}

func (b *requestCallbackBuilder) Timeout(timeout time.Duration) RequestCallbackSubmitOp {
	b.state.timeout = timeout
	return b
}

func (b *requestCallbackBuilder) Flags(flags SendFlags) RequestCallbackSubmitOp {
	b.state.flags = flags
	return b
}

func (b *requestCallbackBuilder) Submit(_ context.Context, callback RequestReplyCallback) (bool, error) {
	return b.state.doSubmitCallback(callback)
}

func (s *requestBuilderState) doSubmitAsync() (<-chan RequestReplyCompletion, error) {
	result := make(chan RequestReplyCompletion, 1)
	ok, err := s.doSubmitCallback(func(r RequestResult, parts []*Message) {
		completion := RequestReplyCompletion{Result: r, Parts: parts}
		if r != RequestOK {
			completion.Err = &RequestError{Result: r}
		}
		result <- completion
		close(result)
	})
	if err != nil {
		return nil, err
	}
	if !ok {
		return nil, &SubmitError{Result: SubmitBackpressured}
	}
	return result, nil
}

func (s *requestBuilderState) doSubmitCallback(callback RequestReplyCallback) (bool, error) {
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

// --- replyBuilder ---

type replyBuilder struct {
	spot  *Spot
	parts []*Message
	flags SendFlags
	submitOnce
	submit func(parts []*Message, flags SendFlags) error
}

func newReplyBuilder(spot *Spot, submit func(parts []*Message, flags SendFlags) error) ReplyOp {
	return &replyBuilder{spot: spot, submit: submit}
}

func (b *replyBuilder) Message(message *Message) ReplySubmitOp {
	b.parts = append(b.parts, message)
	return b
}

func (b *replyBuilder) Flags(flags SendFlags) ReplySubmitOp {
	b.flags = flags
	return b
}

func (b *replyBuilder) Submit(_ context.Context) error {
	if len(b.parts) == 0 {
		return configInvalidArgumentError()
	}
	if err := b.markSubmitted(); err != nil {
		return err
	}
	return b.submit(b.parts, b.flags)
}

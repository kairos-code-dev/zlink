// SPDX-License-Identifier: MPL-2.0

package zlink

/*
#include "zlink.h"
*/
import "C"

import (
	"context"
	"errors"
	"sync"
	"sync/atomic"
	"syscall"
	"time"
)

const requestReplyPollInterval = time.Millisecond
const defaultRequestTimeout = 30 * time.Second

type RequestReplyCallback func(*Received, error)
type pendingRequest struct {
	reply chan requestResult
}

type requestResult struct {
	received *Received
	err      error
}

type requestReplyCore struct {
	pending      sync.Map
	dataQueue    chan *Received
	done         chan struct{}
	closeOnce    sync.Once
	handlerMu    sync.RWMutex
	dataHandler  func(*Received)
	dispatchDone chan struct{}
}

type RequestDealer struct {
	socket       *DealerSocket
	nextID       atomic.Uint64
	core         requestReplyCore
}

type RequestRouter struct {
	socket         *RouterSocket
	nextID         atomic.Uint64
	core           requestReplyCore
}

func NewRequestDealer(socket *DealerSocket) *RequestDealer {
	rr := &RequestDealer{
		socket: socket,
		core: requestReplyCore{
			dataQueue:    make(chan *Received, 64),
			done:         make(chan struct{}),
			dispatchDone: make(chan struct{}),
		},
	}
	rr.nextID.Store(1)
	go rr.dispatchLoop()
	return rr
}

func (r *RequestDealer) Socket() *DealerSocket {
	return r.socket
}

func (r *RequestDealer) Request(ctx context.Context, parts ...*Message) (*Received, error) {
	return r.requestWith(ctx, false, parts...)
}

func (r *RequestDealer) TryRequest(ctx context.Context, parts ...*Message) (*Received, error) {
	return r.requestWith(ctx, true, parts...)
}

func (r *RequestDealer) RequestAsync(timeout time.Duration, callback RequestReplyCallback, parts ...*Message) {
	if callback == nil {
		return
	}
	go func() {
		ctx, cancel := requestContext(timeout)
		defer cancel()
		received, err := r.Request(ctx, parts...)
		callback(received, err)
	}()
}

func (r *RequestDealer) TryRequestAsync(timeout time.Duration, callback RequestReplyCallback, parts ...*Message) {
	if callback == nil {
		return
	}
	go func() {
		ctx, cancel := requestContext(timeout)
		defer cancel()
		received, err := r.TryRequest(ctx, parts...)
		callback(received, err)
	}()
}

func (r *RequestDealer) Recv() (*Received, error) {
	return r.core.recv()
}

func (r *RequestDealer) TryRecv() (*Received, bool, error) {
	return r.core.tryRecv()
}

func (r *RequestDealer) OnReceive(handler func(*Received)) error {
	return r.core.onReceive(handler)
}

func (r *RequestDealer) Close() error {
	var err error
	r.core.closeOnce.Do(func() {
		close(r.core.done)
		err = r.socket.Close()
		<-r.core.dispatchDone
		close(r.core.dataQueue)
		r.core.failAll(&ZlinkError{Kind: ErrorKindNative, Code: int(C.ETERM), Message: "socket is closed"})
	})
	return err
}

func (r *RequestDealer) requestWith(ctx context.Context, trySend bool, parts ...*Message) (*Received, error) {
	if ctx == nil {
		ctx = context.WithoutCancel(context.Background())
	}
	cloned, err := cloneParts(parts)
	if err != nil {
		return nil, err
	}
	correlationID := r.nextID.Add(1) - 1
	if err := cloned[0].SetRequest(correlationID); err != nil {
		closeMessageSlice(cloned)
		return nil, err
	}
	pending := &pendingRequest{reply: make(chan requestResult, 1)}
	r.core.pending.Store(correlationID, pending)
	if trySend {
		var result SendResult
		result, err = r.socket.TrySend(cloned...)
		if err == nil && result != SendResultSent {
			err = &ZlinkError{Kind: ErrorKindNative, Code: int(syscall.EAGAIN), Message: "request send is backpressured"}
		}
	} else {
		err = r.socket.Send(cloned...)
	}
	if err != nil {
		r.core.pending.Delete(correlationID)
		close(pending.reply)
		closeMessageSlice(cloned)
		return nil, err
	}
	return r.core.awaitPending(ctx, correlationID, pending)
}

func (r *RequestDealer) dispatchLoop() {
	defer close(r.core.dispatchDone)
	for {
		select {
		case <-r.core.done:
			return
		default:
		}
		received, ok, err := r.socket.TryRecv()
		if err != nil {
			if isClosed(err) {
				return
			}
			r.core.failAll(err)
			return
		}
		if !ok {
			time.Sleep(requestReplyPollInterval)
			continue
		}
		if err := r.routeReceived(received); err != nil {
			_ = received.Close()
		}
	}
}

func (r *RequestDealer) routeReceived(received *Received) error {
	part, err := received.SinglePartOrError()
	if err != nil {
		r.deliverData(received)
		return nil
	}
	msgType, correlationID, err := part.RequestInfo()
	if err != nil {
		return err
	}
	if msgType == MsgTypeReply {
		value, ok := r.core.pending.LoadAndDelete(correlationID)
		if !ok {
			return received.Close()
		}
		value.(*pendingRequest).reply <- requestResult{received: received}
		close(value.(*pendingRequest).reply)
		return nil
	}
	r.deliverData(received)
	return nil
}

func (r *RequestDealer) deliverData(received *Received) {
	r.core.deliverData(received)
}

func NewRequestRouter(socket *RouterSocket) *RequestRouter {
	rr := &RequestRouter{
		socket: socket,
		core: requestReplyCore{
			dataQueue:    make(chan *Received, 64),
			done:         make(chan struct{}),
			dispatchDone: make(chan struct{}),
		},
	}
	rr.nextID.Store(1)
	go rr.dispatchLoop()
	return rr
}

func (r *RequestRouter) Socket() *RouterSocket {
	return r.socket
}

func (r *RequestRouter) Request(ctx context.Context, routingID RoutingID, parts ...*Message) (*Received, error) {
	return r.requestWith(ctx, false, routingID, parts...)
}

func (r *RequestRouter) TryRequest(ctx context.Context, routingID RoutingID, parts ...*Message) (*Received, error) {
	return r.requestWith(ctx, true, routingID, parts...)
}

func (r *RequestRouter) RequestAsync(routingID RoutingID, timeout time.Duration, callback RequestReplyCallback, parts ...*Message) {
	if callback == nil {
		return
	}
	go func() {
		ctx, cancel := requestContext(timeout)
		defer cancel()
		received, err := r.Request(ctx, routingID, parts...)
		callback(received, err)
	}()
}

func (r *RequestRouter) TryRequestAsync(routingID RoutingID, timeout time.Duration, callback RequestReplyCallback, parts ...*Message) {
	if callback == nil {
		return
	}
	go func() {
		ctx, cancel := requestContext(timeout)
		defer cancel()
		received, err := r.TryRequest(ctx, routingID, parts...)
		callback(received, err)
	}()
}

func (r *RequestRouter) Reply(routingID RoutingID, correlationID uint64, parts ...*Message) error {
	cloned, err := cloneParts(parts)
	if err != nil {
		return err
	}
	if err := cloned[0].SetReply(correlationID); err != nil {
		closeMessageSlice(cloned)
		return err
	}
	return r.socket.SendTo(routingID, cloned...)
}

func (r *RequestRouter) TryReply(routingID RoutingID, correlationID uint64, parts ...*Message) (SendResult, error) {
	cloned, err := cloneParts(parts)
	if err != nil {
		return 0, err
	}
	if err := cloned[0].SetReply(correlationID); err != nil {
		closeMessageSlice(cloned)
		return 0, err
	}
	return r.socket.TrySendTo(routingID, cloned...)
}

func (r *RequestRouter) Recv() (*Received, error) {
	return r.core.recv()
}

func (r *RequestRouter) TryRecv() (*Received, bool, error) {
	return r.core.tryRecv()
}

func (r *RequestRouter) OnReceive(handler func(*Received)) error {
	return r.core.onReceive(handler)
}

func (r *RequestRouter) Close() error {
	var err error
	r.core.closeOnce.Do(func() {
		close(r.core.done)
		err = r.socket.Close()
		<-r.core.dispatchDone
		close(r.core.dataQueue)
		r.core.failAll(&ZlinkError{Kind: ErrorKindNative, Code: int(C.ETERM), Message: "socket is closed"})
	})
	return err
}

func (r *RequestRouter) requestWith(ctx context.Context, trySend bool, routingID RoutingID, parts ...*Message) (*Received, error) {
	if ctx == nil {
		ctx = context.WithoutCancel(context.Background())
	}
	cloned, err := cloneParts(parts)
	if err != nil {
		return nil, err
	}
	correlationID := r.nextID.Add(1) - 1
	if err := cloned[0].SetRequest(correlationID); err != nil {
		closeMessageSlice(cloned)
		return nil, err
	}
	pending := &pendingRequest{reply: make(chan requestResult, 1)}
	r.core.pending.Store(correlationID, pending)
	if trySend {
		var result SendResult
		result, err = r.socket.TrySendTo(routingID, cloned...)
		if err == nil && result != SendResultSent {
			err = &ZlinkError{Kind: ErrorKindNative, Code: int(syscall.EAGAIN), Message: "request send is backpressured"}
		}
	} else {
		err = r.socket.SendTo(routingID, cloned...)
	}
	if err != nil {
		r.core.pending.Delete(correlationID)
		close(pending.reply)
		closeMessageSlice(cloned)
		return nil, err
	}
	return r.core.awaitPending(ctx, correlationID, pending)
}

func (r *RequestRouter) dispatchLoop() {
	defer close(r.core.dispatchDone)
	for {
		select {
		case <-r.core.done:
			return
		default:
		}
		received, ok, err := r.socket.TryRecv()
		if err != nil {
			if isClosed(err) {
				return
			}
			r.core.failAll(err)
			return
		}
		if !ok {
			time.Sleep(requestReplyPollInterval)
			continue
		}
		if err := r.routeReceived(received); err != nil {
			_ = received.Close()
		}
	}
}

func (r *RequestRouter) routeReceived(received *Received) error {
	part, err := received.SinglePartOrError()
	if err != nil {
		r.deliverData(received)
		return nil
	}
	msgType, correlationID, err := part.RequestInfo()
	if err != nil {
		return err
	}
	switch msgType {
	case MsgTypeReply:
		value, ok := r.core.pending.LoadAndDelete(correlationID)
		if !ok {
			return received.Close()
		}
		value.(*pendingRequest).reply <- requestResult{received: received}
		close(value.(*pendingRequest).reply)
		return nil
	}
	r.deliverData(received)
	return nil
}

func (r *RequestRouter) deliverData(received *Received) {
	r.core.deliverData(received)
}

func (c *requestReplyCore) recv() (*Received, error) {
	c.handlerMu.RLock()
	handler := c.dataHandler
	c.handlerMu.RUnlock()
	if handler != nil {
		return nil, stateError("socket is in callback mode")
	}
	select {
	case received, ok := <-c.dataQueue:
		if !ok {
			return nil, &ZlinkError{Kind: ErrorKindNative, Code: int(C.ETERM), Message: "socket is closed"}
		}
		return received, nil
	case <-c.done:
		return nil, &ZlinkError{Kind: ErrorKindNative, Code: int(C.ETERM), Message: "socket is closed"}
	}
}

func (c *requestReplyCore) tryRecv() (*Received, bool, error) {
	c.handlerMu.RLock()
	handler := c.dataHandler
	c.handlerMu.RUnlock()
	if handler != nil {
		return nil, false, stateError("socket is in callback mode")
	}
	select {
	case received, ok := <-c.dataQueue:
		if !ok {
			return nil, false, nil
		}
		return received, true, nil
	default:
		return nil, false, nil
	}
}

func (c *requestReplyCore) onReceive(handler func(*Received)) error {
	if handler == nil {
		return validationError("receive handler must not be nil")
	}
	c.handlerMu.Lock()
	c.dataHandler = handler
	c.handlerMu.Unlock()
	return nil
}

func (c *requestReplyCore) awaitPending(ctx context.Context, correlationID uint64, pending *pendingRequest) (*Received, error) {
	select {
	case result, ok := <-pending.reply:
		c.pending.Delete(correlationID)
		if !ok {
			return nil, &ZlinkError{Kind: ErrorKindNative, Code: int(C.ETERM), Message: "socket is closed"}
		}
		return result.received, result.err
	case <-ctx.Done():
		c.pending.Delete(correlationID)
		return nil, ctx.Err()
	}
}

func (c *requestReplyCore) deliverData(received *Received) {
	c.handlerMu.RLock()
	handler := c.dataHandler
	c.handlerMu.RUnlock()
	if handler != nil {
		go handler(received)
		return
	}
	select {
	case c.dataQueue <- received:
	case <-c.done:
		_ = received.Close()
	}
}

func (c *requestReplyCore) failAll(err error) {
	c.pending.Range(func(key, value any) bool {
		ch := value.(*pendingRequest).reply
		select {
		case ch <- requestResult{err: err}:
		default:
		}
		close(ch)
		c.pending.Delete(key)
		return true
	})
}

func cloneParts(parts []*Message) ([]*Message, error) {
	if len(parts) == 0 {
		return nil, validationError("multipart payload must contain at least one part")
	}
	cloned := make([]*Message, 0, len(parts))
	for i, part := range parts {
		if part == nil {
			closeMessageSlice(cloned)
			return nil, validationError("part %d is nil", i)
		}
		dup, err := part.clone()
		if err != nil {
			closeMessageSlice(cloned)
			return nil, err
		}
		cloned = append(cloned, dup)
	}
	return cloned, nil
}

func requestContext(timeout time.Duration) (context.Context, context.CancelFunc) {
	if timeout <= 0 {
		timeout = defaultRequestTimeout
	}
	return context.WithTimeout(context.Background(), timeout)
}

func isClosed(err error) bool {
	var zerr *ZlinkError
	return errors.As(err, &zerr) && zerr.Kind == ErrorKindState
}

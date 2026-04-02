// SPDX-License-Identifier: MPL-2.0

package zlink

/*
#include "zlink.h"
*/
import "C"

type SendResult int

const (
	SendResultSent SendResult = iota
	SendResultBackpressured
	SendResultNotReady
)

func sendResultFromC(raw C.zlink_send_result_t) (SendResult, error) {
	switch raw {
	case C.ZLINK_SEND_RESULT_SENT:
		return SendResultSent, nil
	case C.ZLINK_SEND_RESULT_BACKPRESSURED:
		return SendResultBackpressured, nil
	case C.ZLINK_SEND_RESULT_NOT_READY:
		return SendResultNotReady, nil
	default:
		return 0, stateError("unknown send result: %d", int(raw))
	}
}

func (r SendResult) Sent() bool {
	return r == SendResultSent
}

type Received struct {
	routingID RoutingID
	parts     []*Message
}

func (r *Received) RoutingID() RoutingID {
	if r == nil {
		return RoutingID{}
	}
	return r.routingID
}

func (r *Received) Parts() []*Message {
	if r == nil {
		return nil
	}
	return r.parts
}

func (r *Received) SinglePartOrError() (*Message, error) {
	if r == nil {
		return nil, stateError("received value is nil")
	}
	if len(r.parts) != 1 {
		return nil, stateError("expected 1 part, got %d", len(r.parts))
	}
	return r.parts[0], nil
}

func (r *Received) Close() error {
	if r == nil {
		return nil
	}
	var first error
	for _, part := range r.parts {
		if err := part.Close(); err != nil && first == nil {
			first = err
		}
	}
	return first
}

type TopicMessage struct {
	routingID RoutingID
	topic     string
	parts     []*Message
}

func (t *TopicMessage) RoutingID() RoutingID {
	if t == nil {
		return RoutingID{}
	}
	return t.routingID
}

func (t *TopicMessage) Topic() string {
	if t == nil {
		return ""
	}
	return t.topic
}

func (t *TopicMessage) Parts() []*Message {
	if t == nil {
		return nil
	}
	return t.parts
}

func (t *TopicMessage) SinglePartOrError() (*Message, error) {
	if t == nil {
		return nil, stateError("topic message is nil")
	}
	if len(t.parts) != 1 {
		return nil, stateError("expected 1 part, got %d", len(t.parts))
	}
	return t.parts[0], nil
}

func (t *TopicMessage) Close() error {
	if t == nil {
		return nil
	}
	var first error
	for _, part := range t.parts {
		if err := part.Close(); err != nil && first == nil {
			first = err
		}
	}
	return first
}

type SubscriptionEvent struct {
	routingID  RoutingID
	subscribed bool
	topic      string
}

func (s *SubscriptionEvent) RoutingID() RoutingID {
	if s == nil {
		return RoutingID{}
	}
	return s.routingID
}

func (s *SubscriptionEvent) Subscribed() bool {
	return s != nil && s.subscribed
}

func (s *SubscriptionEvent) Topic() string {
	if s == nil {
		return ""
	}
	return s.topic
}

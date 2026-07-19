// SPDX-License-Identifier: MPL-2.0

package native

/*
#include <stdint.h>
#include <stdlib.h>
#include "zlink.h"

extern void goZlinkSubscribeTrampoline(zlink_routing_id_t *source_rid_, char *topic_, size_t topic_len_, zlink_msg_t *parts_, size_t part_count_, uintptr_t userdata_);
extern void goZlinkSendReadyTrampoline(void *subject_, uintptr_t userdata_);
extern void goZlinkSpotDispatchEventTrampoline(void *spot_, const zlink_spot_dispatch_info_t *info_, uintptr_t userdata_);
extern void goZlinkReplyTrampoline(zlink_request_result_t result_, zlink_msg_t *parts_, size_t part_count_, uintptr_t userdata_);

static inline int zlink_spot_send_ready_handler_go_local(void *s, uintptr_t userdata) {
    return zlink_send_ready_handler(s, (zlink_send_ready_handler_fn)goZlinkSendReadyTrampoline, (void *)userdata);
}

static inline int zlink_spot_dispatch_event_handler_go_local(void *s, uintptr_t userdata) {
    return zlink_spot_dispatch_event_handler(s, (zlink_spot_dispatch_event_handler_fn)goZlinkSpotDispatchEventTrampoline, (void *)userdata);
}

static inline int zlink_spot_request_to_channel_part_go_local(void *spot, const char *channel_name, zlink_msg_t *part, zlink_send_flags_t flags, zlink_part_flag_t part_flag, uint32_t timeout_ms, uintptr_t userdata) {
    return zlink_spot_request_channel_part(spot, channel_name, part, (zlink_reply_handler_fn)goZlinkReplyTrampoline, (void *)userdata, flags, part_flag, timeout_ms);
}

static inline int zlink_spot_request_spot_part_go_local(void *spot, const zlink_routing_id_t *dest_node_rid, const zlink_routing_id_t *dest_spot_rid, zlink_msg_t *part, zlink_send_flags_t flags, zlink_part_flag_t part_flag, uint32_t timeout_ms, uintptr_t userdata) {
    return zlink_spot_request_spot_part(spot, dest_node_rid, dest_spot_rid, part, (zlink_reply_handler_fn)goZlinkReplyTrampoline, (void *)userdata, flags, part_flag, timeout_ms);
}

static inline int zlink_spot_request_router_part_go_local(void *spot, const zlink_routing_id_t *peer_rid, zlink_msg_t *part, zlink_send_flags_t flags, zlink_part_flag_t part_flag, uint32_t timeout_ms, uintptr_t userdata) {
    return zlink_spot_request_router_part(spot, peer_rid, part, (zlink_reply_handler_fn)goZlinkReplyTrampoline, (void *)userdata, flags, part_flag, timeout_ms);
}
*/
import "C"

import (
	"runtime/cgo"
	"time"
	"unsafe"
)

type Spot struct {
	core *spotCore
}

func (s *Spot) raw() unsafe.Pointer {
	if s == nil || s.core == nil {
		return nil
	}
	return s.core.raw()
}

// isInvalid centralizes the s==nil / core==nil / closed guard. Callers that
// need a typed error use checkValid; recv/handler paths just inspect the
// bool so they can construct their own context-specific error type.
func (s *Spot) isInvalid() bool {
	return s == nil || s.core == nil || s.core.closed
}

// checkValid returns a ConfigError if the spot is unusable, otherwise nil.
func (s *Spot) checkValid() error {
	if s.isInvalid() {
		return &ConfigError{Result: ConfigInvalidHandle, nativeErrno: int(C.EFAULT)}
	}
	return nil
}

func (s *Spot) Close() error {
	if s == nil || s.core == nil {
		return nil
	}
	return s.core.Close()
}

func (s *Spot) SetRoutingID(rid RoutingID) error {
	if err := s.checkValid(); err != nil {
		return err
	}
	raw := rid.toC()
	return configErrorFromResult(C.zlink_set_routing_id(s.raw(), routingIDPointer(&raw), C.size_t(raw.size)))
}

func (s *Spot) RoutingID() (RoutingID, error) {
	if err := s.checkValid(); err != nil {
		return RoutingID{}, err
	}
	return getHandleRoutingID(s.raw())
}

func (s *Spot) SetSendHighWaterMark(value int) error {
	return s.core.setIntOption(C.ZLINK_OPT_SNDHWM, int32(value))
}

func (s *Spot) SetReceiveHighWaterMark(value int) error {
	return s.core.setIntOption(C.ZLINK_OPT_RCVHWM, int32(value))
}

func (s *Spot) SetLinger(value time.Duration) error {
	return s.core.setDurationOption(C.ZLINK_OPT_LINGER, value)
}

func (s *Spot) SetReceiveTimeout(value time.Duration) error {
	return s.core.setDurationOption(C.ZLINK_OPT_RCVTIMEO, value)
}

func (s *Spot) SetSendTimeout(value time.Duration) error {
	return s.core.setDurationOption(C.ZLINK_OPT_SNDTIMEO, value)
}

func (s *Spot) SetRequestTimeout(value time.Duration) error {
	if err := s.checkValid(); err != nil {
		return err
	}
	ms, err := durationToMillis(value)
	if err != nil {
		return err
	}
	raw := C.int(ms)
	return configErrorFromResult(C.zlink_set_spot_option(s.raw(), C.zlink_spot_option_t(C.ZLINK_SPOT_OPT_REQUEST_TIMEOUT_MS), unsafe.Pointer(&raw), C.size_t(C.sizeof_int)))
}

func (s *Spot) RequestTimeout() (time.Duration, error) {
	if err := s.checkValid(); err != nil {
		return 0, err
	}
	var raw C.int
	size := C.size_t(C.sizeof_int)
	if err := configErrorFromResult(C.zlink_get_spot_option(s.raw(), C.zlink_spot_option_t(C.ZLINK_SPOT_OPT_REQUEST_TIMEOUT_MS), unsafe.Pointer(&raw), &size)); err != nil {
		return 0, err
	}
	return time.Duration(raw) * time.Millisecond, nil
}

func (s *Spot) Publish(topic string) SendOp {
	return newSendBuilder(s, func(parts []sendBuilderPart, flags SendFlags) error {
		return s.core.withCString(topic, func(topicC *C.char) error {
			return submitMultipartFromBuilderParts(parts, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
				return submitErrorFromResult(C.zlink_spot_publish_part(s.raw(), topicC, part, C.zlink_send_flags_t(flags), partFlag))
			})
		})
	})
}

func (s *Spot) SendToChannel(channelName string) SendOp {
	return newSendBuilder(s, func(parts []sendBuilderPart, flags SendFlags) error {
		return s.core.withCString(channelName, func(cstr *C.char) error {
			return submitMultipartFromBuilderParts(parts, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
				return submitErrorFromResult(C.zlink_spot_send_channel_part(s.raw(), cstr, part, C.zlink_send_flags_t(flags), partFlag))
			})
		})
	})
}

func (s *Spot) SendToSpot(destNodeRid, destSpotRid RoutingID) SendOp {
	return newSendBuilder(s, func(parts []sendBuilderPart, flags SendFlags) error {
		node := destNodeRid.toC()
		spot := destSpotRid.toC()
		return submitMultipartFromBuilderParts(parts, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
			return submitErrorFromResult(C.zlink_spot_send_spot_part(s.raw(), &node, &spot, part, C.zlink_send_flags_t(flags), partFlag))
		})
	})
}

func (s *Spot) RequestToChannel(channelName string) RequestOp {
	return newRequestBuilder(s, func(parts []requestBuilderPart, flags SendFlags, timeout time.Duration, callback RequestReplyCallback) error {
		if callback == nil {
			return &ConfigError{Result: ConfigInvalidArgument, nativeErrno: int(C.EINVAL)}
		}
		state, err := s.startChannelRequestBuilder(channelName, flags, timeout, parts)
		if err != nil {
			return err
		}
		go func() {
			result := state.wait()
			callback(result.result, result.parts)
		}()
		return nil
	})
}

func (s *Spot) RequestToSpot(destNodeRid, destSpotRid RoutingID) RequestOp {
	return newRequestBuilder(s, func(parts []requestBuilderPart, flags SendFlags, timeout time.Duration, callback RequestReplyCallback) error {
		if callback == nil {
			return &ConfigError{Result: ConfigInvalidArgument, nativeErrno: int(C.EINVAL)}
		}
		if timeout <= 0 {
			timeout = defaultRequestTimeout
		}
		state := newReplyCallbackState()
		handle := cgo.NewHandle(state)
		node := destNodeRid.toC()
		spot := destSpotRid.toC()
		if err := submitMultipartFromRequestParts(parts, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
			return submitErrorFromResult(C.zlink_spot_request_spot_part_go_local(
				s.raw(),
				&node,
				&spot,
				part,
				C.zlink_send_flags_t(flags),
				partFlag,
				C.uint32_t(requestTimeoutMillis(timeout)),
				C.uintptr_t(handle),
			))
		}); err != nil {
			handle.Delete()
			return err
		}
		startSpotRequestProgress(s.raw(), state)
		go func() {
			result := state.wait()
			callback(result.result, result.parts)
		}()
		return nil
	})
}

func (s *Spot) RequestToRouter(peerRid RoutingID) RequestOp {
	return newRequestBuilder(s, func(parts []requestBuilderPart, flags SendFlags, timeout time.Duration, callback RequestReplyCallback) error {
		if callback == nil {
			return &ConfigError{Result: ConfigInvalidArgument, nativeErrno: int(C.EINVAL)}
		}
		if timeout <= 0 {
			timeout = defaultRequestTimeout
		}
		state := newReplyCallbackState()
		handle := cgo.NewHandle(state)
		peer := peerRid.toC()
		if err := submitMultipartFromRequestParts(parts, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
			return submitErrorFromResult(C.zlink_spot_request_router_part_go_local(
				s.raw(),
				&peer,
				part,
				C.zlink_send_flags_t(flags),
				partFlag,
				C.uint32_t(requestTimeoutMillis(timeout)),
				C.uintptr_t(handle),
			))
		}); err != nil {
			handle.Delete()
			return err
		}
		startSpotRequestProgress(s.raw(), state)
		go func() {
			result := state.wait()
			callback(result.result, result.parts)
		}()
		return nil
	})
}

func (s *Spot) ReplyToSpot(destNodeRid, destSpotRid RoutingID, requestSeq uint64) ReplyOp {
	return newReplyBuilder(s, func(parts []*Message, flags SendFlags) error {
		if err := validateReplyFlags(flags); err != nil {
			return err
		}
		node := destNodeRid.toC()
		spot := destSpotRid.toC()
		return submitMultipartFromClones(parts, false, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
			return submitErrorFromResult(C.zlink_spot_reply_spot_part(s.raw(), &node, &spot, C.uint64_t(requestSeq), part, partFlag))
		})
	})
}

func (s *Spot) ReplyToRouter(peerRid RoutingID, requestSeq uint64) ReplyOp {
	return newReplyBuilder(s, func(parts []*Message, flags SendFlags) error {
		if err := validateReplyFlags(flags); err != nil {
			return err
		}
		peer := peerRid.toC()
		return submitMultipartFromClones(parts, false, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
			return submitErrorFromResult(C.zlink_spot_reply_router_part(s.raw(), &peer, C.uint64_t(requestSeq), part, partFlag))
		})
	})
}

func (s *Spot) SetSubscription(filter string) error {
	return s.core.withCString(filter, func(cstr *C.char) error {
		return configErrorFromResult(C.zlink_set_subscription(s.raw(), cstr))
	})
}

func (s *Spot) UnsetSubscription(filter string) error {
	return s.core.withCString(filter, func(cstr *C.char) error {
		return configErrorFromResult(C.zlink_unset_subscription(s.raw(), cstr))
	})
}

func (s *Spot) Subscribe(out *TopicMessage, flags RecvFlags) (bool, error) {
	if out == nil {
		return false, &RecvError{Result: RecvInvalidHandle, nativeErrno: int(C.EINVAL)}
	}
	err := recvSpotTopicMessageInto(out, func(rid **C.zlink_routing_id_t, topic *C.char, topicLen *C.size_t, part *C.zlink_msg_t, hasMore *C.zlink_part_flag_t, recvFlags C.zlink_recv_flags_t) error {
		return recvErrorFromResult(C.zlink_spot_subscribe_part(s.raw(), rid, topic, C.size_t(recvTopicBufferCap), topicLen, part, hasMore, recvFlags))
	}, flags)
	if err != nil {
		if isNoData(err) {
			return false, nil
		}
		return false, err
	}
	return true, nil
}

func (s *Spot) SubscribePart(out *Message, topicBuffer []byte, flags RecvFlags) (SubscribePartResult, bool, error) {
	if s == nil || s.core == nil {
		return SubscribePartResult{}, false, &RecvError{Result: RecvInvalidHandle, nativeErrno: int(C.EFAULT)}
	}
	result, err := recvSubscribePartInto(out, topicBuffer, flags, func(rid **C.zlink_routing_id_t, topic *C.char, topicCap C.size_t, topicLen *C.size_t, part *C.zlink_msg_t, hasMore *C.zlink_part_flag_t, recvFlags C.zlink_recv_flags_t) error {
		return recvErrorFromResult(C.zlink_spot_subscribe_part(s.raw(), rid, topic, topicCap, topicLen, part, hasMore, recvFlags))
	})
	if err != nil {
		if isNoData(err) {
			return SubscribePartResult{}, false, nil
		}
		return SubscribePartResult{}, false, err
	}
	return result, true, nil
}

func (s *Spot) ReceiveSubscriptionEvent(out *SubscriptionEvent, flags RecvFlags) (bool, error) {
	if out == nil {
		return false, &RecvError{Result: RecvInvalidHandle, nativeErrno: int(C.EINVAL)}
	}
	if s.isInvalid() {
		return false, &RecvError{Result: RecvInvalidHandle, nativeErrno: int(C.EFAULT)}
	}
	_ = flags
	return false, &RecvError{Result: RecvNotSupported, nativeErrno: int(C.ENOTSUP)}
}

func (s *Spot) OnSendReady(handler func()) error {
	if handler == nil {
		return &HandlerError{Result: HandlerInvalidArgument, nativeErrno: int(C.EINVAL)}
	}
	if s.isInvalid() {
		return &HandlerError{Result: HandlerInvalidArgument, nativeErrno: int(C.EFAULT)}
	}
	state := newSendReadyCallbackState(sendReadyCallback(handler))
	handle := cgo.NewHandle(state)
	if err := handlerErrorFromResult(C.zlink_spot_send_ready_handler_go_local(s.raw(), C.uintptr_t(handle))); err != nil {
		state.close()
		handle.Delete()
		return err
	}
	if s.core.sendReadyHandle != 0 {
		releaseCallbackHandle(s.core.sendReadyHandle)
	}
	s.core.sendReadyHandle = handle
	return nil
}

func (s *Spot) RecvRoutedPart(out *Message, flags RecvFlags) (RecvPartResult, bool, error) {
	if s.isInvalid() {
		return RecvPartResult{}, false, &RecvError{Result: RecvInvalidHandle, nativeErrno: int(C.EFAULT)}
	}
	result, err := recvRoutedPartInto(out, flags, func(nodeRID **C.zlink_routing_id_t, spotRID **C.zlink_routing_id_t, requestSeq *C.uint64_t, part *C.zlink_msg_t, hasMore *C.zlink_part_flag_t, recvFlags C.zlink_recv_flags_t) error {
		return recvErrorFromResult(C.zlink_spot_recv_part(s.raw(), nodeRID, spotRID, requestSeq, part, hasMore, recvFlags))
	})
	if err != nil {
		if isNoData(err) {
			return RecvPartResult{}, false, nil
		}
		return RecvPartResult{}, false, err
	}
	return result, true, nil
}

func (s *Spot) RecvRouted(out *Received, flags RecvFlags) (bool, error) {
	if out == nil {
		return false, &RecvError{Result: RecvInvalidHandle, nativeErrno: int(C.EINVAL)}
	}
	var sourceRID *C.zlink_routing_id_t
	var spotRID *C.zlink_routing_id_t
	var requestSeq C.uint64_t
	clonedParts, err := recvMultipart(flags, func(part *C.zlink_msg_t, hasMore *C.zlink_part_flag_t, recvFlags C.zlink_recv_flags_t) error {
		return recvErrorFromResult(C.zlink_spot_recv_part(s.raw(), &sourceRID, &spotRID, &requestSeq, part, hasMore, recvFlags))
	})
	if err != nil {
		if isNoData(err) {
			return false, nil
		}
		return false, err
	}
	routingID := routingIDFromCPtr(sourceRID)
	spotRoutingID := routingIDFromCPtr(spotRID)
	seq := uint64(requestSeq)
	out.replace(
		routingID,
		spotRoutingID,
		clonedParts,
		seq,
		requestSeq != 0,
		receivedReplyToSpot(s, routingID, spotRoutingID, seq),
		receivedSendFromSpot(s, routingID, spotRoutingID),
		receivedSendFromSpotBuilder(s, routingID, spotRoutingID),
	)
	return true, nil
}

func (s *Spot) OnDispatchEvent(handler func(*Spot, SpotDispatchInfo)) error {
	if handler == nil {
		return &HandlerError{Result: HandlerInvalidArgument, nativeErrno: int(C.EINVAL)}
	}
	state := newSpotDispatchCallbackState(s, handler)
	handle := cgo.NewHandle(state)
	if err := handlerErrorFromResult(C.zlink_spot_dispatch_event_handler_go_local(s.raw(), C.uintptr_t(handle))); err != nil {
		state.close()
		handle.Delete()
		return err
	}
	if s.core.dispatchHandle != 0 {
		releaseCallbackHandle(s.core.dispatchHandle)
	}
	s.core.dispatchHandle = handle
	return nil
}

func (s *Spot) RecvActorLifecycle(flags RecvFlags) (SpotActorLifecycleEvent, bool, error) {
	if s == nil || s.core == nil || s.core.closed {
		return SpotActorLifecycleEvent{}, false, &RecvError{Result: RecvInvalidHandle, nativeErrno: int(C.EFAULT)}
	}
	var event C.zlink_spot_actor_lifecycle_event_t
	if err := recvErrorFromResult(C.zlink_spot_recv_actor_lifecycle(s.raw(), &event, C.zlink_recv_flags_t(flags))); err != nil {
		if isNoData(err) {
			return SpotActorLifecycleEvent{}, false, nil
		}
		return SpotActorLifecycleEvent{}, false, err
	}
	return SpotActorLifecycleEvent{
		Kind: SpotActorLifecycleEventKind(event.kind),
		Info: spotActorLifecycleInfoFromC(&event.info),
	}, true, nil
}

// SPDX-License-Identifier: MPL-2.0

package native

/*
#include <stdint.h>
#include <stdlib.h>
#include "zlink.h"

extern void goZlinkRecvTrampoline(zlink_routing_id_t *source_rid_, zlink_msg_t *parts_, size_t part_count_, uintptr_t userdata_);
extern void goZlinkSendReadyTrampoline(void *subject_, uintptr_t userdata_);
extern void goZlinkReplyTrampoline(zlink_request_result_t result_, zlink_msg_t *parts_, size_t part_count_, uintptr_t userdata_);

static inline int zlink_recv_handler_go_local(void *s, uintptr_t userdata) {
    return zlink_recv_handler(s, (zlink_socket_msg_handler_fn)goZlinkRecvTrampoline, (void *)userdata);
}

static inline int zlink_send_ready_handler_go_local(void *s, uintptr_t userdata) {
    return zlink_send_ready_handler(s, (zlink_send_ready_handler_fn)goZlinkSendReadyTrampoline, (void *)userdata);
}

static inline int zlink_router_request_spot_part_go_local(void *router, const zlink_routing_id_t *dest_node_rid, const zlink_routing_id_t *dest_spot_rid, zlink_msg_t *part, zlink_send_flags_t flags, zlink_part_flag_t part_flag, uint32_t timeout_ms, uintptr_t userdata) {
    return zlink_router_request_spot_part(router, dest_node_rid, dest_spot_rid, part, (zlink_reply_handler_fn)goZlinkReplyTrampoline, (void *)userdata, flags, part_flag, timeout_ms);
}

static inline int zlink_router_send_spot_part_go_local(void *router, const zlink_routing_id_t *dest_node_rid, const zlink_routing_id_t *dest_spot_rid, zlink_msg_t *part, zlink_send_flags_t flags, zlink_part_flag_t part_flag) {
    return zlink_router_send_spot_part(router, dest_node_rid, dest_spot_rid, part, flags, part_flag);
}
*/
import "C"

type publishSocket struct {
	*connectionSocket
}

func (s *publishSocket) OnSendReady(handler func()) error {
	return s.setSendReady(handler)
}

func (s *publishSocket) SetNoDrop(value bool) error {
	return s.setPubBoolOption(C.ZLINK_PUB_OPT_NODROP, value)
}

func (s *publishSocket) NoDrop() (bool, error) {
	return s.getPubBoolOption(C.ZLINK_PUB_OPT_NODROP)
}

func (s *publishSocket) SetVerbose(value bool) error {
	return s.setPubBoolOption(C.ZLINK_PUB_OPT_VERBOSE, value)
}

func (s *publishSocket) Verbose() (bool, error) {
	return s.getPubBoolOption(C.ZLINK_PUB_OPT_VERBOSE)
}

func (s *publishSocket) SetVerboser(value bool) error {
	return s.setPubBoolOption(C.ZLINK_PUB_OPT_VERBOSER, value)
}

func (s *publishSocket) Verboser() (bool, error) {
	return s.getPubBoolOption(C.ZLINK_PUB_OPT_VERBOSER)
}

func (s *publishSocket) SetManual(value bool) error {
	return s.setPubBoolOption(C.ZLINK_PUB_OPT_MANUAL, value)
}

func (s *publishSocket) Manual() (bool, error) {
	return s.getPubBoolOption(C.ZLINK_PUB_OPT_MANUAL)
}

func (s *publishSocket) SetManualLastValue(value bool) error {
	return s.setPubBoolOption(C.ZLINK_PUB_OPT_MANUAL_LAST_VALUE, value)
}

func (s *publishSocket) ManualLastValue() (bool, error) {
	return s.getPubBoolOption(C.ZLINK_PUB_OPT_MANUAL_LAST_VALUE)
}

func (s *publishSocket) SetWelcomeMessage(message *Message) error {
	if message == nil {
		return s.setPubBytesOption(C.ZLINK_PUB_OPT_WELCOME_MSG, nil)
	}
	return s.setPubBytesOption(C.ZLINK_PUB_OPT_WELCOME_MSG, message.Data())
}

func (s *publishSocket) WelcomeMessage() (*Message, error) {
	data, err := s.getPubBytesOption(C.ZLINK_PUB_OPT_WELCOME_MSG, 256)
	if err != nil {
		return nil, err
	}
	return NewMessage(data)
}

func (s *publishSocket) ApproveSubscribe(routingID RoutingID) error {
	return s.setPubRoutingIDOption(C.ZLINK_PUB_OPT_APPROVE_SUBSCRIBE, routingID)
}

func (s *publishSocket) RejectSubscribe(routingID RoutingID) error {
	return s.setPubRoutingIDOption(C.ZLINK_PUB_OPT_REJECT_SUBSCRIBE, routingID)
}

func (s *publishSocket) PubOptions() *PubSocketOptions {
	return &PubSocketOptions{socket: s.connectionSocket}
}

func (s *publishSocket) submitPublish(topic string, flags SendFlags, parts ...*Message) (bool, error) {
	err := s.withCString(topic, func(cstr *C.char) error {
		return submitMultipartFromClones(parts, true, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
			return submitErrorFromResult(C.zlink_publish_part(s.raw(), cstr, part, C.zlink_send_flags_t(flags), partFlag))
		})
	})
	return submitBackpressureResult(err)
}

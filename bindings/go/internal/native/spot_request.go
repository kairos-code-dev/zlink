// SPDX-License-Identifier: MPL-2.0

package native

/*
#include <stdint.h>
#include <stdlib.h>
#include "zlink.h"

extern void goZlinkReplyTrampoline(zlink_request_result_t result_, zlink_msg_t *parts_, size_t part_count_, uintptr_t userdata_);

static inline int zlink_spot_request_to_channel_part_go_local(void *spot, const char *channel_name, zlink_msg_t *part, zlink_send_flags_t flags, zlink_part_flag_t part_flag, uint32_t timeout_ms, uintptr_t userdata) {
    return zlink_spot_request_channel_part(spot, channel_name, part, (zlink_reply_handler_fn)goZlinkReplyTrampoline, (void *)userdata, flags, part_flag, timeout_ms);
}
*/
import "C"

import (
	"runtime/cgo"
	"time"
)

func (s *Spot) startChannelRequest(channelName string, flags SendFlags, timeout time.Duration, parts ...*Message) (*replyCallbackState, error) {
	builderParts := make([]requestBuilderPart, len(parts))
	for i, part := range parts {
		builderParts[i] = requestBuilderPart{message: part}
	}
	return s.startChannelRequestBuilder(channelName, flags, timeout, builderParts)
}

func (s *Spot) startChannelRequestBuilder(channelName string, flags SendFlags, timeout time.Duration, parts []requestBuilderPart) (*replyCallbackState, error) {
	if timeout <= 0 {
		timeout = defaultRequestTimeout
	}
	state := newReplyCallbackState()
	handle := cgo.NewHandle(state)
	if err := s.core.withCString(channelName, func(cstr *C.char) error {
		return submitMultipartFromRequestParts(parts, func(part *C.zlink_msg_t, partFlag C.zlink_part_flag_t) error {
			return submitErrorFromResult(C.zlink_spot_request_to_channel_part_go_local(
				s.raw(),
				cstr,
				part,
				C.zlink_send_flags_t(flags),
				partFlag,
				C.uint32_t(requestTimeoutMillis(timeout)),
				C.uintptr_t(handle),
			))
		})
	}); err != nil {
		handle.Delete()
		return nil, err
	}
	startSpotRequestProgress(s.raw(), state)
	return state, nil
}

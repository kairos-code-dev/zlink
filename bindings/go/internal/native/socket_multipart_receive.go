// SPDX-License-Identifier: MPL-2.0

package native

/*
#include <stdint.h>
#include <stdlib.h>
#include "zlink.h"
*/
import "C"

import "unsafe"

func recvMultipart(flags RecvFlags, recv multipartRecvFunc) ([]*Message, error) {
	parts := make([]*Message, 0, 1)
	recvFlags := C.zlink_recv_flags_t(flags)
	for {
		var part C.zlink_msg_t
		if err := configErrorFromResult(C.zlink_msg_init(&part)); err != nil {
			closeMessageSlice(parts)
			return nil, err
		}

		var hasMore C.zlink_part_flag_t
		if err := recv(&part, &hasMore, recvFlags); err != nil {
			_ = configErrorFromResult(C.zlink_msg_close(&part))
			closeMessageSlice(parts)
			return nil, err
		}

		msg := &Message{}
		if err := configErrorFromResult(C.zlink_msg_adopt(&msg.msg, &part)); err != nil {
			_ = configErrorFromResult(C.zlink_msg_close(&part))
			closeMessageSlice(parts)
			return nil, err
		}
		parts = append(parts, msg)

		if hasMore == 0 {
			break
		}
		recvFlags = C.zlink_recv_flags_t(C.ZLINK_DONTWAIT)
	}

	return parts, nil
}

func takeParts(ptr *C.zlink_msg_t, partCount C.size_t) ([]*Message, error) {
	count := int(partCount)
	if count == 0 || ptr == nil {
		return nil, nil
	}
	raw := unsafe.Slice(ptr, count)
	parts := make([]*Message, 0, count)
	for i := 0; i < count; i++ {
		msg := &Message{}
		if err := configErrorFromResult(C.zlink_msg_init(&msg.msg)); err != nil {
			closeMessageSlice(parts)
			C.zlink_multipart_close(ptr, partCount)
			return nil, err
		}
		if err := configErrorFromResult(C.zlink_msg_move(&msg.msg, &raw[i])); err != nil {
			_ = msg.Close()
			closeMessageSlice(parts)
			C.zlink_multipart_close(ptr, partCount)
			return nil, err
		}
		parts = append(parts, msg)
	}
	C.zlink_multipart_close(ptr, partCount)
	return parts, nil
}

func bytePartsToMessages(parts [][]byte) ([]*Message, error) {
	if len(parts) == 0 {
		return nil, &ConfigError{Result: ConfigInvalidArgument, nativeErrno: int(C.EINVAL)}
	}
	messages := make([]*Message, 0, len(parts))
	for _, part := range parts {
		msg, err := NewMessage(part)
		if err != nil {
			closeMessageSlice(messages)
			return nil, err
		}
		messages = append(messages, msg)
	}
	return messages, nil
}

func mustTakeParts(ptr *C.zlink_msg_t, partCount C.size_t) []*Message {
	parts, err := takeParts(ptr, partCount)
	if err != nil {
		panic(err)
	}
	return parts
}

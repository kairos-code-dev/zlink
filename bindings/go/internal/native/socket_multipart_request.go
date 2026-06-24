// SPDX-License-Identifier: MPL-2.0

package native

/*
#include <stdint.h>
#include <stdlib.h>
#include "zlink.h"
*/
import "C"

func requestBuilderMessages(parts []requestBuilderPart) []*Message {
	messages := make([]*Message, len(parts))
	for i, part := range parts {
		messages[i] = part.message
	}
	return messages
}

func requestBuilderMessagesForClone(parts []requestBuilderPart) ([]*Message, func(), error) {
	messages := make([]*Message, len(parts))
	temporary := make([]*Message, 0)
	cleanup := func() {
		for _, message := range temporary {
			_ = message.Close()
		}
	}
	for i, part := range parts {
		if part.bytes {
			message, err := NewMessage(part.data)
			if err != nil {
				cleanup()
				return nil, nil, err
			}
			temporary = append(temporary, message)
			messages[i] = message
			continue
		}
		messages[i] = part.message
	}
	return messages, cleanup, nil
}

func requestBuilderPartsUseOnlyMessages(parts []requestBuilderPart) bool {
	for _, part := range parts {
		if part.bytes {
			return false
		}
	}
	return true
}

func submitMultipartFromRequestParts(parts []requestBuilderPart, submit multipartSubmitFunc) error {
	if len(parts) == 0 {
		return &ConfigError{Result: ConfigInvalidArgument, nativeErrno: int(C.EINVAL)}
	}
	if len(parts) == 1 {
		if parts[0].bytes {
			return submitSinglePartFromBytes(parts[0].data, submit)
		}
		return submitMultipartFromClones([]*Message{parts[0].message}, false, submit)
	}
	if requestBuilderPartsUseOnlyMessages(parts) {
		return submitMultipartFromClones(requestBuilderMessages(parts), false, submit)
	}

	native := make([]C.zlink_msg_t, len(parts))
	for i, part := range parts {
		if part.bytes {
			if err := initNativeMessageFromBytes(&native[i], part.data); err != nil {
				closeNativeMultipart(native, i)
				return err
			}
			continue
		}
		if part.message == nil {
			closeNativeMultipart(native, i)
			return &ConfigError{Result: ConfigInvalidArgument, nativeErrno: int(C.EINVAL)}
		}
		if part.message.closed {
			closeNativeMultipart(native, i)
			return &ConfigError{Result: ConfigInvalidHandle, nativeErrno: int(C.EFAULT)}
		}
		if err := configErrorFromResult(C.zlink_msg_init(&native[i])); err != nil {
			closeNativeMultipart(native, i)
			return err
		}
		if err := configErrorFromResult(C.zlink_msg_copy(&native[i], &part.message.msg)); err != nil {
			closeNativeMultipart(native, i+1)
			return err
		}
	}

	prepared := &preparedMultipart{native: native}
	return submitPreparedMultipart(prepared, submit)
}

// SPDX-License-Identifier: MPL-2.0

package native

/*
#include <stdint.h>
#include <stdlib.h>
#include "zlink.h"
*/
import "C"

func submitAggregateFromSendParts(parts []sendBuilderPart, submit func(*C.zlink_msg_t, C.size_t) error) error {
	if len(parts) == 0 {
		return &ConfigError{Result: ConfigInvalidArgument, nativeErrno: int(C.EINVAL)}
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
		if part.move {
			if err := configErrorFromResult(C.zlink_msg_move(&native[i], &part.message.msg)); err != nil {
				closeNativeMultipart(native, i+1)
				return err
			}
		} else {
			if err := configErrorFromResult(C.zlink_msg_copy(&native[i], &part.message.msg)); err != nil {
				closeNativeMultipart(native, i+1)
				return err
			}
		}
	}
	if err := submit(&native[0], C.size_t(len(native))); err != nil {
		closeNativeMultipart(native, len(native))
		return err
	}
	for _, part := range parts {
		if part.message != nil {
			if part.move {
				part.message.moved()
			} else if !part.bytes {
				_ = part.message.Close()
			}
		}
	}
	return nil
}

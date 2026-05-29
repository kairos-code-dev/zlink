// SPDX-License-Identifier: MPL-2.0

package native

/*
#include <stdint.h>
#include <stdlib.h>
#include "zlink.h"
*/
import "C"

import "unsafe"

func closeNativeMultipart(parts []C.zlink_msg_t, count int) {
	if count <= 0 || len(parts) == 0 {
		return
	}
	C.zlink_multipart_close(&parts[0], C.size_t(count))
}

func closeMessageSlice(parts []*Message) {
	for _, part := range parts {
		_ = part.Close()
	}
}

type preparedMultipart struct {
	native []C.zlink_msg_t
	parts  []*Message
}

type multipartSubmitFunc func(*C.zlink_msg_t, C.zlink_part_flag_t) error
type multipartRecvFunc func(*C.zlink_msg_t, *C.zlink_part_flag_t, C.zlink_recv_flags_t) error

func prepareMultipart(parts []*Message) (*preparedMultipart, error) {
	if len(parts) == 0 {
		return nil, &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	native := make([]C.zlink_msg_t, len(parts))
	for i, part := range parts {
		if part == nil {
			return nil, &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
		}
		if part.closed {
			return nil, &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
		}
		if err := configErrorFromResult(C.zlink_msg_init(&native[i])); err != nil {
			closeNativeMultipart(native, i)
			return nil, err
		}
		if err := configErrorFromResult(C.zlink_msg_move(&native[i], &part.msg)); err != nil {
			prepared := &preparedMultipart{native: native[:i+1], parts: parts[:i+1]}
			restoreErr := prepared.restore()
			if restoreErr != nil {
				return nil, restoreErr
			}
			return nil, err
		}
	}
	return &preparedMultipart{native: native, parts: parts}, nil
}

func (p *preparedMultipart) ptr() *C.zlink_msg_t {
	if p == nil || len(p.native) == 0 {
		return nil
	}
	return &p.native[0]
}

func (p *preparedMultipart) count() C.size_t {
	if p == nil {
		return 0
	}
	return C.size_t(len(p.native))
}

func (p *preparedMultipart) commit() {
	if p == nil {
		return
	}
	for _, part := range p.parts {
		part.moved()
	}
}

func (p *preparedMultipart) restore() error {
	if p == nil {
		return nil
	}
	for i, part := range p.parts {
		if err := configErrorFromResult(C.zlink_msg_move(&part.msg, &p.native[i])); err != nil {
			closeNativeMultipart(p.native, len(p.native))
			return err
		}
	}
	closeNativeMultipart(p.native, len(p.native))
	return nil
}

func markPartsMoved(parts []*Message) {
	for _, part := range parts {
		if part != nil {
			part.moved()
		}
	}
}

func submitPreparedMultipart(prepared *preparedMultipart, submit multipartSubmitFunc) error {
	if prepared == nil || len(prepared.native) == 0 {
		return &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	for i := range prepared.native {
		partFlag := C.zlink_part_flag_t(C.ZLINK_PART_FINAL)
		if i+1 < len(prepared.native) {
			partFlag = C.ZLINK_PART_MORE
		}
		if err := submit(&prepared.native[i], partFlag); err != nil {
			if i+1 < len(prepared.native) {
				closeNativeMultipart(prepared.native[i+1:], len(prepared.native)-(i+1))
			}
			return err
		}
	}
	return nil
}

func initNativeMessageFromBytes(native *C.zlink_msg_t, data []byte) error {
	if native == nil {
		return &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	if err := configErrorFromResult(C.zlink_msg_init_size(native, C.size_t(len(data)))); err != nil {
		return err
	}
	if len(data) > 0 {
		copy(unsafe.Slice((*byte)(C.zlink_msg_data(native)), len(data)), data)
	}
	return nil
}

func submitMultipartFromClones(parts []*Message, consumeOriginal bool, submit multipartSubmitFunc) error {
	if consumeOriginal && len(parts) == 1 {
		return submitSinglePartFromCopy(parts[0], submit)
	}
	cloned, err := cloneParts(parts)
	if err != nil {
		return err
	}
	prepared, err := prepareMultipart(cloned)
	if err != nil {
		closeMessageSlice(cloned)
		return err
	}
	err = submitPreparedMultipart(prepared, submit)
	prepared.commit()
	if err != nil {
		return err
	}
	if consumeOriginal {
		markPartsMoved(parts)
	}
	return nil
}

func submitSinglePartFromCopy(part *Message, submit multipartSubmitFunc) error {
	if part == nil {
		return &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	if part.closed {
		return &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	var native C.zlink_msg_t
	if err := configErrorFromResult(C.zlink_msg_init(&native)); err != nil {
		return err
	}
	if err := configErrorFromResult(C.zlink_msg_copy(&native, &part.msg)); err != nil {
		_ = configErrorFromResult(C.zlink_msg_close(&native))
		return err
	}
	err := submit(&native, C.zlink_part_flag_t(C.ZLINK_PART_FINAL))
	if err != nil {
		_ = configErrorFromResult(C.zlink_msg_close(&native))
		return err
	}
	_ = configErrorFromResult(C.zlink_msg_close(&part.msg))
	part.moved()
	return nil
}

func submitSinglePartMoved(part *Message, submit multipartSubmitFunc) error {
	if part == nil {
		return &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	if part.closed {
		return &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	var native C.zlink_msg_t
	if err := configErrorFromResult(C.zlink_msg_init(&native)); err != nil {
		return err
	}
	if err := configErrorFromResult(C.zlink_msg_move(&native, &part.msg)); err != nil {
		_ = configErrorFromResult(C.zlink_msg_close(&native))
		return err
	}
	err := submit(&native, C.zlink_part_flag_t(C.ZLINK_PART_FINAL))
	if err != nil {
		_ = configErrorFromResult(C.zlink_msg_close(&native))
	}
	part.moved()
	return err
}

func submitSinglePartFromBytes(data []byte, submit multipartSubmitFunc) error {
	var native C.zlink_msg_t
	if err := initNativeMessageFromBytes(&native, data); err != nil {
		return err
	}
	err := submit(&native, C.zlink_part_flag_t(C.ZLINK_PART_FINAL))
	if err != nil {
		_ = configErrorFromResult(C.zlink_msg_close(&native))
	}
	return err
}

func builderMessages(parts []sendBuilderPart) []*Message {
	messages := make([]*Message, len(parts))
	for i, part := range parts {
		messages[i] = part.message
	}
	return messages
}

func closeMovedSendBuilderParts(parts []sendBuilderPart) {
	for _, part := range parts {
		if part.move && part.message != nil {
			_ = part.message.Close()
		}
	}
}

func sendBuilderPartsNeedBuilder(parts []sendBuilderPart) bool {
	for _, part := range parts {
		if part.move || part.bytes {
			return true
		}
	}
	return false
}

func sendBuilderPartsUseOnlyRetainedMessages(parts []sendBuilderPart) bool {
	for _, part := range parts {
		if part.move || part.bytes {
			return false
		}
	}
	return true
}

func submitMultipartFromBuilderParts(parts []sendBuilderPart, submit multipartSubmitFunc) error {
	if len(parts) == 0 {
		return &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	if len(parts) == 1 {
		if parts[0].bytes {
			return submitSinglePartFromBytes(parts[0].data, submit)
		}
		if parts[0].move {
			return submitSinglePartMoved(parts[0].message, submit)
		}
		return submitSinglePartFromCopy(parts[0].message, submit)
	}
	if sendBuilderPartsUseOnlyRetainedMessages(parts) {
		return submitMultipartFromClones(builderMessages(parts), true, submit)
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
			return &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
		}
		if part.message.closed {
			closeNativeMultipart(native, i)
			return &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
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

	prepared := &preparedMultipart{native: native, parts: builderMessages(parts)}
	err := submitPreparedMultipart(prepared, submit)
	for _, part := range parts {
		if part.move {
			part.message.moved()
			continue
		}
		if err == nil {
			_ = configErrorFromResult(C.zlink_msg_close(&part.message.msg))
			part.message.moved()
		}
	}
	return err
}

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
		return &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
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
			return &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
		}
		if part.message.closed {
			closeNativeMultipart(native, i)
			return &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
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
		return nil, &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
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

func routingIDPointer(raw *C.zlink_routing_id_t) unsafe.Pointer {
	if raw == nil || raw.size == 0 {
		return nil
	}
	return unsafe.Pointer(&raw.data[0])
}

func getHandleRoutingID(handle unsafe.Pointer) (RoutingID, error) {
	var raw C.zlink_routing_id_t
	if err := configErrorFromResult(C.zlink_get_routing_id(handle, &raw)); err != nil {
		return RoutingID{}, err
	}
	return routingIDFromC(raw), nil
}

func withCStringPair(left string, right string, fn func(*C.char, *C.char) error) error {
	for _, value := range []string{left, right} {
		if err := validateEndpointString(value); err != nil {
			return err
		}
	}
	leftC := C.CString(left)
	defer C.free(unsafe.Pointer(leftC))
	rightC := C.CString(right)
	defer C.free(unsafe.Pointer(rightC))
	return fn(leftC, rightC)
}

func subscriptionAt(handle unsafe.Pointer, index int) (string, bool, error) {
	if index < 0 {
		return "", false, &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	var size C.size_t
	var isPattern C.int
	err := configErrorFromResult(C.zlink_subscription_at(handle, C.size_t(index), nil, &size, &isPattern))
	if err == nil {
		return "", isPattern != 0, nil
	}
	zerr, ok := err.(*ConfigError)
	if !ok || zerr.Result != ConfigInvalidArgument || size == 0 {
		return "", false, err
	}
	buf := make([]byte, int(size))
	if err := configErrorFromResult(C.zlink_subscription_at(handle, C.size_t(index), (*C.char)(unsafe.Pointer(&buf[0])), &size, &isPattern)); err != nil {
		return "", false, err
	}
	return string(buf[:int(size)]), isPattern != 0, nil
}

func setTLSServer(handle unsafe.Pointer, certPath string, keyPath string, requireClientCert bool) error {
	return withCStringPair(certPath, keyPath, func(certC *C.char, keyC *C.char) error {
		var required C.int
		if requireClientCert {
			required = 1
		}
		return configErrorFromResult(C.zlink_set_tls_server(handle, certC, keyC, required))
	})
}

func setTLSClient(handle unsafe.Pointer, caCertPath string, hostname string, trustSystem bool) error {
	return withCStringPair(caCertPath, hostname, func(caCertC *C.char, hostnameC *C.char) error {
		var trust C.int
		if trustSystem {
			trust = 1
		}
		return configErrorFromResult(C.zlink_set_tls_client(handle, caCertC, hostnameC, trust))
	})
}

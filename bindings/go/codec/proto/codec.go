package proto

import (
	"errors"
	"reflect"

	pbproto "google.golang.org/protobuf/proto"

	"zlink"
)

var (
	errNilMessage        = errors.New("zlink/codec/proto: nil message")
	errNilValue          = errors.New("zlink/codec/proto: nil value")
	errInvalidDecodeType = errors.New("zlink/codec/proto: Decode type must be a concrete protobuf message pointer")
)

// Decode unmarshals a zlink message into a concrete protobuf message pointer.
func Decode[T pbproto.Message](msg *zlink.Message) (T, error) {
	var zero T
	if msg == nil {
		return zero, errNilMessage
	}

	out, err := newDecodeTarget[T]()
	if err != nil {
		return zero, err
	}
	if err := pbproto.Unmarshal(msg.Bytes(), out); err != nil {
		return zero, err
	}
	return out, nil
}

// Encode marshals a protobuf message into a zlink message.
func Encode[T pbproto.Message](v T) (*zlink.Message, error) {
	if isNilProtoValue(v) {
		return nil, errNilValue
	}
	payload, err := pbproto.Marshal(v)
	if err != nil {
		return nil, err
	}
	return zlink.NewMessageFromBytes(payload)
}

func newDecodeTarget[T pbproto.Message]() (T, error) {
	var zero T

	targetType := reflect.TypeFor[T]()
	if targetType.Kind() != reflect.Pointer || targetType.Elem().Kind() == reflect.Interface {
		return zero, errInvalidDecodeType
	}

	value, ok := reflect.New(targetType.Elem()).Interface().(T)
	if !ok {
		return zero, errInvalidDecodeType
	}
	return value, nil
}

func isNilProtoValue[T pbproto.Message](v T) bool {
	value := reflect.ValueOf(v)
	if !value.IsValid() {
		return true
	}
	switch value.Kind() {
	case reflect.Pointer, reflect.Interface, reflect.Map, reflect.Slice, reflect.Func:
		return value.IsNil()
	default:
		return false
	}
}

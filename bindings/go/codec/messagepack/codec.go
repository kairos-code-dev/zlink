package messagepack

import (
	"errors"

	"github.com/vmihailenco/msgpack/v5"

	zlink "zlink.systems/zlink/contracts"
)

var errNilMessage = errors.New("zlink.systems/zlink/codec/messagepack: nil message")

// Decode unmarshals a MessagePack payload from a zlink message.
func Decode[T any](msg *zlink.Message) (T, error) {
	var out T
	if msg == nil {
		return out, errNilMessage
	}
	if err := msgpack.Unmarshal(msg.Bytes(), &out); err != nil {
		return out, err
	}
	return out, nil
}

// Encode marshals a Go value into a zlink message.
func Encode[T any](v T) (*zlink.Message, error) {
	payload, err := msgpack.Marshal(v)
	if err != nil {
		return nil, err
	}
	return zlink.NewMessageFrom(payload)
}

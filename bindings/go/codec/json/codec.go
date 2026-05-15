package json

import (
	stdjson "encoding/json"
	"errors"

	zlink "zlink.systems/zlink/contracts"
)

var errNilMessage = errors.New("zlink.systems/zlink/codec/json: nil message")

// Decode unmarshals a zlink message into a Go value.
func Decode[T any](msg *zlink.Message) (T, error) {
	var out T
	if msg == nil {
		return out, errNilMessage
	}
	if err := stdjson.Unmarshal(msg.Bytes(), &out); err != nil {
		return out, err
	}
	return out, nil
}

// Encode marshals a Go value into a zlink message.
func Encode[T any](v T) (*zlink.Message, error) {
	payload, err := stdjson.Marshal(v)
	if err != nil {
		return nil, err
	}
	return zlink.NewMessageFromBytes(payload)
}

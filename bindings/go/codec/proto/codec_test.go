package proto_test

import (
	"google.golang.org/protobuf/types/known/wrapperspb"
	"testing"
	zlinkproto "zlink.systems/zlink/codec/proto"
)

func TestProtoRoundtrip(t *testing.T) {
	original := wrapperspb.String("hello-zlink")
	msg, err := zlinkproto.Encode(original)
	if err != nil {
		t.Fatalf("Encode: %v", err)
	}
	defer msg.Close()
	got, err := zlinkproto.Decode[*wrapperspb.StringValue](msg)
	if err != nil {
		t.Fatalf("Decode: %v", err)
	}
	if got.Value != original.Value {
		t.Fatalf("mismatch: got %q want %q", got.Value, original.Value)
	}
}

package messagepack_test

import (
	"testing"

	zlinkmsgpack "zlink.systems/zlink/codec/messagepack"
)

type person struct {
	Name string `msgpack:"name"`
	Age  int    `msgpack:"age"`
}

func TestEncodeDecodeRoundTrip(t *testing.T) {
	original := person{
		Name: "Alice",
		Age:  30,
	}

	msg, err := zlinkmsgpack.Encode(&original)
	if err != nil {
		t.Fatalf("Encode() error = %v", err)
	}
	defer msg.Close()

	got, err := zlinkmsgpack.Decode[person](msg)
	if err != nil {
		t.Fatalf("Decode() error = %v", err)
	}

	if got.Name != original.Name {
		t.Fatalf("Name = %q, want %q", got.Name, original.Name)
	}
	if got.Age != original.Age {
		t.Fatalf("Age = %d, want %d", got.Age, original.Age)
	}
}

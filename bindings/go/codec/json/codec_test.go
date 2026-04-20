package json_test

import (
	"testing"

	zlinkjson "zlink/codec/json"
)

type person struct {
	Name string `json:"name"`
	Age  int    `json:"age"`
}

func TestEncodeDecodeRoundTrip(t *testing.T) {
	original := person{
		Name: "Alice",
		Age:  30,
	}

	msg, err := zlinkjson.Encode(&original)
	if err != nil {
		t.Fatalf("Encode() error = %v", err)
	}
	defer msg.Close()

	got, err := zlinkjson.Decode[person](msg)
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

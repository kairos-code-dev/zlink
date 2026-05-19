package zlink_test

import (
	"testing"

	"zlink.systems/zlink"
)

func TestMessageDiagnosticAPI(t *testing.T) {
	msg := newMessage(t, "diagnostic")
	defer msg.Close()

	if got := msg.RefCount(); got < 1 {
		t.Fatalf("RefCount() = %d, want >= 1", got)
	}

	value, ok, err := msg.GetProperty("missing")
	if err != nil {
		t.Fatalf("GetProperty() error = %v", err)
	}
	if ok {
		t.Fatalf("GetProperty() ok = true, want false for missing property")
	}
	if value != "" {
		t.Fatalf("GetProperty() value = %q, want empty string for missing property", value)
	}
}

func TestNewMessageWithSizeExposesWritablePayload(t *testing.T) {
	msg, err := zlink.NewMessageWithSize(3)
	if err != nil {
		t.Fatalf("NewMessageWithSize() error = %v", err)
	}
	defer msg.Close()

	data := msg.Data()
	data[0] = 0x01
	data[1] = 0x02
	data[2] = 0x03

	if got := msg.Bytes(); string(got) != string([]byte{0x01, 0x02, 0x03}) {
		t.Fatalf("Bytes() = %v, want [1 2 3]", got)
	}
}

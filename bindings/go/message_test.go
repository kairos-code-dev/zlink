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
	if msg.IsEmpty() {
		t.Fatalf("IsEmpty() = true, want false")
	}
}

func TestMessageCanonicalCopyHelpers(t *testing.T) {
	msg, err := zlink.NewMessageFromString("copy-payload")
	if err != nil {
		t.Fatalf("NewMessageFromString() error = %v", err)
	}
	defer msg.Close()

	if got := msg.Text(); got != "copy-payload" {
		t.Fatalf("Text() = %q, want copy-payload", got)
	}
	if got := msg.String(); got != "copy-payload" {
		t.Fatalf("String() = %q, want copy-payload", got)
	}

	bytes := msg.Bytes()
	bytes[0] = 'C'
	if got := msg.Text(); got != "copy-payload" {
		t.Fatalf("Bytes() should return a snapshot, message text = %q", got)
	}
	if got := string(msg.BytesCopy()); got != "copy-payload" {
		t.Fatalf("BytesCopy() = %q, want copy-payload", got)
	}

	copyMsg, err := msg.Copy()
	if err != nil {
		t.Fatalf("Copy() error = %v", err)
	}
	defer copyMsg.Close()
	if got := copyMsg.Text(); got != "copy-payload" {
		t.Fatalf("Copy().Text() = %q, want copy-payload", got)
	}

	target := make([]byte, len("copy-payload"))
	written, err := msg.CopyTo(target)
	if err != nil {
		t.Fatalf("CopyTo() error = %v", err)
	}
	if written != len("copy-payload") || string(target) != "copy-payload" {
		t.Fatalf("CopyTo() wrote %d/%q, want %d/copy-payload", written, string(target), len("copy-payload"))
	}
	if !msg.TryCopyTo(make([]byte, len("copy-payload"))) {
		t.Fatalf("TryCopyTo() = false, want true")
	}
	if msg.TryCopyTo(make([]byte, len("copy-payload")-1)) {
		t.Fatalf("TryCopyTo() = true for small destination, want false")
	}
}

func TestMessageEmptyState(t *testing.T) {
	msg, err := zlink.NewMessageWithSize(0)
	if err != nil {
		t.Fatalf("NewMessageWithSize() error = %v", err)
	}
	defer msg.Close()

	if !msg.IsEmpty() {
		t.Fatalf("IsEmpty() = false, want true")
	}
}

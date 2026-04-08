package zlink_test

import "testing"

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

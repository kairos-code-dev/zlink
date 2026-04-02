package zlink_test

import (
	"fmt"
	"net"
	"os"
	"sync/atomic"
	"testing"
	"time"

	"zlink"
)

var endpointCounter uint64

func newContext(t testing.TB) *zlink.Context {
	t.Helper()
	ctx, err := zlink.NewContext()
	if err != nil {
		t.Fatalf("NewContext() error = %v", err)
	}
	return ctx
}

func inprocEndpoint(prefix string) string {
	id := atomic.AddUint64(&endpointCounter, 1)
	return fmt.Sprintf("inproc://%s-%d-%d", prefix, os.Getpid(), id)
}

func tcpEndpoint(t testing.TB) string {
	t.Helper()
	listener, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("listen error: %v", err)
	}
	addr := listener.Addr().String()
	if err := listener.Close(); err != nil {
		t.Fatalf("close listener error: %v", err)
	}
	return "tcp://" + addr
}

func newMessage(t testing.TB, data string) *zlink.Message {
	t.Helper()
	msg, err := zlink.NewMessage([]byte(data))
	if err != nil {
		t.Fatalf("NewMessage(%q) error = %v", data, err)
	}
	return msg
}

func waitForMonitorEvent(t testing.TB, mon *zlink.SocketMonitor, timeout time.Duration) *zlink.MonitorEvent {
	t.Helper()
	type result struct {
		event *zlink.MonitorEvent
		err   error
	}
	ch := make(chan result, 1)
	go func() {
		event, err := mon.Recv()
		ch <- result{event: event, err: err}
	}()

	select {
	case out := <-ch:
		if out.err != nil {
			t.Fatalf("monitor recv error: %v", out.err)
		}
		return out.event
	case <-time.After(timeout):
		t.Fatalf("timed out waiting for monitor event after %s", timeout)
		return nil
	}
}

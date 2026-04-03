package zlink_test

import (
	"testing"
	"time"

	"zlink"
)

func TestTrySendReturnsExplicitOutcome(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	socket, _ := ctx.PairSocket()
	defer socket.Close()
	_ = socket.Bind(inprocEndpoint("try-send"))

	result, err := socket.TrySend(newMessage(t, "data"))
	if err != nil {
		t.Fatalf("TrySend() error = %v", err)
	}
	if result != zlink.SendResultNotReady && result != zlink.SendResultBackpressured && result != zlink.SendResultSent {
		t.Fatalf("unexpected SendResult = %v", result)
	}
}

func TestTryPublishReturnsExplicitOutcome(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	socket, _ := ctx.PubSocket()
	defer socket.Close()
	_ = socket.Bind(inprocEndpoint("try-publish"))

	result, err := socket.TryPublish("topic", newMessage(t, "data"))
	if err != nil {
		t.Fatalf("TryPublish() error = %v", err)
	}
	if result != zlink.SendResultNotReady && result != zlink.SendResultBackpressured && result != zlink.SendResultSent {
		t.Fatalf("unexpected SendResult = %v", result)
	}
}

func TestBlockingSendFailureSurfacesError(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	router, _ := ctx.RouterSocket()
	defer router.Close()

	if err := router.SetMandatory(true); err != nil {
		t.Fatalf("SetMandatory() error = %v", err)
	}
	if err := router.Bind(inprocEndpoint("router-send-fail")); err != nil {
		t.Fatalf("Bind() error = %v", err)
	}

	rid, err := zlink.NewRoutingID([]byte("missing-peer"))
	if err != nil {
		t.Fatalf("NewRoutingID() error = %v", err)
	}
	if err := router.SendTo(rid, newMessage(t, "data")); err == nil {
		t.Fatalf("SendTo() should surface an error when no peer exists")
	}
}

func TestBlockingSendFailurePreservesMessagePayload(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	router, _ := ctx.RouterSocket()
	defer router.Close()

	if err := router.SetMandatory(true); err != nil {
		t.Fatalf("SetMandatory() error = %v", err)
	}
	if err := router.Bind(inprocEndpoint("router-send-preserve")); err != nil {
		t.Fatalf("Bind() error = %v", err)
	}

	rid, err := zlink.NewRoutingID([]byte("missing-peer"))
	if err != nil {
		t.Fatalf("NewRoutingID() error = %v", err)
	}

	msg := newMessage(t, "preserve-me")
	defer msg.Close()

	if err := router.SendTo(rid, msg); err == nil {
		t.Fatalf("SendTo() should surface an error when no peer exists")
	}
	if got := string(msg.Data()); got != "preserve-me" {
		t.Fatalf("message payload after SendTo() failure = %q, want %q", got, "preserve-me")
	}
}

func TestTrySendDoesNotSwallowNonEagainErrors(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	socket, _ := ctx.PairSocket()
	if err := socket.Close(); err != nil {
		t.Fatalf("Close() error = %v", err)
	}

	if _, err := socket.TrySend(newMessage(t, "data")); err == nil {
		t.Fatalf("TrySend() on closed socket should surface an error")
	}
}

func TestTryPublishDoesNotSwallowNonEagainErrors(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	socket, _ := ctx.PubSocket()
	if err := socket.Close(); err != nil {
		t.Fatalf("Close() error = %v", err)
	}

	if _, err := socket.TryPublish("topic", newMessage(t, "data")); err == nil {
		t.Fatalf("TryPublish() on closed socket should surface an error")
	}
}

func TestTryPublishFailurePreservesMessagePayload(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	socket, _ := ctx.PubSocket()
	msg := newMessage(t, "preserve-me")
	defer msg.Close()

	if err := socket.Close(); err != nil {
		t.Fatalf("Close() error = %v", err)
	}
	if _, err := socket.TryPublish("topic", msg); err == nil {
		t.Fatalf("TryPublish() on closed socket should surface an error")
	}
	if got := string(msg.Data()); got != "preserve-me" {
		t.Fatalf("message payload after TryPublish() failure = %q, want %q", got, "preserve-me")
	}
}

func TestTryRecvDoesNotSwallowNonEagainErrors(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	socket, _ := ctx.PairSocket()
	if err := socket.Close(); err != nil {
		t.Fatalf("Close() error = %v", err)
	}

	if _, _, err := socket.TryRecv(); err == nil {
		t.Fatalf("TryRecv() on closed socket should surface an error")
	}
}

func TestTrySubscribeDoesNotSwallowNonEagainErrors(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	socket, _ := ctx.SubSocket()
	if err := socket.Close(); err != nil {
		t.Fatalf("Close() error = %v", err)
	}

	if _, _, err := socket.TrySubscribe(); err == nil {
		t.Fatalf("TrySubscribe() on closed socket should surface an error")
	}
}

func TestTryReceiveSubscriptionEventDoesNotSwallowNonEagainErrors(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	socket, _ := ctx.XPubSocket()
	if err := socket.Close(); err != nil {
		t.Fatalf("Close() error = %v", err)
	}

	if _, _, err := socket.TryReceiveSubscriptionEvent(); err == nil {
		t.Fatalf("TryReceiveSubscriptionEvent() on closed socket should surface an error")
	}
}

func TestCallbackModeConflictsWithDirectRecv(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	endpoint := tcpEndpoint(t)
	server, _ := ctx.PairSocket()
	client, _ := ctx.PairSocket()
	defer server.Close()
	defer client.Close()

	serverMon, err := zlink.OpenSocketMonitor(server, zlink.MonitorEventConnectionReady)
	if err != nil {
		t.Fatalf("OpenSocketMonitor(server) error = %v", err)
	}
	defer serverMon.Close()

	clientMon, err := zlink.OpenSocketMonitor(client, zlink.MonitorEventConnectionReady)
	if err != nil {
		t.Fatalf("OpenSocketMonitor(client) error = %v", err)
	}
	defer clientMon.Close()

	if err := server.Bind(endpoint); err != nil {
		t.Fatalf("Bind() error = %v", err)
	}
	if err := client.Connect(endpoint); err != nil {
		t.Fatalf("Connect() error = %v", err)
	}

	waitForMonitorEvent(t, serverMon, 5*time.Second)
	waitForMonitorEvent(t, clientMon, 5*time.Second)

	delivered := make(chan struct{}, 1)
	if err := server.OnReceive(func(received *zlink.Received) {
		defer received.Close()
		delivered <- struct{}{}
	}); err != nil {
		t.Fatalf("OnReceive() error = %v", err)
	}

	if _, err := server.Recv(); err == nil {
		t.Fatalf("Recv() after OnReceive() should fail")
	}

	if err := client.Send(newMessage(t, "callback-data")); err != nil {
		t.Fatalf("Send() error = %v", err)
	}

	select {
	case <-delivered:
	case <-time.After(5 * time.Second):
		t.Fatalf("callback was not delivered within 5s")
	}
}

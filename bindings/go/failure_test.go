package zlink_test

import (
	"errors"
	"sync/atomic"
	"testing"
	"time"

	"zlink"
)

func TestSendDontWaitReturnsErrorWhenUnroutable(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	socket, _ := ctx.PairSocket()
	defer socket.Close()
	_ = socket.Bind(inprocEndpoint("try-send"))

	if err := socket.Send(zlink.SendFlagsDontWait, newMessage(t, "data")); err == nil {
		t.Fatalf("Send() with DontWait on idle socket should surface an error")
	}
}

func TestPublishDontWaitReturnsErrorWhenUnroutable(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	socket, _ := ctx.PubSocket()
	defer socket.Close()
	_ = socket.Bind(inprocEndpoint("try-publish"))

	if err := socket.Publish("topic", zlink.SendFlagsDontWait, newMessage(t, "data")); err != nil {
		t.Fatalf("Publish() with DontWait on idle socket should succeed: %v", err)
	}
}

func TestReplyAPIsRejectUnsupportedFlags(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	router, err := ctx.RouterSocket()
	if err != nil {
		t.Fatalf("RouterSocket() error = %v", err)
	}
	defer router.Close()

	node, err := ctx.SpotNode()
	if err != nil {
		t.Fatalf("SpotNode() error = %v", err)
	}
	defer node.Close()

	spot, err := node.Spot()
	if err != nil {
		t.Fatalf("Spot() error = %v", err)
	}
	defer spot.Close()

	nodeRID := zlink.NewRoutingID([]byte("node"))
	spotRID := zlink.NewRoutingID([]byte("spot"))
	peerRID := zlink.NewRoutingID([]byte("peer"))

	assertUnsupported := func(name string, call func() error) {
		t.Helper()
		err := call()
		if err == nil {
			t.Fatalf("%s should fail for unsupported flags", name)
		}
		var submitErr *zlink.SubmitError
		if !errors.As(err, &submitErr) {
			t.Fatalf("%s error type = %T, want *SubmitError", name, err)
		}
		if submitErr.Result != zlink.SubmitNotSupported {
			t.Fatalf("%s result = %v, want %v", name, submitErr.Result, zlink.SubmitNotSupported)
		}
	}

	assertUnsupported("Spot.ReplyToSpot", func() error {
		return spot.ReplyToSpot(nodeRID, spotRID, 1, zlink.SendFlags(2), newMessage(t, "reply"))
	})
	assertUnsupported("Spot.ReplyToRouter", func() error {
		return spot.ReplyToRouter(peerRID, 1, zlink.SendFlags(2), newMessage(t, "reply"))
	})
	assertUnsupported("RouterSocket.ReplyToSpot", func() error {
		return router.ReplyToSpot(nodeRID, spotRID, 1, zlink.SendFlags(2), newMessage(t, "reply"))
	})
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

	rid := zlink.NewRoutingID([]byte("missing-peer"))
	if err := router.SendTo(rid, zlink.SendFlagsNone, newMessage(t, "data")); err == nil {
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

	rid := zlink.NewRoutingID([]byte("missing-peer"))

	msg := newMessage(t, "preserve-me")
	defer msg.Close()

	if err := router.SendTo(rid, zlink.SendFlagsNone, msg); err == nil {
		t.Fatalf("SendTo() should surface an error when no peer exists")
	}
	if got := string(msg.Data()); got != "preserve-me" {
		t.Fatalf("message payload after SendTo() failure = %q, want %q", got, "preserve-me")
	}
}

func TestSendDoesNotSwallowClosedSocketErrors(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	socket, _ := ctx.PairSocket()
	if err := socket.Close(); err != nil {
		t.Fatalf("Close() error = %v", err)
	}

	if err := socket.Send(zlink.SendFlagsNone, newMessage(t, "data")); err == nil {
		t.Fatalf("Send() on closed socket should surface an error")
	}
}

func TestPublishDoesNotSwallowClosedSocketErrors(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	socket, _ := ctx.PubSocket()
	if err := socket.Close(); err != nil {
		t.Fatalf("Close() error = %v", err)
	}

	if err := socket.Publish("topic", zlink.SendFlagsNone, newMessage(t, "data")); err == nil {
		t.Fatalf("Publish() on closed socket should surface an error")
	}
}

func TestPublishFailurePreservesMessagePayload(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	socket, _ := ctx.PubSocket()
	msg := newMessage(t, "preserve-me")
	defer msg.Close()

	if err := socket.Close(); err != nil {
		t.Fatalf("Close() error = %v", err)
	}
	if err := socket.Publish("topic", zlink.SendFlagsNone, msg); err == nil {
		t.Fatalf("Publish() on closed socket should surface an error")
	}
	if got := string(msg.Data()); got != "preserve-me" {
		t.Fatalf("message payload after Publish() failure = %q, want %q", got, "preserve-me")
	}
}

func TestRecvDoesNotSwallowClosedSocketErrors(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	socket, _ := ctx.PairSocket()
	if err := socket.Close(); err != nil {
		t.Fatalf("Close() error = %v", err)
	}

	if _, err := socket.Recv(zlink.RecvFlagsDontWait); err == nil {
		t.Fatalf("Recv() on closed socket should surface an error")
	}
}

func TestSubscribeDoesNotSwallowClosedSocketErrors(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	socket, _ := ctx.SubSocket()
	if err := socket.Close(); err != nil {
		t.Fatalf("Close() error = %v", err)
	}

	if _, err := socket.Subscribe(zlink.RecvFlagsDontWait); err == nil {
		t.Fatalf("Subscribe() on closed socket should surface an error")
	}
}

func TestReceiveSubscriptionEventDoesNotSwallowClosedSocketErrors(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	socket, _ := ctx.XPubSocket()
	if err := socket.Close(); err != nil {
		t.Fatalf("Close() error = %v", err)
	}

	if _, err := socket.ReceiveSubscriptionEvent(zlink.RecvFlagsDontWait); err == nil {
		t.Fatalf("ReceiveSubscriptionEvent() on closed socket should surface an error")
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

	if _, err := server.Recv(zlink.RecvFlagsNone); err == nil {
		t.Fatalf("Recv() after OnReceive() should fail")
	}

	if err := client.Send(zlink.SendFlagsNone, newMessage(t, "callback-data")); err != nil {
		t.Fatalf("Send() error = %v", err)
	}

	select {
	case <-delivered:
	case <-time.After(5 * time.Second):
		t.Fatalf("callback was not delivered within 5s")
	}
}

func TestReceiveCallbackCanUseBlockingSend(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	endpoint := tcpEndpoint(t)
	server, _ := ctx.PairSocket()
	client, _ := ctx.PairSocket()
	defer server.Close()
	defer client.Close()

	if err := server.Bind(endpoint); err != nil {
		t.Fatalf("Bind() error = %v", err)
	}
	if err := client.Connect(endpoint); err != nil {
		t.Fatalf("Connect() error = %v", err)
	}

	sendErrs := make(chan error, 1)
	if err := server.OnReceive(func(received *zlink.Received) {
		defer received.Close()
		reply, err := zlink.NewMessage([]byte("reply-from-callback"))
		if err != nil {
			sendErrs <- err
			return
		}
		if err := server.Send(zlink.SendFlagsNone, reply); err != nil {
			_ = reply.Close()
			sendErrs <- err
			return
		}
		sendErrs <- nil
	}); err != nil {
		t.Fatalf("OnReceive() error = %v", err)
	}

	if err := client.Send(zlink.SendFlagsNone, newMessage(t, "request")); err != nil {
		t.Fatalf("Send() error = %v", err)
	}

	select {
	case err := <-sendErrs:
		if err != nil {
			t.Fatalf("callback Send() error = %v", err)
		}
	case <-time.After(5 * time.Second):
		t.Fatalf("callback send did not complete within 5s")
	}

	reply, err := client.Recv(zlink.RecvFlagsNone)
	if err != nil {
		t.Fatalf("Recv() error = %v", err)
	}
	defer reply.Close()
	part, err := reply.SinglePartOrError()
	if err != nil {
		t.Fatalf("SinglePartOrError() error = %v", err)
	}
	if got := string(part.Data()); got != "reply-from-callback" {
		t.Fatalf("callback reply = %q, want %q", got, "reply-from-callback")
	}
}

func TestReceiveCallbackPanicDoesNotCloseSocketOrStopFutureCallbacks(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	endpoint := tcpEndpoint(t)
	server, _ := ctx.PairSocket()
	client, _ := ctx.PairSocket()
	defer server.Close()
	defer client.Close()

	if err := server.Bind(endpoint); err != nil {
		t.Fatalf("Bind() error = %v", err)
	}
	if err := client.Connect(endpoint); err != nil {
		t.Fatalf("Connect() error = %v", err)
	}

	var calls atomic.Int32
	delivered := make(chan struct{}, 1)
	if err := server.OnReceive(func(received *zlink.Received) {
		switch calls.Add(1) {
		case 1:
			panic("callback panic for policy test")
		default:
			defer received.Close()
			delivered <- struct{}{}
		}
	}); err != nil {
		t.Fatalf("OnReceive() error = %v", err)
	}

	if err := client.Send(zlink.SendFlagsNone, newMessage(t, "first")); err != nil {
		t.Fatalf("Send(first) error = %v", err)
	}
	if err := client.Send(zlink.SendFlagsNone, newMessage(t, "second")); err != nil {
		t.Fatalf("Send(second) error = %v", err)
	}

	select {
	case <-delivered:
	case <-time.After(5 * time.Second):
		t.Fatalf("callback delivery stopped after panic")
	}

	if got := calls.Load(); got < 2 {
		t.Fatalf("callback invocation count = %d, want at least 2", got)
	}
}

func TestCloseInsideReceiveCallbackDoesNotDeadlock(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	endpoint := tcpEndpoint(t)
	server, _ := ctx.PairSocket()
	client, _ := ctx.PairSocket()
	defer client.Close()

	if err := server.Bind(endpoint); err != nil {
		t.Fatalf("Bind() error = %v", err)
	}
	if err := client.Connect(endpoint); err != nil {
		t.Fatalf("Connect() error = %v", err)
	}

	closed := make(chan error, 1)
	if err := server.OnReceive(func(received *zlink.Received) {
		defer received.Close()
		closed <- server.Close()
	}); err != nil {
		t.Fatalf("OnReceive() error = %v", err)
	}

	if err := client.Send(zlink.SendFlagsNone, newMessage(t, "close-from-callback")); err != nil {
		t.Fatalf("Send() error = %v", err)
	}

	select {
	case err := <-closed:
		if err != nil {
			t.Fatalf("Close() from callback error = %v", err)
		}
	case <-time.After(5 * time.Second):
		t.Fatalf("Close() from callback deadlocked")
	}
}

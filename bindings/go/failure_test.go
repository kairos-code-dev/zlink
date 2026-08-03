package zlink_test

import (
	"context"
	"errors"
	"net"
	"strings"
	"sync/atomic"
	"testing"
	"time"

	zlink "zlink.systems/zlink/v11"
)

func TestSubmitContextCancellationUsesStandardErrors(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	socket, err := ctx.PairSocket()
	if err != nil {
		t.Fatalf("PairSocket() error = %v", err)
	}
	defer socket.Close()

	canceled, cancel := context.WithCancel(context.Background())
	cancel()
	message := newMessage(t, "context-canceled")
	defer message.Close()

	if _, err := socket.Send().Message(message).Submit(canceled); !errors.Is(err, context.Canceled) {
		t.Fatalf("Send().Submit(canceled) error = %v, want context.Canceled", err)
	}
	if got := string(message.Data()); got != "context-canceled" {
		t.Fatalf("canceled send consumed message payload = %q", got)
	}
}

func TestRequestContextDeadlineUsesStandardErrors(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	socket, err := ctx.DealerSocket()
	if err != nil {
		t.Fatalf("DealerSocket() error = %v", err)
	}
	defer socket.Close()

	deadline, cancel := context.WithDeadline(context.Background(), time.Now().Add(-time.Second))
	defer cancel()
	if _, err := socket.Request().Bytes([]byte("context-deadline")).SubmitAsync(deadline); !errors.Is(err, context.DeadlineExceeded) {
		t.Fatalf("Request().SubmitAsync(expired) error = %v, want context.DeadlineExceeded", err)
	}
}

func TestSendDontWaitDoesNotTreatTemporaryBackpressureAsError(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	socket, _ := ctx.PairSocket()
	defer socket.Close()
	_ = socket.Bind(inprocEndpoint("try-send"))

	ok, err := socket.Send().Message(newMessage(t, "data")).Flags(zlink.SendFlagsDontWait).Submit(nil)
	if err != nil {
		t.Fatalf("Send() with DontWait should not error for backpressure, got: %v", err)
	}
	_ = ok
}

func TestPublishDontWaitReturnsErrorWhenUnroutable(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	socket, _ := ctx.PubSocket()
	defer socket.Close()
	_ = socket.Bind(inprocEndpoint("try-publish"))

	if _, err := socket.Publish("topic").Message(newMessage(t, "data")).Flags(zlink.SendFlagsDontWait).Submit(nil); err != nil {
		t.Fatalf("Publish() with DontWait on idle socket should succeed: %v", err)
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

	rid := zlink.NewRoutingID([]byte("missing-peer"))
	if _, err := router.SendTo(rid).Message(newMessage(t, "data")).Submit(nil); err == nil {
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

	if _, err := router.SendTo(rid).Message(msg).Submit(nil); err == nil {
		t.Fatalf("SendTo() should surface an error when no peer exists")
	}
	if got := string(msg.Data()); got != "preserve-me" {
		t.Fatalf("message payload after SendTo() failure = %q, want %q", got, "preserve-me")
	}
}

func TestBlockingSendFailurePreservesBytesPayload(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	router, _ := ctx.RouterSocket()
	defer router.Close()

	if err := router.SetMandatory(true); err != nil {
		t.Fatalf("SetMandatory() error = %v", err)
	}
	if err := router.Bind(inprocEndpoint("router-send-bytes-preserve")); err != nil {
		t.Fatalf("Bind() error = %v", err)
	}

	rid := zlink.NewRoutingID([]byte("missing-peer"))
	payload := []byte("preserve-bytes")

	if _, err := router.SendTo(rid).Bytes(payload).Submit(nil); err == nil {
		t.Fatalf("SendTo().Bytes() should surface an error when no peer exists")
	}
	if got := string(payload); got != "preserve-bytes" {
		t.Fatalf("bytes payload after SendTo().Bytes() failure = %q, want %q", got, "preserve-bytes")
	}
}

func TestMoveMessageFailureConsumesMessagePayload(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	router, _ := ctx.RouterSocket()
	defer router.Close()

	if err := router.SetMandatory(true); err != nil {
		t.Fatalf("SetMandatory() error = %v", err)
	}
	if err := router.Bind(inprocEndpoint("router-move-send-consumes")); err != nil {
		t.Fatalf("Bind() error = %v", err)
	}

	rid := zlink.NewRoutingID([]byte("missing-peer"))

	msg := newMessage(t, "consume-me")
	defer msg.Close()

	if _, err := router.SendTo(rid).MoveMessage(msg).Submit(nil); err == nil {
		t.Fatalf("MoveMessage SendTo() should surface an error when no peer exists")
	}
	if got := msg.Data(); got != nil {
		t.Fatalf("message payload after MoveMessage failure = %q, want nil", string(got))
	}
}

func TestSendDoesNotSwallowClosedSocketErrors(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	socket, _ := ctx.PairSocket()
	if err := socket.Close(); err != nil {
		t.Fatalf("Close() error = %v", err)
	}

	if _, err := socket.Send().Message(newMessage(t, "data")).Submit(nil); err == nil {
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

	if _, err := socket.Publish("topic").Message(newMessage(t, "data")).Submit(nil); err == nil {
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
	if _, err := socket.Publish("topic").Message(msg).Submit(nil); err == nil {
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

	var received zlink.Received
	if _, err := socket.Recv(&received, zlink.RecvFlagsDontWait); err == nil {
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

	var message zlink.TopicMessage
	if _, err := socket.Subscribe(&message, zlink.RecvFlagsDontWait); err == nil {
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

	var event zlink.SubscriptionEvent
	if _, err := socket.ReceiveSubscriptionEvent(&event, zlink.RecvFlagsDontWait); err == nil {
		t.Fatalf("ReceiveSubscriptionEvent() on closed socket should surface an error")
	}
}

func TestCallbackModeConflictsWithDirectRecv(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	endpoint := tcpEndpoint(t)
	server, _ := ctx.StreamSocket()
	defer server.Close()

	if err := server.Bind(endpoint); err != nil {
		t.Fatalf("Bind() error = %v", err)
	}
	conn, err := net.DialTimeout("tcp", strings.TrimPrefix(endpoint, "tcp://"), 5*time.Second)
	if err != nil {
		t.Fatalf("net.DialTimeout() error = %v", err)
	}
	defer conn.Close()

	delivered := make(chan struct{}, 1)
	if err := server.OnPacket(func(source zlink.RoutingID, header, body *zlink.Message) {
		defer header.Close()
		defer body.Close()
		_ = source
		delivered <- struct{}{}
	}); err != nil {
		t.Fatalf("OnPacket() error = %v", err)
	}

	var received zlink.Received
	if _, err := server.Recv(&received, zlink.RecvFlagsNone); err == nil {
		t.Fatalf("Recv() after OnPacket() should fail")
	}

	writeStreamPacket(t, conn, []byte("callback-data"))

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
	server, _ := ctx.StreamSocket()
	defer server.Close()

	if err := server.Bind(endpoint); err != nil {
		t.Fatalf("Bind() error = %v", err)
	}

	conn, err := net.DialTimeout("tcp", strings.TrimPrefix(endpoint, "tcp://"), 5*time.Second)
	if err != nil {
		t.Fatalf("net.DialTimeout() error = %v", err)
	}
	defer conn.Close()

	sendErrs := make(chan error, 1)
	if err := server.OnPacket(func(source zlink.RoutingID, header, body *zlink.Message) {
		packet := frameStreamPacketMessage(t, header, body)
		_ = header.Close()
		_ = body.Close()
		if _, err := server.SendTo(source).Message(packet).Submit(nil); err != nil {
			_ = packet.Close()
			sendErrs <- err
			return
		}
		sendErrs <- nil
	}); err != nil {
		t.Fatalf("OnPacket() error = %v", err)
	}

	writeStreamPacket(t, conn, []byte("request"))

	select {
	case err := <-sendErrs:
		if err != nil {
			t.Fatalf("callback Send() error = %v", err)
		}
	case <-time.After(5 * time.Second):
		t.Fatalf("callback send did not complete within 5s")
	}

	reply := readStreamPacketBody(t, conn)
	if got := string(reply); got != "request" {
		t.Fatalf("callback reply = %q, want %q", got, "request")
	}
}

func TestReceiveCallbackPanicDoesNotCloseSocketOrStopFutureCallbacks(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	endpoint := tcpEndpoint(t)
	server, _ := ctx.StreamSocket()
	defer server.Close()

	if err := server.Bind(endpoint); err != nil {
		t.Fatalf("Bind() error = %v", err)
	}

	conn, err := net.DialTimeout("tcp", strings.TrimPrefix(endpoint, "tcp://"), 5*time.Second)
	if err != nil {
		t.Fatalf("net.DialTimeout() error = %v", err)
	}
	defer conn.Close()

	var calls atomic.Int32
	delivered := make(chan struct{}, 1)
	if err := server.OnPacket(func(source zlink.RoutingID, header, body *zlink.Message) {
		defer header.Close()
		switch calls.Add(1) {
		case 1:
			panic("callback panic for policy test")
		default:
			defer body.Close()
			_ = source
			delivered <- struct{}{}
		}
	}); err != nil {
		t.Fatalf("OnPacket() error = %v", err)
	}

	writeStreamPacket(t, conn, []byte("first"))
	writeStreamPacket(t, conn, []byte("second"))

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
	server, _ := ctx.StreamSocket()
	defer server.Close()

	if err := server.Bind(endpoint); err != nil {
		t.Fatalf("Bind() error = %v", err)
	}

	conn, err := net.DialTimeout("tcp", strings.TrimPrefix(endpoint, "tcp://"), 5*time.Second)
	if err != nil {
		t.Fatalf("net.DialTimeout() error = %v", err)
	}
	defer conn.Close()

	closed := make(chan error, 1)
	if err := server.OnPacket(func(source zlink.RoutingID, header, body *zlink.Message) {
		defer header.Close()
		defer body.Close()
		_ = source
		closed <- server.Close()
	}); err != nil {
		t.Fatalf("OnPacket() error = %v", err)
	}

	writeStreamPacket(t, conn, []byte("close-from-callback"))

	select {
	case err := <-closed:
		if err != nil {
			t.Fatalf("Close() from callback error = %v", err)
		}
	case <-time.After(5 * time.Second):
		t.Fatalf("Close() from callback deadlocked")
	}
}

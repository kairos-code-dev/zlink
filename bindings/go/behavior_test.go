package zlink_test

import (
	"bytes"
	"testing"
	"time"

	"zlink"
)

func TestPairSendRecvRoundTrip(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	endpoint := inprocEndpoint("pair-roundtrip")
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

	if err := client.Send(zlink.SendFlagsNone, newMessage(t, "hello-pair")); err != nil {
		t.Fatalf("Send() error = %v", err)
	}

	received, err := server.Recv(zlink.RecvFlagsNone)
	if err != nil {
		t.Fatalf("Recv() error = %v", err)
	}
	defer received.Close()

	part, err := received.SinglePartOrError()
	if err != nil {
		t.Fatalf("SinglePartOrError() error = %v", err)
	}
	if !bytes.Equal(part.Data(), []byte("hello-pair")) {
		t.Fatalf("unexpected payload = %q", string(part.Data()))
	}
}

func TestPairMultipartRoundTrip(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	endpoint := inprocEndpoint("pair-multipart")
	server, _ := ctx.PairSocket()
	client, _ := ctx.PairSocket()
	defer server.Close()
	defer client.Close()

	_ = server.Bind(endpoint)
	_ = client.Connect(endpoint)

	if err := client.Send(zlink.SendFlagsNone, newMessage(t, "frame-1"), newMessage(t, "frame-2")); err != nil {
		t.Fatalf("Send() error = %v", err)
	}

	received, err := server.Recv(zlink.RecvFlagsNone)
	if err != nil {
		t.Fatalf("Recv() error = %v", err)
	}
	defer received.Close()

	if len(received.Parts()) != 2 {
		t.Fatalf("len(Parts()) = %d, want 2", len(received.Parts()))
	}
}

func TestPairRecvEmpty(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	socket, _ := ctx.PairSocket()
	defer socket.Close()
	_ = socket.Bind(inprocEndpoint("pair-try-recv"))

	received, err := socket.Recv(zlink.RecvFlagsDontWait)
	if err == nil {
		t.Fatalf("Recv() should return an error on empty non-blocking receive")
	}
	if received != nil {
		t.Fatalf("Recv() = %v, want nil", received)
	}
}

func TestSubSubscribeEmpty(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	socket, _ := ctx.SubSocket()
	defer socket.Close()

	message, err := socket.Subscribe(zlink.RecvFlagsDontWait)
	if err == nil {
		t.Fatalf("Subscribe() should return an error on empty non-blocking receive")
	}
	if message != nil {
		t.Fatalf("Subscribe() = %v, want nil", message)
	}
}

func TestDealerRouterRoundTrip(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	endpoint := inprocEndpoint("dealer-router")
	router, _ := ctx.RouterSocket()
	dealer, _ := ctx.DealerSocket()
	defer router.Close()
	defer dealer.Close()

	rid, err := zlink.NewRoutingID([]byte("dealer-42"))
	if err != nil {
		t.Fatalf("NewRoutingID() error = %v", err)
	}

	_ = router.Bind(endpoint)
	_ = dealer.SetRoutingID(rid)
	_ = dealer.Connect(endpoint)
	_ = dealer.SetRecvTimeout(5 * time.Second)

	if err := dealer.Send(zlink.SendFlagsNone, newMessage(t, "request")); err != nil {
		t.Fatalf("dealer Send() error = %v", err)
	}

	request, err := router.Recv(zlink.RecvFlagsNone)
	if err != nil {
		t.Fatalf("router Recv() error = %v", err)
	}
	defer request.Close()

	if err := router.SendTo(request.RoutingID(), zlink.SendFlagsNone, newMessage(t, "response")); err != nil {
		t.Fatalf("router SendTo() error = %v", err)
	}

	response, err := dealer.Recv(zlink.RecvFlagsNone)
	if err != nil {
		t.Fatalf("dealer Recv() error = %v", err)
	}
	defer response.Close()

	part, _ := response.SinglePartOrError()
	if !bytes.Equal(part.Data(), []byte("response")) {
		t.Fatalf("unexpected response = %q", string(part.Data()))
	}
}

func TestPubSubRoundTrip(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	endpoint := inprocEndpoint("pubsub")
	pubSocket, _ := ctx.PubSocket()
	subSocket, _ := ctx.SubSocket()
	defer pubSocket.Close()
	defer subSocket.Close()

	_ = pubSocket.Bind(endpoint)
	_ = subSocket.Connect(endpoint)
	_ = subSocket.SetSubscription("market.")
	_ = subSocket.SetRecvTimeout(5 * time.Second)

	if err := pubSocket.Publish("market.price", zlink.SendFlagsNone, newMessage(t, "42.5")); err != nil {
		t.Fatalf("Publish() error = %v", err)
	}

	message, err := subSocket.Subscribe(zlink.RecvFlagsNone)
	if err != nil {
		t.Fatalf("Subscribe() error = %v", err)
	}
	defer message.Close()

	if got := message.Topic(); got != "market.price" {
		t.Fatalf("Topic() = %q, want %q", got, "market.price")
	}
}

func TestXPubReceiveSubscriptionEventEmpty(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	socket, _ := ctx.XPubSocket()
	defer socket.Close()

	event, err := socket.ReceiveSubscriptionEvent(zlink.RecvFlagsDontWait)
	if err == nil {
		t.Fatalf("ReceiveSubscriptionEvent() should return an error on empty non-blocking receive")
	}
	if event != nil {
		t.Fatalf("ReceiveSubscriptionEvent() = %v, want nil", event)
	}
}

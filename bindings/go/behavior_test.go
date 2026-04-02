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

	if err := client.Send(newMessage(t, "hello-pair")); err != nil {
		t.Fatalf("Send() error = %v", err)
	}

	received, err := server.Recv()
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

	if err := client.Send(newMessage(t, "frame-1"), newMessage(t, "frame-2")); err != nil {
		t.Fatalf("Send() error = %v", err)
	}

	received, err := server.Recv()
	if err != nil {
		t.Fatalf("Recv() error = %v", err)
	}
	defer received.Close()

	if len(received.Parts()) != 2 {
		t.Fatalf("len(Parts()) = %d, want 2", len(received.Parts()))
	}
}

func TestPairTryRecvEmpty(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	socket, _ := ctx.PairSocket()
	defer socket.Close()
	_ = socket.Bind(inprocEndpoint("pair-try-recv"))

	received, ok, err := socket.TryRecv()
	if err != nil {
		t.Fatalf("TryRecv() error = %v", err)
	}
	if ok || received != nil {
		t.Fatalf("TryRecv() = (%v, %v), want (nil, false)", received, ok)
	}
}

func TestSubTrySubscribeEmpty(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	socket, _ := ctx.SubSocket()
	defer socket.Close()

	message, ok, err := socket.TrySubscribe()
	if err != nil {
		t.Fatalf("TrySubscribe() error = %v", err)
	}
	if ok || message != nil {
		t.Fatalf("TrySubscribe() = (%v, %v), want (nil, false)", message, ok)
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

	if err := dealer.Send(newMessage(t, "request")); err != nil {
		t.Fatalf("dealer Send() error = %v", err)
	}

	request, err := router.Recv()
	if err != nil {
		t.Fatalf("router Recv() error = %v", err)
	}
	defer request.Close()

	if err := router.SendTo(request.RoutingID(), newMessage(t, "response")); err != nil {
		t.Fatalf("router SendTo() error = %v", err)
	}

	response, err := dealer.Recv()
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

	if err := pubSocket.Publish("market.price", newMessage(t, "42.5")); err != nil {
		t.Fatalf("Publish() error = %v", err)
	}

	message, err := subSocket.Subscribe()
	if err != nil {
		t.Fatalf("Subscribe() error = %v", err)
	}
	defer message.Close()

	if got := message.Topic(); got != "market.price" {
		t.Fatalf("Topic() = %q, want %q", got, "market.price")
	}
}

func TestXPubTryReceiveSubscriptionEventEmpty(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	socket, _ := ctx.XPubSocket()
	defer socket.Close()

	event, ok, err := socket.TryReceiveSubscriptionEvent()
	if err != nil {
		t.Fatalf("TryReceiveSubscriptionEvent() error = %v", err)
	}
	if ok || event != nil {
		t.Fatalf("TryReceiveSubscriptionEvent() = (%v, %v), want (nil, false)", event, ok)
	}
}

package zlink_test

import (
	"bytes"
	"testing"
	"time"

	"zlink.systems/zlink"
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

	if _, err := client.Send().Message(newMessage(t, "hello-pair")).Submit(nil); err != nil {
		t.Fatalf("Send() error = %v", err)
	}

	var received zlink.Received
	if _, err := server.Recv(&received, zlink.RecvFlagsNone); err != nil {
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

	if _, err := client.Send().Message(newMessage(t, "frame-1")).Message(newMessage(t, "frame-2")).Submit(nil); err != nil {
		t.Fatalf("Send() error = %v", err)
	}

	var received zlink.Received
	if _, err := server.Recv(&received, zlink.RecvFlagsNone); err != nil {
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

	var received zlink.Received
	ok, err := socket.Recv(&received, zlink.RecvFlagsDontWait)
	if err != nil {
		t.Fatalf("Recv() error = %v, want nil for non-blocking empty receive", err)
	}
	if ok {
		t.Fatalf("Recv() returned ok=true on empty non-blocking receive")
	}
}

func TestSubSubscribeEmpty(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	socket, _ := ctx.SubSocket()
	defer socket.Close()

	var message zlink.TopicMessage
	ok, err := socket.Subscribe(&message, zlink.RecvFlagsDontWait)
	if err != nil {
		t.Fatalf("Subscribe() error = %v", err)
	}
	if ok {
		t.Fatalf("Subscribe() returned ok=true on empty non-blocking receive")
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

	rid := zlink.NewRoutingID([]byte("dealer-42"))

	_ = router.Bind(endpoint)
	_ = dealer.SetRoutingID(rid)
	_ = dealer.Connect(endpoint)
	_ = dealer.SetRecvTimeout(5 * time.Second)

	if _, err := dealer.Send().Message(newMessage(t, "request")).Submit(nil); err != nil {
		t.Fatalf("dealer Send() error = %v", err)
	}

	var request zlink.Received
	if _, err := router.Recv(&request, zlink.RecvFlagsNone); err != nil {
		t.Fatalf("router Recv() error = %v", err)
	}
	defer request.Close()

	if _, err := router.SendTo(request.RoutingID()).Message(newMessage(t, "response")).Submit(nil); err != nil {
		t.Fatalf("router SendTo() error = %v", err)
	}

	var response zlink.Received
	if _, err := dealer.Recv(&response, zlink.RecvFlagsNone); err != nil {
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

	if _, err := pubSocket.Publish("market.price").Message(newMessage(t, "42.5")).Submit(nil); err != nil {
		t.Fatalf("Publish() error = %v", err)
	}

	var message zlink.TopicMessage
	ok, err := subSocket.Subscribe(&message, zlink.RecvFlagsNone)
	if err != nil {
		t.Fatalf("Subscribe() error = %v", err)
	}
	if !ok {
		t.Fatalf("Subscribe() returned ok=false")
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

	var event zlink.SubscriptionEvent
	ok, err := socket.ReceiveSubscriptionEvent(&event, zlink.RecvFlagsDontWait)
	if err != nil {
		t.Fatalf("ReceiveSubscriptionEvent() error = %v", err)
	}
	if ok {
		t.Fatalf("ReceiveSubscriptionEvent() returned ok=true on empty non-blocking receive")
	}
}

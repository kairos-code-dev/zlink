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

func TestPollerWaitWritesCallerOwnedEvents(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	endpoint := inprocEndpoint("poller-wait")
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

	poller, err := zlink.NewPoller()
	if err != nil {
		t.Fatalf("NewPoller() error = %v", err)
	}
	defer poller.Close()

	if err := poller.AddSocket(server, zlink.PollIn, 7); err != nil {
		t.Fatalf("AddSocket() error = %v", err)
	}
	if _, err := client.Send().Message(newMessage(t, "poller")).Submit(nil); err != nil {
		t.Fatalf("Send() error = %v", err)
	}

	events := make([]zlink.PollEvent, 4)
	n, err := poller.Wait(events, 5*time.Second)
	if err != nil {
		t.Fatalf("Wait() error = %v", err)
	}
	if n != 1 {
		t.Fatalf("Wait() count = %d, want 1", n)
	}
	if events[0].SourceKind != zlink.PollSourceSocket {
		t.Fatalf("SourceKind = %v, want socket", events[0].SourceKind)
	}
	if events[0].Slot != 7 {
		t.Fatalf("Slot = %d, want 7", events[0].Slot)
	}
	if events[0].Revents&zlink.PollIn == 0 {
		t.Fatalf("Revents = %v, want PollIn", events[0].Revents)
	}
}

func TestPollerRejectsEmptyEventSlice(t *testing.T) {
	poller, err := zlink.NewPoller()
	if err != nil {
		t.Fatalf("NewPoller() error = %v", err)
	}
	defer poller.Close()

	if _, err := poller.Wait(nil, 0); err == nil {
		t.Fatalf("Wait(nil) error = nil, want invalid argument")
	}
	if _, err := poller.Wait([]zlink.PollEvent{}, 0); err == nil {
		t.Fatalf("Wait(empty) error = nil, want invalid argument")
	}
}

func TestPollerTimerEventUsesSlot(t *testing.T) {
	timer, err := zlink.NewTimer()
	if err != nil {
		t.Fatalf("NewTimer() error = %v", err)
	}
	defer timer.Close()
	poller, err := zlink.NewPoller()
	if err != nil {
		t.Fatalf("NewPoller() error = %v", err)
	}
	defer poller.Close()

	if err := poller.AddTimer(timer, 11); err != nil {
		t.Fatalf("AddTimer() error = %v", err)
	}
	if err := timer.Start(uint64(time.Millisecond), 1); err != nil {
		t.Fatalf("Timer.Start() error = %v", err)
	}

	events := make([]zlink.PollEvent, 2)
	n, err := poller.Wait(events, 5*time.Second)
	if err != nil {
		t.Fatalf("Wait() error = %v", err)
	}
	if n != 1 {
		t.Fatalf("Wait() count = %d, want 1", n)
	}
	if events[0].SourceKind != zlink.PollSourceTimer {
		t.Fatalf("SourceKind = %v, want timer", events[0].SourceKind)
	}
	if events[0].Slot != 11 {
		t.Fatalf("Slot = %d, want 11", events[0].Slot)
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

func TestPairRecvPartRoundTrip(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	endpoint := inprocEndpoint("pair-recv-part")
	server, _ := ctx.PairSocket()
	client, _ := ctx.PairSocket()
	defer server.Close()
	defer client.Close()

	_ = server.Bind(endpoint)
	_ = client.Connect(endpoint)
	_ = server.SetRecvTimeout(5 * time.Second)

	if _, err := client.Send().Message(newMessage(t, "hello-part")).Submit(nil); err != nil {
		t.Fatalf("Send() error = %v", err)
	}

	part, err := zlink.NewMessageWithSize(0)
	if err != nil {
		t.Fatalf("NewMessageWithSize() error = %v", err)
	}
	defer part.Close()
	result, ok, err := server.RecvPart(part, zlink.RecvFlagsNone)
	if err != nil {
		t.Fatalf("RecvPart() error = %v", err)
	}
	if !ok {
		t.Fatalf("RecvPart() returned ok=false")
	}
	if result.More {
		t.Fatalf("RecvPart() More = true, want false")
	}
	if result.RoutingID.Size() != 0 {
		t.Fatalf("RecvPart() RoutingID size = %d, want 0", result.RoutingID.Size())
	}
	if got := string(part.Data()); got != "hello-part" {
		t.Fatalf("payload = %q, want %q", got, "hello-part")
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

func TestRouterRecvPartRoundTrip(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	endpoint := inprocEndpoint("router-recv-part")
	router, _ := ctx.RouterSocket()
	dealer, _ := ctx.DealerSocket()
	defer router.Close()
	defer dealer.Close()

	rid := zlink.NewRoutingID([]byte("dealer-part"))
	_ = router.Bind(endpoint)
	_ = dealer.SetRoutingID(rid)
	_ = dealer.Connect(endpoint)
	_ = router.SetRecvTimeout(5 * time.Second)

	if _, err := dealer.Send().Message(newMessage(t, "routed-part")).Submit(nil); err != nil {
		t.Fatalf("dealer Send() error = %v", err)
	}

	part, err := zlink.NewMessageWithSize(0)
	if err != nil {
		t.Fatalf("NewMessageWithSize() error = %v", err)
	}
	defer part.Close()
	result, ok, err := router.RecvPart(part, zlink.RecvFlagsNone)
	if err != nil {
		t.Fatalf("router RecvPart() error = %v", err)
	}
	if !ok {
		t.Fatalf("router RecvPart() returned ok=false")
	}
	if result.More {
		t.Fatalf("router RecvPart() More = true, want false")
	}
	if !bytes.Equal(result.RoutingID.Bytes(), rid.Bytes()) {
		t.Fatalf("RoutingID = %q, want %q", string(result.RoutingID.Bytes()), string(rid.Bytes()))
	}
	if got := string(part.Data()); got != "routed-part" {
		t.Fatalf("payload = %q, want %q", got, "routed-part")
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

func TestSubSubscribePartRoundTrip(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	endpoint := inprocEndpoint("pubsub-part")
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

	msg, err := zlink.NewMessageWithSize(0)
	if err != nil {
		t.Fatalf("NewMessageWithSize() error = %v", err)
	}
	defer msg.Close()
	topic := make([]byte, 64)
	result, ok, err := subSocket.SubscribePart(msg, topic, zlink.RecvFlagsNone)
	if err != nil {
		t.Fatalf("SubscribePart() error = %v", err)
	}
	if !ok {
		t.Fatalf("SubscribePart() returned ok=false")
	}
	if result.More {
		t.Fatalf("SubscribePart() More = true, want false")
	}
	if got := string(topic[:result.TopicLen]); got != "market.price" {
		t.Fatalf("topic = %q, want %q", got, "market.price")
	}
	if got := string(msg.Data()); got != "42.5" {
		t.Fatalf("payload = %q, want %q", got, "42.5")
	}
}

func TestSpotSubscribePartRoundTrip(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	publisherNode, err := ctx.SpotNode()
	if err != nil {
		t.Fatalf("publisher SpotNode() error = %v", err)
	}
	defer publisherNode.Close()
	subscriberNode, err := ctx.SpotNode()
	if err != nil {
		t.Fatalf("subscriber SpotNode() error = %v", err)
	}
	defer subscriberNode.Close()

	publisherEndpoint := tcpEndpoint(t)
	subscriberEndpoint := tcpEndpoint(t)
	if err := publisherNode.SetRoutingID(zlink.NewRoutingID([]byte("z-go-test-spot-publisher"))); err != nil {
		t.Fatalf("publisher SetRoutingID() error = %v", err)
	}
	if err := subscriberNode.SetRoutingID(zlink.NewRoutingID([]byte("a-go-test-spot-subscriber"))); err != nil {
		t.Fatalf("subscriber SetRoutingID() error = %v", err)
	}
	if err := publisherNode.Bind(publisherEndpoint); err != nil {
		t.Fatalf("publisher Bind() error = %v", err)
	}
	if err := subscriberNode.Bind(subscriberEndpoint); err != nil {
		t.Fatalf("subscriber Bind() error = %v", err)
	}
	if err := publisherNode.ConnectPeer(subscriberEndpoint); err != nil {
		t.Fatalf("publisher ConnectPeer() error = %v", err)
	}
	if err := subscriberNode.ConnectPeer(publisherEndpoint); err != nil {
		t.Fatalf("subscriber ConnectPeer() error = %v", err)
	}

	publisher, err := publisherNode.Spot()
	if err != nil {
		t.Fatalf("publisher Spot() error = %v", err)
	}
	defer publisher.Close()
	subscriber, err := subscriberNode.Spot()
	if err != nil {
		t.Fatalf("subscriber Spot() error = %v", err)
	}
	defer subscriber.Close()
	if err := publisher.SetRoutingID(zlink.NewRoutingID([]byte("z-go-test-spot-publisher-spot"))); err != nil {
		t.Fatalf("publisher spot SetRoutingID() error = %v", err)
	}
	if err := subscriber.SetRoutingID(zlink.NewRoutingID([]byte("a-go-test-spot-subscriber-spot"))); err != nil {
		t.Fatalf("subscriber spot SetRoutingID() error = %v", err)
	}
	if err := subscriber.SetSubscription("market.price"); err != nil {
		t.Fatalf("SetSubscription() error = %v", err)
	}
	if err := subscriber.SetRecvTimeout(5 * time.Second); err != nil {
		t.Fatalf("SetRecvTimeout() error = %v", err)
	}

	msg, err := zlink.NewMessageWithSize(0)
	if err != nil {
		t.Fatalf("NewMessageWithSize() error = %v", err)
	}
	defer msg.Close()
	topic := make([]byte, 64)
	deadline := time.Now().Add(5 * time.Second)
	for time.Now().Before(deadline) {
		_, _ = publisherNode.StatusSnapshot()
		_, _ = subscriberNode.StatusSnapshot()
		if _, err := publisher.Publish("market.price").Message(newMessage(t, "42.5")).Submit(nil); err != nil {
			time.Sleep(10 * time.Millisecond)
			continue
		}
		result, ok, err := subscriber.SubscribePart(msg, topic, zlink.RecvFlagsDontWait)
		if err != nil {
			time.Sleep(10 * time.Millisecond)
			continue
		}
		if !ok {
			time.Sleep(10 * time.Millisecond)
			continue
		}
		if result.More {
			t.Fatalf("SubscribePart() More = true, want false")
		}
		if got := string(topic[:result.TopicLen]); got != "market.price" {
			t.Fatalf("topic = %q, want %q", got, "market.price")
		}
		if got := string(msg.Data()); got != "42.5" {
			t.Fatalf("payload = %q, want %q", got, "42.5")
		}
		return
	}
	t.Fatalf("spot SubscribePart() did not receive message before timeout")
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

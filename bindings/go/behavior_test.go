package zlink_test

import (
	"bytes"
	"testing"
	"time"

	zlink "zlink.systems/zlink/v11"
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

func TestPairSendBytesRoundTrip(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	endpoint := inprocEndpoint("pair-bytes")
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

	payload := []byte("hello-bytes")
	if _, err := client.Send().Bytes(payload).Submit(nil); err != nil {
		t.Fatalf("Send().Bytes() error = %v", err)
	}
	if string(payload) != "hello-bytes" {
		t.Fatalf("Bytes() mutated caller payload = %q", string(payload))
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
	if !bytes.Equal(part.Data(), []byte("hello-bytes")) {
		t.Fatalf("unexpected payload = %q", string(part.Data()))
	}
}

func TestPairMultipartBytesRoundTrip(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	endpoint := inprocEndpoint("pair-multipart-bytes")
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

	message := newMessage(t, "message-part")
	defer message.Close()
	if _, err := client.Send().
		Bytes([]byte("first-bytes")).
		Message(message).
		Bytes([]byte("last-bytes")).
		Submit(nil); err != nil {
		t.Fatalf("multipart Send() error = %v", err)
	}

	var received zlink.Received
	if _, err := server.Recv(&received, zlink.RecvFlagsNone); err != nil {
		t.Fatalf("Recv() error = %v", err)
	}
	defer received.Close()
	parts := received.Parts()
	if len(parts) != 3 {
		t.Fatalf("multipart parts = %d, want 3", len(parts))
	}
	want := []string{"first-bytes", "message-part", "last-bytes"}
	for i, part := range parts {
		if got := string(part.Data()); got != want[i] {
			t.Fatalf("part[%d] = %q, want %q", i, got, want[i])
		}
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

func TestPollEmptyItemsUsesTimeout(t *testing.T) {
	if n, err := zlink.Poll(nil, time.Millisecond); err != nil || n != 0 {
		t.Fatalf("Poll(nil) = (%d, %v), want (0, nil)", n, err)
	}
	if n, err := zlink.Poll([]zlink.PollItem{}, time.Millisecond); err != nil || n != 0 {
		t.Fatalf("Poll(empty) = (%d, %v), want (0, nil)", n, err)
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

func TestPollerCapacityLeavesRemainingReadySource(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	sender1, _ := ctx.PairSocket()
	receiver1, _ := ctx.PairSocket()
	sender2, _ := ctx.PairSocket()
	receiver2, _ := ctx.PairSocket()
	defer sender1.Close()
	defer receiver1.Close()
	defer sender2.Close()
	defer receiver2.Close()

	endpoint1 := inprocEndpoint("poller-capacity-a")
	endpoint2 := inprocEndpoint("poller-capacity-b")
	_ = sender1.Bind(endpoint1)
	_ = receiver1.Connect(endpoint1)
	_ = sender2.Bind(endpoint2)
	_ = receiver2.Connect(endpoint2)

	poller, err := zlink.NewPoller()
	if err != nil {
		t.Fatalf("NewPoller() error = %v", err)
	}
	defer poller.Close()
	if err := poller.AddSocket(receiver1, zlink.PollIn, 101); err != nil {
		t.Fatalf("AddSocket(receiver1) error = %v", err)
	}
	if err := poller.AddSocket(receiver2, zlink.PollIn, 102); err != nil {
		t.Fatalf("AddSocket(receiver2) error = %v", err)
	}
	if _, err := sender1.Send().Message(newMessage(t, "a")).Submit(nil); err != nil {
		t.Fatalf("Send(a) error = %v", err)
	}
	if _, err := sender2.Send().Message(newMessage(t, "b")).Submit(nil); err != nil {
		t.Fatalf("Send(b) error = %v", err)
	}

	events := make([]zlink.PollEvent, 1)
	n, err := poller.Wait(events, 5*time.Second)
	if err != nil {
		t.Fatalf("Wait(first) error = %v", err)
	}
	if n != 1 {
		t.Fatalf("Wait(first) count = %d, want 1", n)
	}
	firstSlot := events[0].Slot
	if firstSlot != 101 && firstSlot != 102 {
		t.Fatalf("first slot = %d, want 101 or 102", firstSlot)
	}
	if firstSlot == 101 {
		assertRecvText(t, receiver1, "a")
	} else {
		assertRecvText(t, receiver2, "b")
	}

	n, err = poller.Wait(events, 5*time.Second)
	if err != nil {
		t.Fatalf("Wait(second) error = %v", err)
	}
	if n != 1 {
		t.Fatalf("Wait(second) count = %d, want 1", n)
	}
	if events[0].Slot == firstSlot {
		t.Fatalf("second slot = %d, want remaining source", events[0].Slot)
	}
	if events[0].Slot == 101 {
		assertRecvText(t, receiver1, "a")
	} else {
		assertRecvText(t, receiver2, "b")
	}
}

func TestPollerModifyRemoveAndTimeout(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	sender, _ := ctx.PairSocket()
	receiver, _ := ctx.PairSocket()
	defer sender.Close()
	defer receiver.Close()

	endpoint := inprocEndpoint("poller-modify-remove")
	_ = sender.Bind(endpoint)
	_ = receiver.Connect(endpoint)

	poller, err := zlink.NewPoller()
	if err != nil {
		t.Fatalf("NewPoller() error = %v", err)
	}
	defer poller.Close()
	if err := poller.AddSocket(receiver, zlink.PollIn, 31); err != nil {
		t.Fatalf("AddSocket() error = %v", err)
	}
	if err := poller.ModifySocket(receiver, 0); err != nil {
		t.Fatalf("ModifySocket(none) error = %v", err)
	}
	if _, err := sender.Send().Message(newMessage(t, "hidden")).Submit(nil); err != nil {
		t.Fatalf("Send(hidden) error = %v", err)
	}
	events := make([]zlink.PollEvent, 1)
	n, err := poller.Wait(events, 20*time.Millisecond)
	if err != nil {
		t.Fatalf("Wait(timeout) error = %v", err)
	}
	if n != 0 {
		t.Fatalf("Wait(timeout) count = %d, want 0", n)
	}

	if err := poller.ModifySocket(receiver, zlink.PollIn); err != nil {
		t.Fatalf("ModifySocket(PollIn) error = %v", err)
	}
	n, err = poller.Wait(events, 5*time.Second)
	if err != nil {
		t.Fatalf("Wait(ready) error = %v", err)
	}
	if n != 1 || events[0].Slot != 31 {
		t.Fatalf("Wait(ready) = (%d, slot %d), want (1, 31)", n, events[0].Slot)
	}
	assertRecvText(t, receiver, "hidden")

	if err := poller.RemoveSocket(receiver); err != nil {
		t.Fatalf("RemoveSocket() error = %v", err)
	}
	if _, err := sender.Send().Message(newMessage(t, "removed")).Submit(nil); err != nil {
		t.Fatalf("Send(removed) error = %v", err)
	}
	n, err = poller.Wait(events, 0)
	if err != nil {
		t.Fatalf("Wait(after remove) error = %v", err)
	}
	if n != 0 {
		t.Fatalf("Wait(after remove) count = %d, want 0", n)
	}
}

func TestPollerDistinguishesTimerAndSocketInSameBuffer(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	sender, _ := ctx.PairSocket()
	receiver, _ := ctx.PairSocket()
	defer sender.Close()
	defer receiver.Close()

	timer, err := zlink.NewTimer()
	if err != nil {
		t.Fatalf("NewTimer() error = %v", err)
	}
	defer timer.Close()

	endpoint := inprocEndpoint("poller-timer-socket")
	_ = sender.Bind(endpoint)
	_ = receiver.Connect(endpoint)

	poller, err := zlink.NewPoller()
	if err != nil {
		t.Fatalf("NewPoller() error = %v", err)
	}
	defer poller.Close()
	if err := poller.AddSocket(receiver, zlink.PollIn, 41); err != nil {
		t.Fatalf("AddSocket() error = %v", err)
	}
	if err := poller.AddTimer(timer, 42); err != nil {
		t.Fatalf("AddTimer() error = %v", err)
	}
	if _, err := sender.Send().Message(newMessage(t, "socket")).Submit(nil); err != nil {
		t.Fatalf("Send(socket) error = %v", err)
	}
	if err := timer.Start(uint64(5*time.Millisecond), 1); err != nil {
		t.Fatalf("Timer.Start() error = %v", err)
	}

	events := make([]zlink.PollEvent, 2)
	sawSocket := false
	sawTimer := false
	deadline := time.Now().Add(2 * time.Second)
	for (!sawSocket || !sawTimer) && time.Now().Before(deadline) {
		n, err := poller.Wait(events, 200*time.Millisecond)
		if err != nil {
			t.Fatalf("Wait() error = %v", err)
		}
		for i := 0; i < n; i++ {
			switch events[i].SourceKind {
			case zlink.PollSourceSocket:
				if events[i].Slot != 41 {
					t.Fatalf("socket slot = %d, want 41", events[i].Slot)
				}
				assertRecvText(t, receiver, "socket")
				sawSocket = true
			case zlink.PollSourceTimer:
				if events[i].Slot != 42 {
					t.Fatalf("timer slot = %d, want 42", events[i].Slot)
				}
				if fireCount, ok, err := timer.Recv(); err != nil || !ok || fireCount != 1 {
					t.Fatalf("Timer.Recv() = (%d, %v, %v), want (1, true, nil)", fireCount, ok, err)
				}
				sawTimer = true
			}
		}
	}
	if !sawSocket || !sawTimer {
		t.Fatalf("sawSocket=%v sawTimer=%v, want both", sawSocket, sawTimer)
	}
}

func assertRecvText(t testing.TB, socket interface {
	Recv(*zlink.Received, zlink.RecvFlags) (bool, error)
}, want string) {
	t.Helper()
	var received zlink.Received
	ok, err := socket.Recv(&received, zlink.RecvFlagsNone)
	if err != nil {
		t.Fatalf("Recv() error = %v", err)
	}
	if !ok {
		t.Fatalf("Recv() ok = false, want true")
	}
	defer received.Close()
	part, err := received.SinglePartOrError()
	if err != nil {
		t.Fatalf("SinglePartOrError() error = %v", err)
	}
	if string(part.Data()) != want {
		t.Fatalf("recv payload = %q, want %q", string(part.Data()), want)
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
	_ = server.SetReceiveTimeout(5 * time.Second)

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
	_ = dealer.SetReceiveTimeout(5 * time.Second)

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
	_ = router.SetReceiveTimeout(5 * time.Second)

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
	_ = subSocket.SetReceiveTimeout(5 * time.Second)

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
	_ = subSocket.SetReceiveTimeout(5 * time.Second)

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

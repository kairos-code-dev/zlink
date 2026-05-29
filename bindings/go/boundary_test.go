package zlink_test

import (
	"strings"
	"testing"
	"time"

	zlink "zlink.systems/zlink/contracts"
)

func TestRoutingIDBoundary(t *testing.T) {
	valid := strings.Repeat("a", 255)
	if got := zlink.NewRoutingID([]byte(valid)); got.Size() != 255 {
		t.Fatalf("NewRoutingID(255 bytes).Size() = %d, want 255", got.Size())
	}

	invalid := strings.Repeat("b", 256)
	if got := zlink.NewRoutingID([]byte(invalid)); got.Size() != 0 {
		t.Fatalf("NewRoutingID(256 bytes).Size() = %d, want 0", got.Size())
	}
}

func TestDurationOverflowFailsFast(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	socket, _ := ctx.PairSocket()
	defer socket.Close()

	if err := socket.SetReceiveTimeout((1<<31)*time.Millisecond + time.Second); err == nil {
		t.Fatalf("SetReceiveTimeout() overflow should fail")
	}
}

func TestDurationMaxInt32MillisAccepted(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	socket, _ := ctx.PairSocket()
	defer socket.Close()

	maxDuration := time.Duration((1<<31)-1) * time.Millisecond
	if err := socket.SetReceiveTimeout(maxDuration); err != nil {
		t.Fatalf("SetReceiveTimeout(max int32 millis) error = %v", err)
	}
}

func TestNullByteValidation(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	pair, _ := ctx.PairSocket()
	defer pair.Close()

	if err := pair.Bind("inproc://bad\x00endpoint"); err == nil {
		t.Fatalf("Bind() with null byte should fail")
	}
	if err := pair.Bind("tcp://127.0.0.1/" + strings.Repeat("a", 256)); err == nil {
		t.Fatalf("Bind() with oversized endpoint should fail")
	}

	sub, _ := ctx.SubSocket()
	defer sub.Close()

	if err := sub.SetSubscription("bad\x00topic"); err == nil {
		t.Fatalf("SetSubscription() with null byte should fail")
	}
	if err := sub.UnsetSubscription("bad\x00topic"); err == nil {
		t.Fatalf("UnsetSubscription() with null byte should fail")
	}
	if _, err := ctx.Discovery(zlink.AutoConnectClientServer, strings.Repeat("s", 256)); err == nil {
		t.Fatalf("Discovery() with oversized channel name should fail")
	}
	if discovery, err := ctx.Discovery(zlink.AutoConnectClientServer, strings.Repeat("s", 255)); err != nil {
		t.Fatalf("Discovery() with max-sized channel name error = %v", err)
	} else {
		discovery.Close()
	}
}

func TestNilInputValidation(t *testing.T) {
	if got := zlink.NewRoutingID(nil); got.Size() != 0 {
		t.Fatalf("NewRoutingID(nil).Size() = %d, want 0", got.Size())
	}

	ctx := newContext(t)
	defer ctx.Close()

	socket, _ := ctx.PairSocket()
	defer socket.Close()

	if _, err := socket.Send().Message(nil).Submit(nil); err == nil {
		t.Fatalf("Send(nil) should fail")
	}
	stream, _ := ctx.StreamSocket()
	defer stream.Close()

	if err := stream.OnPacket(nil); err == nil {
		t.Fatalf("OnPacket(nil) should fail")
	}
}

package zlink_test

import (
	"bytes"
	"testing"
	"time"

	zlink "zlink.systems/zlink/contracts"
)

func TestSpotRouteBridgeRouterSendEmitsRelayPacket(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	node, err := ctx.SpotNode()
	if err != nil {
		t.Fatalf("SpotNode() error = %v", err)
	}
	defer node.Close()
	bridgeRouter, err := ctx.RouterSocket()
	if err != nil {
		t.Fatalf("RouterSocket() error = %v", err)
	}
	defer bridgeRouter.Close()
	router, err := ctx.RouterSocket()
	if err != nil {
		t.Fatalf("RouterSocket() error = %v", err)
	}
	defer router.Close()
	bridge, err := node.CreateRouteBridge(nil)
	if err != nil {
		t.Fatalf("CreateRouteBridge() error = %v", err)
	}
	defer bridge.Close()

	endpoint := inprocEndpoint("go-spot-route-bridge")
	targetNode := zlink.NewRoutingID([]byte("go-bridge-server"))
	if err := bridgeRouter.SetRoutingID(zlink.NewRoutingID([]byte("go-bridge-client"))); err != nil {
		t.Fatalf("bridgeRouter SetRoutingID() error = %v", err)
	}
	if err := router.SetRoutingID(targetNode); err != nil {
		t.Fatalf("router SetRoutingID() error = %v", err)
	}
	if err := router.Bind(endpoint); err != nil {
		t.Fatalf("Bind() error = %v", err)
	}
	if err := bridgeRouter.Connect(endpoint); err != nil {
		t.Fatalf("Connect() error = %v", err)
	}
	time.Sleep(50 * time.Millisecond)
	if err := bridge.AttachRouterChannel("api", bridgeRouter, nil); err != nil {
		t.Fatalf("AttachRouterChannel() error = %v", err)
	}

	targetSpot := zlink.NewRoutingID([]byte("target-spot"))
	payload := newMessage(t, "hello-bridge")
	defer payload.Close()
	ok, err := bridge.Send("api", targetNode, targetSpot, zlink.SendFlagsNone, payload)
	if err != nil {
		t.Fatalf("bridge.Send() error = %v", err)
	}
	if !ok {
		t.Fatalf("bridge.Send() returned false")
	}

	var received zlink.Received
	deadline := time.Now().Add(3 * time.Second)
	for {
		ok, err := router.Recv(&received, zlink.RecvFlagsDontWait)
		if err != nil {
			t.Fatalf("Recv() error = %v", err)
		}
		if ok {
			break
		}
		if time.Now().After(deadline) {
			t.Fatalf("timed out waiting for bridge relay packet")
		}
		time.Sleep(time.Millisecond)
	}
	defer received.Close()

	parts := received.Parts()
	if len(parts) < 3 {
		t.Fatalf("received %d parts, want at least 3", len(parts))
	}
	if got := string(parts[0].Data()); got != "__zlink.routed_spot.egress.send" {
		t.Fatalf("packet name = %q", got)
	}
	if !bytes.Equal(parts[1].Data(), targetSpot.Bytes()) {
		t.Fatalf("target spot rid = %q, want %q", parts[1].Data(), targetSpot.Bytes())
	}
	if got := string(bytes.TrimRight(parts[2].Data(), "\x00")); got != "hello-bridge" {
		t.Fatalf("payload = %q", got)
	}
}

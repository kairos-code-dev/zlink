package zlink_test

import (
	"bytes"
	"errors"
	"net"
	"strings"
	"testing"
	"time"

	"zlink.systems/zlink"
)

func TestSendConsumesMessageOwnership(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	endpoint := inprocEndpoint("ownership-send")
	server, _ := ctx.PairSocket()
	client, _ := ctx.PairSocket()
	defer server.Close()
	defer client.Close()

	_ = server.Bind(endpoint)
	_ = client.Connect(endpoint)

	msg := newMessage(t, "owned")
	if _, err := client.Send().Message(msg).Submit(nil); err != nil {
		t.Fatalf("Send() error = %v", err)
	}
	if data := msg.Data(); data != nil {
		t.Fatalf("moved message data should be nil, got %q", string(data))
	}
}

func TestRecvOwnershipCanBeExplicitlyReleased(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	endpoint := inprocEndpoint("ownership-recv")
	server, _ := ctx.PairSocket()
	client, _ := ctx.PairSocket()
	defer server.Close()
	defer client.Close()

	_ = server.Bind(endpoint)
	_ = client.Connect(endpoint)
	_, _ = client.Send().Message(newMessage(t, "recv-owned")).Submit(nil)

	var received zlink.Received
	if _, err := server.Recv(&received, zlink.RecvFlagsNone); err != nil {
		t.Fatalf("Recv() error = %v", err)
	}
	if err := received.Close(); err != nil {
		t.Fatalf("Received.Close() error = %v", err)
	}
}

func TestUnsentMessageSupportsExplicitClose(t *testing.T) {
	msg, err := zlink.NewMessage([]byte("unsent"))
	if err != nil {
		t.Fatalf("NewMessage() error = %v", err)
	}
	if err := msg.Close(); err != nil {
		t.Fatalf("Message.Close() error = %v", err)
	}
}

func TestStreamRecvShapeMatchesCallbackShape(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	directServer, _ := ctx.StreamSocket()
	defer directServer.Close()

	directEndpoint := tcpEndpoint(t)
	if err := directServer.Bind(directEndpoint); err != nil {
		t.Fatalf("direct Bind() error = %v", err)
	}
	directConn, err := net.DialTimeout("tcp", strings.TrimPrefix(directEndpoint, "tcp://"), 5*time.Second)
	if err != nil {
		t.Fatalf("direct dial error = %v", err)
	}
	defer directConn.Close()

	payload := []byte("frame-a/frame-b")
	if _, err := directConn.Write(payload); err != nil {
		t.Fatalf("direct Write() error = %v", err)
	}

	var directReceived zlink.Received
	if _, err := directServer.Recv(&directReceived, zlink.RecvFlagsNone); err != nil {
		t.Fatalf("direct Recv() error = %v", err)
	}
	defer directReceived.Close()

	callbackServer, _ := ctx.StreamSocket()
	defer callbackServer.Close()

	callbackEndpoint := tcpEndpoint(t)
	if err := callbackServer.Bind(callbackEndpoint); err != nil {
		t.Fatalf("callback Bind() error = %v", err)
	}

	callbackPartsCh := make(chan []byte, 1)
	if err := callbackServer.OnPacket(func(source zlink.RoutingID, header, body *zlink.Message) {
		defer header.Close()
		defer body.Close()
		_ = source
		callbackPartsCh <- append([]byte(nil), body.Data()...)
	}); err != nil {
		t.Fatalf("OnPacket() error = %v", err)
	}

	callbackConn, err := net.DialTimeout("tcp", strings.TrimPrefix(callbackEndpoint, "tcp://"), 5*time.Second)
	if err != nil {
		t.Fatalf("callback dial error = %v", err)
	}
	defer callbackConn.Close()

	writeStreamPacket(t, callbackConn, payload)

	var callbackPayload []byte
	select {
	case callbackPayload = <-callbackPartsCh:
	case <-time.After(5 * time.Second):
		t.Fatalf("callback did not deliver payload within 5s")
	}

	directPart, err := directReceived.SinglePartOrError()
	if err != nil {
		t.Fatalf("direct SinglePartOrError() error = %v", err)
	}
	if !bytes.Equal(directPart.Data(), callbackPayload) {
		t.Fatalf("payload mismatch: direct=%q callback=%q", string(directPart.Data()), string(callbackPayload))
	}
}

func TestSpotNodeCloseCascadesLiveSpots(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	node, err := ctx.SpotNode()
	if err != nil {
		t.Fatalf("SpotNode() error = %v", err)
	}

	spotA, err := node.Spot()
	if err != nil {
		t.Fatalf("Spot() spotA error = %v", err)
	}
	spotB, err := node.Spot()
	if err != nil {
		t.Fatalf("Spot() spotB error = %v", err)
	}

	if err := node.Close(); err != nil {
		t.Fatalf("SpotNode.Close() error = %v", err)
	}

	if err := spotA.Close(); err != nil {
		t.Fatalf("Spot.Close() after node cascade error = %v", err)
	}
	if err := spotB.Close(); err != nil {
		t.Fatalf("Spot.Close() after node cascade error = %v", err)
	}

	if _, err := node.Spot(); err == nil {
		t.Fatalf("SpotNode.Spot() should fail after node close")
	} else {
		var configErr *zlink.ConfigError
		if !errors.As(err, &configErr) {
			t.Fatalf("SpotNode.Spot() error type = %T, want *ConfigError", err)
		}
	}

	if err := spotA.SetSubscription("topic.alpha"); err == nil {
		t.Fatalf("Spot.SetSubscription() should fail after node cascade close")
	} else {
		var configErr *zlink.ConfigError
		if !errors.As(err, &configErr) {
			t.Fatalf("Spot.SetSubscription() error type = %T, want *ConfigError", err)
		}
		if configErr.Result != zlink.ConfigInvalidHandle {
			t.Fatalf("Spot.SetSubscription() result = %v, want %v", configErr.Result, zlink.ConfigInvalidHandle)
		}
	}
}

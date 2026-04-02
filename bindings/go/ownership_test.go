package zlink_test

import (
	"bytes"
	"testing"
	"time"

	"zlink"
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
	if err := client.Send(msg); err != nil {
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
	_ = client.Send(newMessage(t, "recv-owned"))

	received, err := server.Recv()
	if err != nil {
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

func TestMultipartRecvShapeMatchesCallbackShape(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	directServer, _ := ctx.PairSocket()
	directClient, _ := ctx.PairSocket()
	defer directServer.Close()
	defer directClient.Close()

	directEndpoint := tcpEndpoint(t)
	if err := directServer.Bind(directEndpoint); err != nil {
		t.Fatalf("direct Bind() error = %v", err)
	}
	if err := directClient.Connect(directEndpoint); err != nil {
		t.Fatalf("direct Connect() error = %v", err)
	}

	if err := directClient.Send(newMessage(t, "frame-a"), newMessage(t, "frame-b")); err != nil {
		t.Fatalf("direct Send() error = %v", err)
	}

	directReceived, err := directServer.Recv()
	if err != nil {
		t.Fatalf("direct Recv() error = %v", err)
	}
	defer directReceived.Close()

	callbackServer, _ := ctx.PairSocket()
	callbackClient, _ := ctx.PairSocket()
	defer callbackServer.Close()
	defer callbackClient.Close()

	callbackEndpoint := tcpEndpoint(t)
	if err := callbackServer.Bind(callbackEndpoint); err != nil {
		t.Fatalf("callback Bind() error = %v", err)
	}

	callbackPartsCh := make(chan [][]byte, 1)
	if err := callbackServer.OnReceive(func(received *zlink.Received) {
		defer received.Close()
		parts := make([][]byte, len(received.Parts()))
		for i, part := range received.Parts() {
			parts[i] = append([]byte(nil), part.Data()...)
		}
		callbackPartsCh <- parts
	}); err != nil {
		t.Fatalf("OnReceive() error = %v", err)
	}

	if err := callbackClient.Connect(callbackEndpoint); err != nil {
		t.Fatalf("callback Connect() error = %v", err)
	}
	if err := callbackClient.Send(newMessage(t, "frame-a"), newMessage(t, "frame-b")); err != nil {
		t.Fatalf("callback Send() error = %v", err)
	}

	var callbackParts [][]byte
	select {
	case callbackParts = <-callbackPartsCh:
	case <-time.After(5 * time.Second):
		t.Fatalf("callback did not deliver multipart payload within 5s")
	}

	directParts := directReceived.Parts()
	if len(directParts) != len(callbackParts) {
		t.Fatalf("part count mismatch: direct=%d callback=%d", len(directParts), len(callbackParts))
	}
	for i, part := range directParts {
		if !bytes.Equal(part.Data(), callbackParts[i]) {
			t.Fatalf("part %d mismatch: direct=%q callback=%q", i, string(part.Data()), string(callbackParts[i]))
		}
	}
}

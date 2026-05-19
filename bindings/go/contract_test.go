package zlink_test

import (
	"bytes"
	"fmt"
	"io"
	"net"
	"strings"
	"testing"
	"time"

	"zlink.systems/zlink"
)

func TestRuntimeVersionIsAvailable(t *testing.T) {
	version := zlink.RuntimeVersion()
	if version.Major <= 0 {
		t.Fatalf("RuntimeVersion().Major = %d, want > 0", version.Major)
	}
}

func TestContextLifecycle(t *testing.T) {
	ctx := newContext(t)
	if _, err := ctx.Options().IOThreads(); err != nil {
		t.Fatalf("IOThreads() error = %v", err)
	}
	if err := ctx.Shutdown(); err != nil {
		t.Fatalf("Shutdown() error = %v", err)
	}
	if err := ctx.Close(); err != nil {
		t.Fatalf("Close() error = %v", err)
	}
}

func TestServiceLayerEmptySnapshotsAndCanonicalNames(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	registry, err := ctx.Registry()
	if err != nil {
		t.Fatalf("Registry() error = %v", err)
	}
	defer registry.Close()
	registryPub := tcpEndpoint(t)
	registryRouter := tcpEndpoint(t)
	if err := registry.Bind(registryPub, registryRouter); err != nil {
		t.Fatalf("Bind() error = %v", err)
	}

	discovery, err := ctx.Discovery(zlink.AutoConnectClientServer, "svc-alpha")
	if err != nil {
		t.Fatalf("Discovery() error = %v", err)
	}
	defer discovery.Close()

	node, err := ctx.SpotNode()
	if err != nil {
		t.Fatalf("SpotNode() error = %v", err)
	}
	defer node.Close()

	query, err := ctx.RegistryQueryClient()
	if err != nil {
		t.Fatalf("RegistryQueryClient() error = %v", err)
	}
	defer query.Close()
	if err := query.Connect(registryRouter); err != nil {
		t.Fatalf("Connect() error = %v", err)
	}

	if err := discovery.SetValue(7); err != nil {
		t.Fatalf("SetValue() error = %v", err)
	}
	if got, err := discovery.GetValue(); err != nil || got != 7 {
		t.Fatalf("GetValue() = (%d, %v), want (7, nil)", got, err)
	}

	if status, err := registry.StatusSnapshot(); err != nil || status == nil {
		t.Fatalf("StatusSnapshot() = (%v, %v), want (non-nil, nil)", status, err)
	}
	if status, err := node.StatusSnapshot(); err != nil || status == nil {
		t.Fatalf("SpotNode.StatusSnapshot() = (%v, %v), want (non-nil, nil)", status, err)
	}
	if peers, err := node.PeersSnapshot(); err != nil || len(peers) != 0 {
		t.Fatalf("PeersSnapshot() = (%v, %v), want (empty, nil)", peers, err)
	}
	if subjects, err := node.SubjectsSnapshot(); err != nil || len(subjects) != 0 {
		t.Fatalf("SubjectsSnapshot() = (%v, %v), want (empty, nil)", subjects, err)
	}
	if topology, err := registry.TopologySnapshot(); err != nil || len(topology) != 0 {
		t.Fatalf("TopologySnapshot() = (%v, %v), want (empty, nil)", topology, err)
	}
	if snap, err := query.Snapshot(nil); err != nil || len(snap) != 0 {
		t.Fatalf("Snapshot(nil) = (%v, %v), want (empty, nil)", snap, err)
	}
}

func TestSpotNodeEntrySpotAndLookup(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	node, err := ctx.SpotNode()
	if err != nil {
		t.Fatalf("SpotNode() error = %v", err)
	}
	defer node.Close()

	entryA, err := node.EntrySpot()
	if err != nil {
		t.Fatalf("EntrySpot() error = %v", err)
	}
	defer entryA.Close()
	entryB, err := node.EntrySpot()
	if err != nil {
		t.Fatalf("EntrySpot() second error = %v", err)
	}
	defer entryB.Close()

	entryRID, err := entryA.RoutingID()
	if err != nil {
		t.Fatalf("entry RoutingID() error = %v", err)
	}
	secondEntryRID, err := entryB.RoutingID()
	if err != nil {
		t.Fatalf("second entry RoutingID() error = %v", err)
	}
	if !entryRID.Equal(secondEntryRID) {
		t.Fatalf("EntrySpot() facades have different routing ids")
	}

	lookupEntry, err := node.SpotLookup(entryRID)
	if err != nil {
		t.Fatalf("SpotLookup(entry) error = %v", err)
	}
	defer lookupEntry.Close()
	lookupEntryRID, err := lookupEntry.RoutingID()
	if err != nil {
		t.Fatalf("lookup entry RoutingID() error = %v", err)
	}
	if !lookupEntryRID.Equal(entryRID) {
		t.Fatalf("SpotLookup(entry) returned routing id %s, want %s", lookupEntryRID.Hex(), entryRID.Hex())
	}

	spot, err := node.Spot()
	if err != nil {
		t.Fatalf("Spot() error = %v", err)
	}
	defer spot.Close()
	spotRID, err := spot.RoutingID()
	if err != nil {
		t.Fatalf("spot RoutingID() error = %v", err)
	}
	lookupSpot, err := node.SpotLookup(spotRID)
	if err != nil {
		t.Fatalf("SpotLookup(user spot) error = %v", err)
	}
	defer lookupSpot.Close()
	lookupSpotRID, err := lookupSpot.RoutingID()
	if err != nil {
		t.Fatalf("lookup spot RoutingID() error = %v", err)
	}
	if !lookupSpotRID.Equal(spotRID) {
		t.Fatalf("SpotLookup(user spot) returned routing id %s, want %s", lookupSpotRID.Hex(), spotRID.Hex())
	}

	roomRID := zlink.NewRoutingID([]byte("go-room"))
	roomA, createdA, err := node.GetOrCreateSpot(roomRID)
	if err != nil {
		t.Fatalf("GetOrCreateSpot(first) error = %v", err)
	}
	defer roomA.Close()
	roomB, createdB, err := node.GetOrCreateSpot(roomRID)
	if err != nil {
		t.Fatalf("GetOrCreateSpot(second) error = %v", err)
	}
	defer roomB.Close()
	if !createdA || createdB {
		t.Fatalf("GetOrCreateSpot created flags = %v, %v; want true, false", createdA, createdB)
	}
	roomARID, err := roomA.RoutingID()
	if err != nil {
		t.Fatalf("roomA RoutingID() error = %v", err)
	}
	roomBRID, err := roomB.RoutingID()
	if err != nil {
		t.Fatalf("roomB RoutingID() error = %v", err)
	}
	if !roomARID.Equal(roomRID) || !roomBRID.Equal(roomRID) {
		t.Fatalf("GetOrCreateSpot returned unexpected routing ids")
	}
}

func TestAttachDiscoveryBlocksManualSocketLifecycleControls(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	dealer, err := ctx.DealerSocket()
	if err != nil {
		t.Fatalf("DealerSocket() error = %v", err)
	}
	defer dealer.Close()

	discovery, err := ctx.Discovery(zlink.AutoConnectClientServer, "attach-discovery")
	if err != nil {
		t.Fatalf("Discovery() error = %v", err)
	}
	defer discovery.Close()

	if err := dealer.AttachDiscovery(discovery); err != nil {
		t.Fatalf("AttachDiscovery() error = %v", err)
	}
	if err := dealer.Connect("tcp://127.0.0.1:5555"); err == nil {
		t.Fatalf("Connect() after AttachDiscovery() should fail")
	}
	if err := dealer.Disconnect("tcp://127.0.0.1:5555"); err == nil {
		t.Fatalf("Disconnect() after AttachDiscovery() should fail")
	}
	if err := dealer.Unbind("tcp://127.0.0.1:5555"); err == nil {
		t.Fatalf("Unbind() after AttachDiscovery() should fail")
	}
}

func TestRequestReplyCanonicalDealerRouterRoundTrip(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	routerSocket, err := ctx.RouterSocket()
	if err != nil {
		t.Fatalf("RouterSocket() error = %v", err)
	}
	defer routerSocket.Close()

	dealerSocket, err := ctx.DealerSocket()
	if err != nil {
		t.Fatalf("DealerSocket() error = %v", err)
	}
	defer dealerSocket.Close()

	endpoint := inprocEndpoint("request-reply")
	if err := routerSocket.Bind(endpoint); err != nil {
		t.Fatalf("Bind() error = %v", err)
	}
	if err := dealerSocket.Connect(endpoint); err != nil {
		t.Fatalf("Connect() error = %v", err)
	}

	done := make(chan struct{})
	go func() {
		defer close(done)
		var received zlink.Received
		if _, err := routerSocket.Recv(&received, zlink.RecvFlagsNone); err != nil {
			t.Errorf("Recv() error = %v", err)
			return
		}
		defer received.Close()
		if !received.HasRoutingID() {
			t.Errorf("HasRoutingID() = false")
			return
		}
		if !received.HasRequestSeq() {
			t.Errorf("HasRequestSeq() = false")
			return
		}
		if !received.IsSinglePart() {
			t.Errorf("IsSinglePart() = false")
			return
		}
		part, err := received.FirstPart()
		if err != nil {
			t.Errorf("FirstPart() error = %v", err)
			return
		}
		if got := string(part.Data()); got != "ping" {
			t.Errorf("request payload = %q, want %q", got, "ping")
			return
		}
		reply, err := zlink.NewMessage([]byte("pong"))
		if err != nil {
			t.Errorf("NewMessage() error = %v", err)
			return
		}
		if err := received.Reply().Message(reply).Submit(nil); err != nil {
			t.Errorf("Received.Reply() error = %v", err)
		}
	}()

	replyCh, err := dealerSocket.Request().Message(newMessage(t, "ping")).Timeout(2 * time.Second).SubmitAsync(nil)
	if err != nil {
		t.Fatalf("Request() error = %v", err)
	}
	completion := <-replyCh
	if completion.Err != nil {
		t.Fatalf("Request() completion error = %v", completion.Err)
	}
	reply := completion.Parts
	if len(reply) != 1 {
		t.Fatalf("Request() reply parts = %d, want 1", len(reply))
	}
	defer reply[0].Close()
	part := reply[0]
	if got := string(part.Data()); got != "pong" {
		t.Fatalf("reply payload = %q, want %q", got, "pong")
	}
	select {
	case <-done:
	case <-time.After(2 * time.Second):
		t.Fatalf("request handler did not run")
	}
}

func TestRouterRequestSupportPreservesDataReceiveSurface(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	routerSocket, err := ctx.RouterSocket()
	if err != nil {
		t.Fatalf("RouterSocket() error = %v", err)
	}
	defer routerSocket.Close()

	dealerSocket, err := ctx.DealerSocket()
	if err != nil {
		t.Fatalf("DealerSocket() error = %v", err)
	}
	defer dealerSocket.Close()

	endpoint := inprocEndpoint("request-reply-data")
	if err := routerSocket.Bind(endpoint); err != nil {
		t.Fatalf("Bind() error = %v", err)
	}
	if err := dealerSocket.Connect(endpoint); err != nil {
		t.Fatalf("Connect() error = %v", err)
	}

	payload, err := zlink.NewMessage([]byte("plain-data"))
	if err != nil {
		t.Fatalf("NewMessage() error = %v", err)
	}
	if _, err := dealerSocket.Send().Message(payload).Submit(nil); err != nil {
		t.Fatalf("Send() error = %v", err)
	}
	var received zlink.Received
	if _, err := routerSocket.Recv(&received, zlink.RecvFlagsNone); err != nil {
		t.Fatalf("Recv() error = %v", err)
	}
	defer received.Close()
	part, err := received.SinglePartOrError()
	if err != nil {
		t.Fatalf("SinglePartOrError() error = %v", err)
	}
	if got := string(part.Data()); got != "plain-data" {
		t.Fatalf("data payload = %q, want %q", got, "plain-data")
	}
}

func TestStreamRecvCanonicalRoundTrip(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	stream, err := ctx.StreamSocket()
	if err != nil {
		t.Fatalf("StreamSocket() error = %v", err)
	}
	defer stream.Close()

	monitor, err := zlink.OpenSocketMonitor(
		stream,
		zlink.MonitorEventAll,
	)
	if err != nil {
		t.Fatalf("OpenSocketMonitor() error = %v", err)
	}
	defer monitor.Close()

	endpoint := tcpEndpoint(t)
	if err := stream.Bind(endpoint); err != nil {
		t.Fatalf("Bind() error = %v", err)
	}

	conn, err := net.DialTimeout("tcp", strings.TrimPrefix(endpoint, "tcp://"), 5*time.Second)
	if err != nil {
		t.Fatalf("net.DialTimeout() error = %v", err)
	}
	defer conn.Close()

	waitForMonitorEvent(t, monitor, 5*time.Second)

	payload := []byte("hello-stream")
	if _, err := conn.Write(payload); err != nil {
		t.Fatalf("conn.Write() error = %v", err)
	}

	var received zlink.Received
	if _, err := stream.Recv(&received, zlink.RecvFlagsNone); err != nil {
		t.Fatalf("Recv() error = %v", err)
	}
	defer received.Close()
	part, err := received.SinglePartOrError()
	if err != nil {
		t.Fatalf("SinglePartOrError() error = %v", err)
	}
	if !bytes.Equal(part.Data(), payload) {
		t.Fatalf("stream payload = %q, want %q", string(part.Data()), string(payload))
	}

	reply := newMessage(t, "hello-stream")
	if _, err := stream.SendTo(received.RoutingID()).Message(reply).Submit(nil); err != nil {
		t.Fatalf("SendTo() error = %v", err)
	}

	buffer := make([]byte, len(payload))
	if _, err := io.ReadFull(conn, buffer); err != nil {
		t.Fatalf("io.ReadFull() error = %v", err)
	}
	if !bytes.Equal(buffer, payload) {
		t.Fatalf("stream reply = %q, want %q", string(buffer), string(payload))
	}
}

func TestStreamOnPacketCanonicalRoundTrip(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	stream, err := ctx.StreamSocket()
	if err != nil {
		t.Fatalf("StreamSocket() error = %v", err)
	}
	defer stream.Close()

	monitor, err := zlink.OpenSocketMonitor(
		stream,
		zlink.MonitorEventAll,
	)
	if err != nil {
		t.Fatalf("OpenSocketMonitor() error = %v", err)
	}
	defer monitor.Close()

	endpoint := tcpEndpoint(t)
	if err := stream.Bind(endpoint); err != nil {
		t.Fatalf("Bind() error = %v", err)
	}

	done := make(chan error, 1)
	if err := stream.OnPacket(func(source zlink.RoutingID, header, body *zlink.Message) {
		if got := string(body.Data()); got != "hello-stream" {
			done <- fmt.Errorf("packet body = %q, want %q", got, "hello-stream")
			_ = header.Close()
			_ = body.Close()
			return
		}
		packet := frameStreamPacketMessage(t, header, body)
		_ = header.Close()
		_ = body.Close()
		if _, err := stream.SendTo(source).Message(packet).Submit(nil); err != nil {
			_ = packet.Close()
			done <- err
			return
		}
		done <- nil
	}); err != nil {
		t.Fatalf("OnPacket() error = %v", err)
	}

	conn, err := net.DialTimeout("tcp", strings.TrimPrefix(endpoint, "tcp://"), 5*time.Second)
	if err != nil {
		t.Fatalf("net.DialTimeout() error = %v", err)
	}
	defer conn.Close()

	waitForMonitorEvent(t, monitor, 5*time.Second)

	payload := []byte("hello-stream")
	writeStreamPacket(t, conn, payload)

	buffer := readStreamPacketBody(t, conn)
	if !bytes.Equal(buffer, payload) {
		t.Fatalf("stream callback reply = %q, want %q", string(buffer), string(payload))
	}

	select {
	case err := <-done:
		if err != nil {
			t.Fatalf("callback send error = %v", err)
		}
	case <-time.After(2 * time.Second):
		t.Fatalf("stream callback did not reply")
	}
}

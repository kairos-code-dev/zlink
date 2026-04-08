package zlink_test

import (
	"testing"

	"zlink"
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

	discovery, err := ctx.Discovery(zlink.ServiceTypeSocket, "svc-alpha")
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

func TestAttachDiscoveryBlocksManualSocketLifecycleControls(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	dealer, err := ctx.DealerSocket()
	if err != nil {
		t.Fatalf("DealerSocket() error = %v", err)
	}
	defer dealer.Close()

	discovery, err := ctx.Discovery(zlink.ServiceTypeSocket, "attach-discovery")
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

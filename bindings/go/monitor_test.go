package zlink_test

import (
	"testing"
	"time"

	"zlink"
)

func TestMonitorRecv(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	endpoint := tcpEndpoint(t)
	server, _ := ctx.PairSocket()
	client, _ := ctx.PairSocket()
	defer server.Close()
	defer client.Close()

	serverMon, err := zlink.OpenSocketMonitor(server, zlink.MonitorEventAll)
	if err != nil {
		t.Fatalf("OpenSocketMonitor(server) error = %v", err)
	}
	defer serverMon.Close()

	clientMon, err := zlink.OpenSocketMonitor(client, zlink.MonitorEventAll)
	if err != nil {
		t.Fatalf("OpenSocketMonitor(client) error = %v", err)
	}
	defer clientMon.Close()

	if err := server.Bind(endpoint); err != nil {
		t.Fatalf("Bind() error = %v", err)
	}
	if err := client.Connect(endpoint); err != nil {
		t.Fatalf("Connect() error = %v", err)
	}

	event := waitForMonitorEvent(t, serverMon, 5*time.Second)
	if !event.IsListening() && !event.IsConnectionReady() && !event.IsAccepted() {
		t.Fatalf("unexpected monitor event: %+v", event)
	}

	snapshot, err := serverMon.Snapshot()
	if err != nil {
		t.Fatalf("Snapshot() error = %v", err)
	}
	if snapshot == nil {
		t.Fatalf("Snapshot() returned nil")
	}
}

func TestMonitorOnEventReceivesStateChange(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	endpoint := tcpEndpoint(t)
	server, _ := ctx.PairSocket()
	client, _ := ctx.PairSocket()
	defer server.Close()
	defer client.Close()

	serverMon, err := zlink.OpenSocketMonitor(server, zlink.MonitorEventAll)
	if err != nil {
		t.Fatalf("OpenSocketMonitor(server) error = %v", err)
	}
	defer serverMon.Close()

	events := make(chan *zlink.MonitorEvent, 4)
	if err := serverMon.OnEvent(func(event *zlink.MonitorEvent) {
		events <- event
	}); err != nil {
		t.Fatalf("OnEvent() error = %v", err)
	}

	if err := server.Bind(endpoint); err != nil {
		t.Fatalf("Bind() error = %v", err)
	}
	if err := client.Connect(endpoint); err != nil {
		t.Fatalf("Connect() error = %v", err)
	}

	var event *zlink.MonitorEvent
	select {
	case event = <-events:
	case <-time.After(5 * time.Second):
		t.Fatalf("monitor callback did not receive an event within 5s")
	}

	if !event.IsListening() && !event.IsAccepted() && !event.IsConnectionReady() {
		t.Fatalf("unexpected monitor callback event: %+v", event)
	}
}

func TestServiceMonitorOnEventReceivesRegistryStateChange(t *testing.T) {
	ctx := newContext(t)
	defer ctx.Close()

	registry, err := ctx.Registry()
	if err != nil {
		t.Fatalf("Registry() error = %v", err)
	}
	defer registry.Close()

	discovery, err := ctx.Discovery(zlink.ServiceTypeSocket, "service-monitor-callback")
	if err != nil {
		t.Fatalf("Discovery() error = %v", err)
	}
	defer discovery.Close()

	pub, err := ctx.PubSocket()
	if err != nil {
		t.Fatalf("PubSocket() error = %v", err)
	}
	defer pub.Close()

	monitor, err := discovery.MonitorOpen(zlink.ServiceMonitorEventAll)
	if err != nil {
		t.Fatalf("MonitorOpen() error = %v", err)
	}
	defer monitor.Close()

	events := make(chan *zlink.ServiceMonitorEvent, 4)
	if err := monitor.OnEvent(func(event *zlink.ServiceMonitorEvent) {
		events <- event
	}); err != nil {
		t.Fatalf("OnEvent() error = %v", err)
	}

	registryPub := tcpEndpoint(t)
	registryRouter := tcpEndpoint(t)
	if err := registry.Bind(registryPub, registryRouter); err != nil {
		t.Fatalf("Bind() error = %v", err)
	}
	if err := discovery.ConnectRegistry(registryRouter); err != nil {
		t.Fatalf("ConnectRegistry() error = %v", err)
	}
	if err := pub.AttachDiscovery(discovery); err != nil {
		t.Fatalf("AttachDiscovery() error = %v", err)
	}
	if err := pub.Bind(tcpEndpoint(t)); err != nil {
		t.Fatalf("pub.Bind() error = %v", err)
	}

	var event *zlink.ServiceMonitorEvent
	select {
	case event = <-events:
	case <-time.After(5 * time.Second):
		t.Fatalf("service monitor callback did not receive an event within 5s")
	}

	if event == nil {
		t.Fatalf("service monitor callback returned nil event")
	}
	mask := zlink.ServiceMonitorEventType(zlink.ServiceMonitorEventDiscoveryServiceUp | zlink.ServiceMonitorEventDiscoveryProvidersChanged)
	if event.EventType&mask == 0 {
		t.Fatalf("unexpected service monitor callback event: %+v", event)
	}
}

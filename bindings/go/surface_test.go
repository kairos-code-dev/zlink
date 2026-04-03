package zlink_test

import (
	"reflect"
	"testing"

	"zlink"
)

func hasMethod(target any, name string) bool {
	typ := reflect.TypeOf(target)
	_, ok := typ.MethodByName(name)
	return ok
}

func TestSurfaceCapabilities(t *testing.T) {
	if !hasMethod((*zlink.PairSocket)(nil), "Send") {
		t.Fatalf("PairSocket should expose Send")
	}
	if !hasMethod((*zlink.PairSocket)(nil), "TrySend") {
		t.Fatalf("PairSocket should expose TrySend")
	}
	if !hasMethod((*zlink.PairSocket)(nil), "Recv") {
		t.Fatalf("PairSocket should expose Recv")
	}
	if !hasMethod((*zlink.PairSocket)(nil), "TryRecv") {
		t.Fatalf("PairSocket should expose TryRecv")
	}
	if hasMethod((*zlink.PubSocket)(nil), "Recv") {
		t.Fatalf("PubSocket should not expose Recv")
	}
	if !hasMethod((*zlink.PubSocket)(nil), "Publish") {
		t.Fatalf("PubSocket should expose Publish")
	}
	if !hasMethod((*zlink.PubSocket)(nil), "TryPublish") {
		t.Fatalf("PubSocket should expose TryPublish")
	}
	if hasMethod((*zlink.SubSocket)(nil), "Send") {
		t.Fatalf("SubSocket should not expose Send")
	}
	if !hasMethod((*zlink.SubSocket)(nil), "Subscribe") {
		t.Fatalf("SubSocket should expose Subscribe")
	}
	if !hasMethod((*zlink.SubSocket)(nil), "TrySubscribe") {
		t.Fatalf("SubSocket should expose TrySubscribe")
	}
	if !hasMethod((*zlink.RouterSocket)(nil), "SendTo") {
		t.Fatalf("RouterSocket should expose SendTo")
	}
	if !hasMethod((*zlink.RouterSocket)(nil), "SetRoutingID") {
		t.Fatalf("RouterSocket should expose SetRoutingID")
	}
	if !hasMethod((*zlink.RouterSocket)(nil), "SetConnectRoutingID") {
		t.Fatalf("RouterSocket should expose SetConnectRoutingID")
	}
	if !hasMethod((*zlink.RouterSocket)(nil), "RoutingID") {
		t.Fatalf("RouterSocket should expose RoutingID")
	}
	if !hasMethod((*zlink.RouterSocket)(nil), "TrySendTo") {
		t.Fatalf("RouterSocket should expose TrySendTo")
	}
	if hasMethod((*zlink.RouterSocket)(nil), "Send") {
		t.Fatalf("RouterSocket should not expose Send")
	}
	if !hasMethod((*zlink.StreamSocket)(nil), "SetNotify") {
		t.Fatalf("StreamSocket should expose SetNotify")
	}
	if hasMethod((*zlink.StreamSocket)(nil), "Connect") {
		t.Fatalf("StreamSocket should not expose Connect")
	}
	if hasMethod((*zlink.StreamSocket)(nil), "Disconnect") {
		t.Fatalf("StreamSocket should not expose Disconnect")
	}
	if hasMethod((*zlink.PairSocket)(nil), "SetNotify") {
		t.Fatalf("PairSocket should not expose SetNotify")
	}
	if _, err := zlink.Has("inproc"); err != nil {
		t.Fatalf("Has() should be callable: %v", err)
	}
	if !hasMethod((*zlink.XPubSocket)(nil), "ReceiveSubscriptionEvent") {
		t.Fatalf("XPubSocket should expose ReceiveSubscriptionEvent")
	}
	if !hasMethod((*zlink.XPubSocket)(nil), "TryReceiveSubscriptionEvent") {
		t.Fatalf("XPubSocket should expose TryReceiveSubscriptionEvent")
	}
	if !hasMethod((*zlink.SpotNode)(nil), "Bind") {
		t.Fatalf("SpotNode should expose Bind")
	}
	if !hasMethod((*zlink.SpotNode)(nil), "ConnectPeer") {
		t.Fatalf("SpotNode should expose ConnectPeer")
	}
	if !hasMethod((*zlink.SpotNode)(nil), "Spot") {
		t.Fatalf("SpotNode should expose Spot")
	}
	if !hasMethod((*zlink.SpotNode)(nil), "StatusSnapshot") {
		t.Fatalf("SpotNode should expose StatusSnapshot")
	}
	if !hasMethod((*zlink.SpotNode)(nil), "AttachDiscovery") {
		t.Fatalf("SpotNode should expose AttachDiscovery")
	}
	if !hasMethod((*zlink.Spot)(nil), "Publish") {
		t.Fatalf("Spot should expose Publish")
	}
	if !hasMethod((*zlink.Spot)(nil), "TryPublish") {
		t.Fatalf("Spot should expose TryPublish")
	}
	if !hasMethod((*zlink.Spot)(nil), "Subscribe") {
		t.Fatalf("Spot should expose Subscribe")
	}
	if !hasMethod((*zlink.Spot)(nil), "TrySubscribe") {
		t.Fatalf("Spot should expose TrySubscribe")
	}
	if !hasMethod((*zlink.Spot)(nil), "OnSendReady") {
		t.Fatalf("Spot should expose OnSendReady")
	}
	if hasMethod((*zlink.PubSocket)(nil), "ReceiveSubscriptionEvent") {
		t.Fatalf("PubSocket should not expose ReceiveSubscriptionEvent")
	}
	if !hasMethod((*zlink.Context)(nil), "Registry") {
		t.Fatalf("Context should expose Registry")
	}
	if !hasMethod((*zlink.Context)(nil), "Discovery") {
		t.Fatalf("Context should expose Discovery")
	}
	if !hasMethod((*zlink.Context)(nil), "RegistryQueryClient") {
		t.Fatalf("Context should expose RegistryQueryClient")
	}
	if !hasMethod((*zlink.Registry)(nil), "TopologySnapshot") {
		t.Fatalf("Registry should expose TopologySnapshot")
	}
	if !hasMethod((*zlink.Registry)(nil), "StatusSnapshot") {
		t.Fatalf("Registry should expose StatusSnapshot")
	}
	if !hasMethod((*zlink.Discovery)(nil), "MemberPeers") {
		t.Fatalf("Discovery should expose MemberPeers")
	}
	if !hasMethod((*zlink.Discovery)(nil), "GetValue") {
		t.Fatalf("Discovery should expose GetValue")
	}
	if !hasMethod((*zlink.Discovery)(nil), "GetMetadata") {
		t.Fatalf("Discovery should expose GetMetadata")
	}
	if !hasMethod((*zlink.Discovery)(nil), "MonitorOpen") {
		t.Fatalf("Discovery should expose MonitorOpen")
	}
}

func TestSurfaceRemovesRawFlagsAndOptionBags(t *testing.T) {
	socketTypes := []any{
		(*zlink.PairSocket)(nil),
		(*zlink.PubSocket)(nil),
		(*zlink.SubSocket)(nil),
		(*zlink.DealerSocket)(nil),
		(*zlink.RouterSocket)(nil),
		(*zlink.XPubSocket)(nil),
		(*zlink.XSubSocket)(nil),
		(*zlink.StreamSocket)(nil),
	}

	for _, socketType := range socketTypes {
		if hasMethod(socketType, "SetOption") {
			t.Fatalf("%T should not expose SetOption", socketType)
		}
		if hasMethod(socketType, "GetOption") {
			t.Fatalf("%T should not expose GetOption", socketType)
		}
	}

	if !hasMethod((*zlink.SocketMonitor)(nil), "Recv") {
		t.Fatalf("SocketMonitor should expose Recv")
	}
	if !hasMethod((*zlink.SocketMonitor)(nil), "TryRecv") {
		t.Fatalf("SocketMonitor should expose TryRecv")
	}
	if hasMethod((*zlink.SocketMonitor)(nil), "RecvWithFlags") {
		t.Fatalf("SocketMonitor should not expose raw flag receive")
	}
}

func TestSurfaceTypedOptionMethods(t *testing.T) {
	if !hasMethod((*zlink.PairSocket)(nil), "SetSendHWM") {
		t.Fatalf("PairSocket should expose typed common options")
	}
	if !hasMethod((*zlink.PairSocket)(nil), "SetTLSServer") {
		t.Fatalf("PairSocket should expose TLS server options")
	}
	if !hasMethod((*zlink.PairSocket)(nil), "SetTLSClient") {
		t.Fatalf("PairSocket should expose TLS client options")
	}
	if !hasMethod((*zlink.PairSocket)(nil), "SetTCPKeepalive") {
		t.Fatalf("PairSocket should expose typed boolean options")
	}
	if !hasMethod((*zlink.Context)(nil), "SetMaxSockets") {
		t.Fatalf("Context should expose typed context options")
	}
	if !hasMethod((*zlink.Context)(nil), "Blocky") {
		t.Fatalf("Context should expose blocky getters")
	}
	if !hasMethod((*zlink.RouterSocket)(nil), "SetMandatory") {
		t.Fatalf("RouterSocket should expose router-specific options")
	}
	if !hasMethod((*zlink.DealerSocket)(nil), "SetProbe") {
		t.Fatalf("DealerSocket should expose dealer-specific options")
	}
	if !hasMethod((*zlink.PubSocket)(nil), "SetVerboser") {
		t.Fatalf("PubSocket should expose pub-specific options")
	}
	if !hasMethod((*zlink.PubSocket)(nil), "SetManual") {
		t.Fatalf("PubSocket should expose pub manual option")
	}
	if !hasMethod((*zlink.SubSocket)(nil), "TopicsCount") {
		t.Fatalf("SubSocket should expose sub-specific options")
	}
	if hasMethod((*zlink.PairSocket)(nil), "SetMandatory") {
		t.Fatalf("PairSocket should not expose router-specific options")
	}
	if !hasMethod((*zlink.StreamSocket)(nil), "Notify") {
		t.Fatalf("StreamSocket should expose stream-specific options")
	}
	if !hasMethod((*zlink.StreamSocket)(nil), "SetRoutingID") {
		t.Fatalf("StreamSocket should expose routing id setter")
	}
	if !hasMethod((*zlink.StreamSocket)(nil), "RoutingID") {
		t.Fatalf("StreamSocket should expose routing id getter")
	}
	if hasMethod((*zlink.SubSocket)(nil), "Notify") {
		t.Fatalf("SubSocket should not expose stream-specific options")
	}
	if !hasMethod((*zlink.SubSocket)(nil), "SubscriptionAt") {
		t.Fatalf("SubSocket should expose subscription snapshots")
	}
	if !hasMethod((*zlink.XSubSocket)(nil), "SubscriptionAt") {
		t.Fatalf("XSubSocket should expose subscription snapshots")
	}
}

func TestSurfaceCallbackCapabilities(t *testing.T) {
	if !hasMethod((*zlink.PairSocket)(nil), "OnReceive") {
		t.Fatalf("PairSocket should expose OnReceive")
	}
	if !hasMethod((*zlink.PairSocket)(nil), "OnSendReady") {
		t.Fatalf("PairSocket should expose OnSendReady")
	}
	if hasMethod((*zlink.PairSocket)(nil), "OnSubscribe") {
		t.Fatalf("PairSocket should not expose OnSubscribe")
	}

	if !hasMethod((*zlink.DealerSocket)(nil), "OnReceive") {
		t.Fatalf("DealerSocket should expose OnReceive")
	}
	if !hasMethod((*zlink.DealerSocket)(nil), "OnSendReady") {
		t.Fatalf("DealerSocket should expose OnSendReady")
	}
	if hasMethod((*zlink.DealerSocket)(nil), "OnSubscribe") {
		t.Fatalf("DealerSocket should not expose OnSubscribe")
	}

	if !hasMethod((*zlink.RouterSocket)(nil), "OnReceive") {
		t.Fatalf("RouterSocket should expose OnReceive")
	}
	if !hasMethod((*zlink.RouterSocket)(nil), "OnSendReady") {
		t.Fatalf("RouterSocket should expose OnSendReady")
	}
	if hasMethod((*zlink.RouterSocket)(nil), "OnSubscribe") {
		t.Fatalf("RouterSocket should not expose OnSubscribe")
	}

	if !hasMethod((*zlink.SubSocket)(nil), "OnSubscribe") {
		t.Fatalf("SubSocket should expose OnSubscribe")
	}
	if hasMethod((*zlink.SubSocket)(nil), "OnSendReady") {
		t.Fatalf("SubSocket should not expose OnSendReady")
	}
	if hasMethod((*zlink.SubSocket)(nil), "OnReceive") {
		t.Fatalf("SubSocket should not expose OnReceive")
	}

	if !hasMethod((*zlink.XPubSocket)(nil), "OnSendReady") {
		t.Fatalf("XPubSocket should expose OnSendReady")
	}
	if hasMethod((*zlink.XPubSocket)(nil), "OnReceive") {
		t.Fatalf("XPubSocket should not expose OnReceive")
	}

	if !hasMethod((*zlink.XSubSocket)(nil), "OnSubscribe") {
		t.Fatalf("XSubSocket should expose OnSubscribe")
	}
	if hasMethod((*zlink.XSubSocket)(nil), "OnSendReady") {
		t.Fatalf("XSubSocket should not expose OnSendReady")
	}
	if hasMethod((*zlink.XSubSocket)(nil), "OnReceive") {
		t.Fatalf("XSubSocket should not expose OnReceive")
	}

	if !hasMethod((*zlink.StreamSocket)(nil), "OnReceive") {
		t.Fatalf("StreamSocket should expose OnReceive")
	}
	if !hasMethod((*zlink.StreamSocket)(nil), "OnSendReady") {
		t.Fatalf("StreamSocket should expose OnSendReady")
	}
	if hasMethod((*zlink.StreamSocket)(nil), "OnSubscribe") {
		t.Fatalf("StreamSocket should not expose OnSubscribe")
	}

	if !hasMethod((*zlink.Spot)(nil), "OnSubscribe") {
		t.Fatalf("Spot should expose OnSubscribe")
	}
	if !hasMethod((*zlink.Spot)(nil), "OnSendReady") {
		t.Fatalf("Spot should expose OnSendReady")
	}
	if hasMethod((*zlink.Spot)(nil), "OnReceive") {
		t.Fatalf("Spot should not expose OnReceive")
	}
}

func TestSurfaceSpotDoesNotExposeDirectMessaging(t *testing.T) {
	if hasMethod((*zlink.Spot)(nil), "Recv") {
		t.Fatalf("Spot should not expose Recv")
	}
	if hasMethod((*zlink.Spot)(nil), "TryRecv") {
		t.Fatalf("Spot should not expose TryRecv")
	}
	if hasMethod((*zlink.Spot)(nil), "Send") {
		t.Fatalf("Spot should not expose Send")
	}
	if hasMethod((*zlink.Spot)(nil), "TrySend") {
		t.Fatalf("Spot should not expose TrySend")
	}
	if hasMethod((*zlink.Spot)(nil), "SubscriptionAt") {
		t.Fatalf("Spot should not expose SubscriptionAt")
	}
	if hasMethod((*zlink.XPubSocket)(nil), "OnSubscribe") {
		t.Fatalf("XPubSocket should not expose OnSubscribe")
	}
	if hasMethod((*zlink.PubSocket)(nil), "TopicsCount") {
		t.Fatalf("PubSocket should not expose TopicsCount")
	}
	if hasMethod((*zlink.XPubSocket)(nil), "TopicsCount") {
		t.Fatalf("XPubSocket should not expose TopicsCount")
	}
	if hasMethod((*zlink.Discovery)(nil), "Value") {
		t.Fatalf("Discovery should not expose Value")
	}
	if hasMethod((*zlink.Discovery)(nil), "Metadata") {
		t.Fatalf("Discovery should not expose Metadata")
	}
	if hasMethod((*zlink.Registry)(nil), "Status") {
		t.Fatalf("Registry should not expose Status")
	}
	if hasMethod((*zlink.SpotNode)(nil), "Status") {
		t.Fatalf("SpotNode should not expose Status")
	}
}

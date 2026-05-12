package zlink_test

import (
	"reflect"
	"testing"

	"zlink.systems/zlink"
)

var (
	_ zlink.SocketTarget = (*zlink.PairSocket)(nil)
	_ zlink.SocketTarget = (*zlink.PubSocket)(nil)
	_ zlink.SocketTarget = (*zlink.SubSocket)(nil)
	_ zlink.SocketTarget = (*zlink.DealerSocket)(nil)
	_ zlink.SocketTarget = (*zlink.RouterSocket)(nil)
	_ zlink.SocketTarget = (*zlink.XPubSocket)(nil)
	_ zlink.SocketTarget = (*zlink.XSubSocket)(nil)
	_ zlink.SocketTarget = (*zlink.StreamSocket)(nil)
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
	if !hasMethod((*zlink.PairSocket)(nil), "Recv") {
		t.Fatalf("PairSocket should expose Recv")
	}
	if !hasMethod((*zlink.PairSocket)(nil), "TrySend") {
		t.Fatalf("PairSocket should expose TrySend")
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
	if hasMethod((*zlink.PubSocket)(nil), "TryPublish") {
		t.Fatalf("PubSocket should not expose TryPublish")
	}
	if hasMethod((*zlink.SubSocket)(nil), "Send") {
		t.Fatalf("SubSocket should not expose Send")
	}
	if !hasMethod((*zlink.SubSocket)(nil), "Subscribe") {
		t.Fatalf("SubSocket should expose Subscribe")
	}
	if hasMethod((*zlink.SubSocket)(nil), "TrySubscribe") {
		t.Fatalf("SubSocket should not expose TrySubscribe")
	}
	if !hasMethod((*zlink.RouterSocket)(nil), "SendTo") {
		t.Fatalf("RouterSocket should expose SendTo")
	}
	if !hasMethod((*zlink.DealerSocket)(nil), "TryRequestCallback") {
		t.Fatalf("DealerSocket should expose TryRequestCallback")
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
	if !hasMethod((*zlink.RouterSocket)(nil), "TryRequestCallback") {
		t.Fatalf("RouterSocket should expose TryRequestCallback")
	}
	if hasMethod((*zlink.RouterSocket)(nil), "Send") {
		t.Fatalf("RouterSocket should not expose Send")
	}
	if !hasMethod((*zlink.StreamSocket)(nil), "SetNotify") {
		t.Fatalf("StreamSocket should expose SetNotify")
	}
	if !hasMethod((*zlink.StreamSocket)(nil), "OnPacket") {
		t.Fatalf("StreamSocket should expose OnPacket")
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
	if hasMethod((*zlink.XPubSocket)(nil), "TryReceiveSubscriptionEvent") {
		t.Fatalf("XPubSocket should not expose TryReceiveSubscriptionEvent")
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
	if !hasMethod((*zlink.SpotNode)(nil), "EntrySpot") {
		t.Fatalf("SpotNode should expose EntrySpot")
	}
	if !hasMethod((*zlink.SpotNode)(nil), "SpotLookup") {
		t.Fatalf("SpotNode should expose SpotLookup")
	}
	if !hasMethod((*zlink.SpotNode)(nil), "StatusSnapshot") {
		t.Fatalf("SpotNode should expose StatusSnapshot")
	}
	if !hasMethod((*zlink.SpotNode)(nil), "AttachDiscovery") {
		t.Fatalf("SpotNode should expose AttachDiscovery")
	}
	if !hasMethod((*zlink.SpotNode)(nil), "AttachChannelDealer") {
		t.Fatalf("SpotNode should expose AttachChannelDealer")
	}
	if !hasMethod((*zlink.SpotNode)(nil), "AttachChannelDealerManual") {
		t.Fatalf("SpotNode should expose AttachChannelDealerManual")
	}
	if !hasMethod((*zlink.SpotNode)(nil), "AttachPubIngress") {
		t.Fatalf("SpotNode should expose AttachPubIngress")
	}
	if !hasMethod((*zlink.SpotNode)(nil), "SetRoutingID") {
		t.Fatalf("SpotNode should expose SetRoutingID")
	}
	if !hasMethod((*zlink.SpotNode)(nil), "RoutingID") {
		t.Fatalf("SpotNode should expose RoutingID")
	}
	if !hasMethod((*zlink.SpotNode)(nil), "DisconnectPeerRID") {
		t.Fatalf("SpotNode should expose DisconnectPeerRID")
	}
	if !hasMethod((*zlink.Spot)(nil), "Publish") {
		t.Fatalf("Spot should expose Publish")
	}
	if !hasMethod((*zlink.Spot)(nil), "SendChannel") {
		t.Fatalf("Spot should expose SendChannel")
	}
	if !hasMethod((*zlink.Spot)(nil), "RequestChannel") {
		t.Fatalf("Spot should expose RequestChannel")
	}
	if !hasMethod((*zlink.Spot)(nil), "Subscribe") {
		t.Fatalf("Spot should expose Subscribe")
	}
	if !hasMethod((*zlink.Spot)(nil), "ReceiveSubscriptionEvent") {
		t.Fatalf("Spot should expose ReceiveSubscriptionEvent")
	}
	if hasMethod((*zlink.Spot)(nil), "TryPublish") {
		t.Fatalf("Spot should not expose TryPublish")
	}
	if hasMethod((*zlink.Spot)(nil), "TrySubscribe") {
		t.Fatalf("Spot should not expose TrySubscribe")
	}
	if !hasMethod((*zlink.Spot)(nil), "OnSendReady") {
		t.Fatalf("Spot should expose OnSendReady")
	}
	if !hasMethod((*zlink.Spot)(nil), "SetRoutingID") {
		t.Fatalf("Spot should expose SetRoutingID")
	}
	if !hasMethod((*zlink.Spot)(nil), "RoutingID") {
		t.Fatalf("Spot should expose RoutingID")
	}
	if hasMethod((*zlink.Spot)(nil), "SetTLSServer") {
		t.Fatalf("Spot should not expose SetTLSServer")
	}
	if hasMethod((*zlink.Spot)(nil), "SetTLSClient") {
		t.Fatalf("Spot should not expose SetTLSClient")
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
	if !hasMethod((*zlink.Context)(nil), "Options") {
		t.Fatalf("Context should expose ContextOptions facade")
	}
	if hasMethod((*zlink.Context)(nil), "SetThreadNamePrefix") {
		t.Fatalf("Context should not expose SetThreadNamePrefix")
	}
	if hasMethod((*zlink.Context)(nil), "ThreadNamePrefix") {
		t.Fatalf("Context should not expose ThreadNamePrefix")
	}
	if hasMethod((*zlink.Context)(nil), "SetMaxSockets") {
		t.Fatalf("Context should not expose direct context option setters")
	}
	if hasMethod((*zlink.Context)(nil), "MaxSockets") {
		t.Fatalf("Context should not expose direct context option getters")
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
	if hasMethod((*zlink.Discovery)(nil), "GetMetadata") {
		t.Fatalf("Discovery should not expose GetMetadata")
	}
	if hasMethod((*zlink.Discovery)(nil), "BindRoute") {
		t.Fatalf("Discovery should not expose BindRoute")
	}
	if hasMethod((*zlink.Discovery)(nil), "ResolveRoute") {
		t.Fatalf("Discovery should not expose ResolveRoute")
	}
	if !hasMethod((*zlink.Discovery)(nil), "SetSpotOwnerSyncEnabled") {
		t.Fatalf("Discovery should expose SetSpotOwnerSyncEnabled")
	}
	if !hasMethod((*zlink.Discovery)(nil), "SpotOwnerSyncEnabled") {
		t.Fatalf("Discovery should expose SpotOwnerSyncEnabled")
	}
	if !hasMethod((*zlink.Discovery)(nil), "SetActorRouteSyncEnabled") {
		t.Fatalf("Discovery should expose SetActorRouteSyncEnabled")
	}
	if !hasMethod((*zlink.Discovery)(nil), "ActorRouteSyncEnabled") {
		t.Fatalf("Discovery should expose ActorRouteSyncEnabled")
	}
	if hasMethod((*zlink.Discovery)(nil), "MonitorOpen") {
		t.Fatalf("Discovery should not expose MonitorOpen")
	}
	if !hasMethod((*zlink.Discovery)(nil), "ResolveSpot") {
		t.Fatalf("Discovery should expose ResolveSpot")
	}
	if hasMethod((*zlink.Discovery)(nil), "SetDealerPeerMode") {
		t.Fatalf("Discovery should not expose SetDealerPeerMode")
	}
	if !hasMethod((*zlink.Registry)(nil), "SetTLSServer") {
		t.Fatalf("Registry should expose SetTLSServer")
	}
	if !hasMethod((*zlink.Registry)(nil), "SetTLSClient") {
		t.Fatalf("Registry should expose SetTLSClient")
	}
	if !hasMethod((*zlink.Discovery)(nil), "SetTLSClient") {
		t.Fatalf("Discovery should expose SetTLSClient")
	}
	if !hasMethod((*zlink.SpotNode)(nil), "SetTLSServer") {
		t.Fatalf("SpotNode should expose SetTLSServer")
	}
	if !hasMethod((*zlink.SpotNode)(nil), "SetTLSClient") {
		t.Fatalf("SpotNode should expose SetTLSClient")
	}
	if !hasMethod((*zlink.Message)(nil), "GetProperty") {
		t.Fatalf("Message should expose GetProperty")
	}
	if !hasMethod((*zlink.Message)(nil), "RefCount") {
		t.Fatalf("Message should expose RefCount")
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
	if hasMethod((*zlink.SocketMonitor)(nil), "TryRecv") {
		t.Fatalf("SocketMonitor should not expose TryRecv")
	}
	if hasMethod((*zlink.SocketMonitor)(nil), "RecvWithFlags") {
		t.Fatalf("SocketMonitor should not expose raw flag receive")
	}
	if zlink.IgnoreMonitorHandler == nil {
		t.Fatalf("IgnoreMonitorHandler should be defined")
	}
}

func TestSurfaceActorCapabilities(t *testing.T) {
	if !hasMethod((*zlink.SpotNode)(nil), "Actor") {
		t.Fatalf("SpotNode should expose Actor")
	}
	if !hasMethod((*zlink.SpotNode)(nil), "ActorLookup") {
		t.Fatalf("SpotNode should expose ActorLookup")
	}
	if !hasMethod((*zlink.SpotNode)(nil), "CreateRemoteActor") {
		t.Fatalf("SpotNode should expose CreateRemoteActor")
	}
	if !hasMethod((*zlink.SpotNode)(nil), "DestroyRemoteActor") {
		t.Fatalf("SpotNode should expose DestroyRemoteActor")
	}
	if !hasMethod((*zlink.SpotNode)(nil), "OnActorAdmission") {
		t.Fatalf("SpotNode should expose OnActorAdmission")
	}
	if !hasMethod((*zlink.SpotNode)(nil), "JoinActor") {
		t.Fatalf("SpotNode should expose JoinActor")
	}
	if !hasMethod((*zlink.SpotNode)(nil), "LeaveActor") {
		t.Fatalf("SpotNode should expose LeaveActor")
	}
	if !hasMethod((*zlink.SpotNode)(nil), "SpotsSnapshot") {
		t.Fatalf("SpotNode should expose SpotsSnapshot")
	}
	if !hasMethod((*zlink.SpotNode)(nil), "ActorsSnapshot") {
		t.Fatalf("SpotNode should expose ActorsSnapshot")
	}

	if !hasMethod((*zlink.Actor)(nil), "Ref") {
		t.Fatalf("Actor should expose Ref")
	}
	if !hasMethod((*zlink.Actor)(nil), "Join") {
		t.Fatalf("Actor should expose Join")
	}
	if !hasMethod((*zlink.Actor)(nil), "Leave") {
		t.Fatalf("Actor should expose Leave")
	}
	if !hasMethod((*zlink.Actor)(nil), "RecvPart") {
		t.Fatalf("Actor should expose RecvPart")
	}
	if !hasMethod((*zlink.Actor)(nil), "SendBoundSession") {
		t.Fatalf("Actor should expose SendBoundSession")
	}
	if !hasMethod((*zlink.Actor)(nil), "CloseWithTimeout") {
		t.Fatalf("Actor should expose CloseWithTimeout")
	}
	if !hasMethod((*zlink.Actor)(nil), "Close") {
		t.Fatalf("Actor should expose Close")
	}

	if !hasMethod((*zlink.Spot)(nil), "RecvActorJoin") {
		t.Fatalf("Spot should expose RecvActorJoin")
	}
	if !hasMethod((*zlink.Spot)(nil), "ReplyActorJoin") {
		t.Fatalf("Spot should expose ReplyActorJoin")
	}
	if !hasMethod((*zlink.Spot)(nil), "ActorsSnapshot") {
		t.Fatalf("Spot should expose ActorsSnapshot")
	}

	if !hasMethod((*zlink.StreamSocket)(nil), "BindActor") {
		t.Fatalf("StreamSocket should expose BindActor")
	}
	if !hasMethod((*zlink.StreamSocket)(nil), "UnbindActor") {
		t.Fatalf("StreamSocket should expose UnbindActor")
	}
	if !hasMethod((*zlink.StreamSocket)(nil), "SendBoundActor") {
		t.Fatalf("StreamSocket should expose SendBoundActor")
	}

	if !hasMethod((*zlink.Discovery)(nil), "ResolveActor") {
		t.Fatalf("Discovery should expose ResolveActor")
	}

	remote, err := zlink.RemoteActorRef(zlink.NewRoutingID([]byte("node")), "actor")
	if err != nil {
		t.Fatalf("RemoteActorRef should create unchecked refs: %v", err)
	}
	if !remote.IsUnchecked() {
		t.Fatalf("RemoteActorRef should return unchecked refs")
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
	if !hasMethod((*zlink.ContextOptions)(nil), "SetMaxSockets") {
		t.Fatalf("ContextOptions should expose typed context options")
	}
	if !hasMethod((*zlink.ContextOptions)(nil), "Blocky") {
		t.Fatalf("ContextOptions should expose blocky getters")
	}
	if !hasMethod((*zlink.ContextOptions)(nil), "SetThreadSchedulingPolicy") {
		t.Fatalf("ContextOptions should expose thread scheduling policy setter")
	}
	if !hasMethod((*zlink.ContextOptions)(nil), "ThreadSchedulingPolicy") {
		t.Fatalf("ContextOptions should expose thread scheduling policy getter")
	}
	if !hasMethod((*zlink.ContextOptions)(nil), "AddThreadAffinity") {
		t.Fatalf("ContextOptions should expose thread affinity add")
	}
	if !hasMethod((*zlink.ContextOptions)(nil), "RemoveThreadAffinity") {
		t.Fatalf("ContextOptions should expose thread affinity remove")
	}
	if !hasMethod((*zlink.RouterSocket)(nil), "SetMandatory") {
		t.Fatalf("RouterSocket should expose router-specific options")
	}
	if !hasMethod((*zlink.PairSocket)(nil), "DisconnectRID") {
		t.Fatalf("PairSocket should expose disconnect by routing id")
	}
	if !hasMethod((*zlink.RouterSocket)(nil), "SetRidDuplicatePolicy") {
		t.Fatalf("RouterSocket should expose rid duplicate policy")
	}
	if !hasMethod((*zlink.RouterSocket)(nil), "CommonOptions") {
		t.Fatalf("RouterSocket should expose CommonOptions")
	}
	if !hasMethod((*zlink.DealerSocket)(nil), "CommonOptions") {
		t.Fatalf("DealerSocket should expose CommonOptions")
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
	if hasMethod((*zlink.PairSocket)(nil), "OnReceive") {
		t.Fatalf("PairSocket should not expose OnReceive")
	}
	if !hasMethod((*zlink.PairSocket)(nil), "OnSendReady") {
		t.Fatalf("PairSocket should expose OnSendReady")
	}
	if hasMethod((*zlink.PairSocket)(nil), "OnSubscribe") {
		t.Fatalf("PairSocket should not expose OnSubscribe")
	}

	if hasMethod((*zlink.DealerSocket)(nil), "OnReceive") {
		t.Fatalf("DealerSocket should not expose OnReceive")
	}
	if !hasMethod((*zlink.DealerSocket)(nil), "OnSendReady") {
		t.Fatalf("DealerSocket should expose OnSendReady")
	}
	if !hasMethod((*zlink.DealerSocket)(nil), "Request") {
		t.Fatalf("DealerSocket should expose Request")
	}
	if !hasMethod((*zlink.DealerSocket)(nil), "RequestCallback") {
		t.Fatalf("DealerSocket should expose RequestCallback")
	}
	if hasMethod((*zlink.DealerSocket)(nil), "OnSubscribe") {
		t.Fatalf("DealerSocket should not expose OnSubscribe")
	}

	if hasMethod((*zlink.RouterSocket)(nil), "OnReceive") {
		t.Fatalf("RouterSocket should not expose OnReceive")
	}
	if !hasMethod((*zlink.RouterSocket)(nil), "OnSendReady") {
		t.Fatalf("RouterSocket should expose OnSendReady")
	}
	if hasMethod((*zlink.RouterSocket)(nil), "RecvSpot") {
		t.Fatalf("RouterSocket should not expose RecvSpot")
	}
	if hasMethod((*zlink.RouterSocket)(nil), "OnSpotReceive") {
		t.Fatalf("RouterSocket should not expose OnSpotReceive")
	}
	if !hasMethod((*zlink.RouterSocket)(nil), "SendToSpot") {
		t.Fatalf("RouterSocket should expose SendToSpot")
	}
	if !hasMethod((*zlink.RouterSocket)(nil), "Request") {
		t.Fatalf("RouterSocket should expose Request")
	}
	if !hasMethod((*zlink.RouterSocket)(nil), "RequestCallback") {
		t.Fatalf("RouterSocket should expose RequestCallback")
	}
	if !hasMethod((*zlink.RouterSocket)(nil), "Reply") {
		t.Fatalf("RouterSocket should expose Reply")
	}
	if !hasMethod((*zlink.RouterSocket)(nil), "RequestToSpot") {
		t.Fatalf("RouterSocket should expose RequestToSpot")
	}
	if !hasMethod((*zlink.RouterSocket)(nil), "TryRequestToSpot") {
		t.Fatalf("RouterSocket should expose TryRequestToSpot")
	}
	if !hasMethod((*zlink.RouterSocket)(nil), "ReplyToSpot") {
		t.Fatalf("RouterSocket should expose ReplyToSpot")
	}
	if hasMethod((*zlink.RouterSocket)(nil), "OnSubscribe") {
		t.Fatalf("RouterSocket should not expose OnSubscribe")
	}

	if hasMethod((*zlink.SubSocket)(nil), "OnSubscribe") {
		t.Fatalf("SubSocket should not expose OnSubscribe")
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

	if hasMethod((*zlink.XSubSocket)(nil), "OnSubscribe") {
		t.Fatalf("XSubSocket should not expose OnSubscribe")
	}
	if hasMethod((*zlink.XSubSocket)(nil), "OnSendReady") {
		t.Fatalf("XSubSocket should not expose OnSendReady")
	}
	if hasMethod((*zlink.XSubSocket)(nil), "OnReceive") {
		t.Fatalf("XSubSocket should not expose OnReceive")
	}

	if hasMethod((*zlink.StreamSocket)(nil), "OnReceive") {
		t.Fatalf("StreamSocket should not expose OnReceive")
	}
	if !hasMethod((*zlink.StreamSocket)(nil), "OnPacket") {
		t.Fatalf("StreamSocket should expose OnPacket")
	}
	if !hasMethod((*zlink.StreamSocket)(nil), "OnSendReady") {
		t.Fatalf("StreamSocket should expose OnSendReady")
	}
	if hasMethod((*zlink.StreamSocket)(nil), "OnSubscribe") {
		t.Fatalf("StreamSocket should not expose OnSubscribe")
	}

	if hasMethod((*zlink.Spot)(nil), "OnSubscribe") {
		t.Fatalf("Spot should not expose OnSubscribe")
	}
	if !hasMethod((*zlink.Spot)(nil), "OnSendReady") {
		t.Fatalf("Spot should expose OnSendReady")
	}
	if !hasMethod((*zlink.Spot)(nil), "RecvRouted") {
		t.Fatalf("Spot should expose RecvRouted")
	}
	if !hasMethod((*zlink.Spot)(nil), "OnRoutedReceive") {
		t.Fatalf("Spot should expose OnRoutedReceive")
	}
	if !hasMethod((*zlink.Spot)(nil), "OnDispatchEvent") {
		t.Fatalf("Spot should expose OnDispatchEvent")
	}
	if !hasMethod((*zlink.Spot)(nil), "DrainChannelReplyFrom") {
		t.Fatalf("Spot should expose DrainChannelReplyFrom")
	}
	if !hasMethod((*zlink.Spot)(nil), "ReceiveSubscriptionEvent") {
		t.Fatalf("Spot should expose ReceiveSubscriptionEvent")
	}
	if !hasMethod((*zlink.Spot)(nil), "SendToSpot") {
		t.Fatalf("Spot should expose SendToSpot")
	}
	if !hasMethod((*zlink.Spot)(nil), "ReplyToSpot") {
		t.Fatalf("Spot should expose ReplyToSpot")
	}
	if !hasMethod((*zlink.Spot)(nil), "ReplyToRouter") {
		t.Fatalf("Spot should expose ReplyToRouter")
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
	if !hasMethod((*zlink.DealerSocket)(nil), "SetChannelName") {
		t.Fatalf("DealerSocket should expose SetChannelName")
	}
	if !hasMethod((*zlink.DealerSocket)(nil), "ChannelName") {
		t.Fatalf("DealerSocket should expose ChannelName")
	}
	if !hasMethod((*zlink.Timer)(nil), "Start") || !hasMethod((*zlink.Timer)(nil), "Recv") || !hasMethod((*zlink.Timer)(nil), "OnFire") {
		t.Fatalf("Timer should expose canonical timer methods")
	}
	if !hasMethod((*zlink.Poller)(nil), "Wait") || !hasMethod((*zlink.Poller)(nil), "WaitMany") || !hasMethod((*zlink.Poller)(nil), "AddTimer") {
		t.Fatalf("Poller should expose canonical poller methods")
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

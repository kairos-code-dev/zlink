package main

import (
	"fmt"
	"time"
	"zlink"
	"zlink/samples/internal/samplecommon"
)

func main() {
	ctx, err := zlink.NewContext()
	samplecommon.Must(err)
	defer ctx.Close()

	gatewayNode, err := ctx.SpotNode()
	samplecommon.Must(err)
	defer gatewayNode.Close()
	playNode, err := ctx.SpotNode()
	samplecommon.Must(err)
	defer playNode.Close()
	playSpot, err := playNode.Spot()
	samplecommon.Must(err)
	defer playSpot.Close()

	samplecommon.Must(playNode.OnActorAdmission(func(actorID string, message *zlink.Message) zlink.ActorAdmissionResult {
		if actorID != "play-session-actor" || string(message.Data()) != "spawn" {
			return zlink.ActorAdmissionReject
		}
		return zlink.ActorAdmissionAccept
	}))
	created, err := gatewayNode.CreateRemoteActor(
		mustRID(playNode.RoutingID()),
		"play-session-actor",
		samplecommon.Message("spawn"),
		time.Second,
	)
	samplecommon.Must(err)
	if created.Status != zlink.ActorCreateCreated {
		samplecommon.Must(fmt.Errorf("unexpected create status %v", created.Status))
	}

	payloadCh := make(chan string, 1)
	samplecommon.Must(playSpot.OnDispatchEvent(func(_ *zlink.Spot, info zlink.SpotDispatchInfo) {
		if info.Event != zlink.SpotDispatchEventActorReadable {
			return
		}
		part, err := info.RecvActorPart(zlink.RecvFlagsDontWait)
		if err == nil && part != nil {
			payloadCh <- string(part.Message.Data())
			part.Message.Close()
		}
	}))

	actorRef := created.Actor
	stream, err := ctx.StreamSocket()
	samplecommon.Must(err)
	defer stream.Close()
	session := zlink.NewRoutingID([]byte("gateway-session"))
	samplecommon.Must(stream.BindActor(gatewayNode, session, actorRef, time.Second))

	joinCh := make(chan zlink.RequestResult, 1)
	playSpotRID := mustRID(playSpot.RoutingID())
	playNodeRID := mustRID(playNode.RoutingID())
	_, joinErr := gatewayNode.JoinActor(actorRef, playNodeRID, playSpotRID, samplecommon.Message("join-play"), func(result zlink.RequestResult, parts []*zlink.Message) {
		for _, part := range parts {
			part.Close()
		}
		joinCh <- result
	}, zlink.SendFlagsDontWait, time.Second)
	samplecommon.Must(joinErr)
	request, err := playSpot.RecvActorJoin(zlink.RecvFlagsDontWait)
	samplecommon.Must(err)
	if string(request.Message.Data()) != "join-play" {
		samplecommon.Must(fmt.Errorf("unexpected join payload"))
	}
	request.Message.Close()
	_, replyErr := playSpot.ReplyActorJoin(request, zlink.ActorAdmissionAccept, zlink.SendFlagsNone, samplecommon.Message("accepted"))
	samplecommon.Must(replyErr)
	if result := <-joinCh; result != zlink.RequestOK {
		samplecommon.Must(fmt.Errorf("unexpected join result %v", result))
	}

	_, sendErr := stream.SendBoundActor(gatewayNode, session, samplecommon.Message("client-input"), zlink.SendFlagsDontWait)
	samplecommon.Must(sendErr)
	select {
	case payload := <-payloadCh:
		if payload != "client-input" {
			samplecommon.Must(fmt.Errorf("unexpected actor payload %q", payload))
		}
	case <-time.After(2 * time.Second):
		samplecommon.Must(fmt.Errorf("timed out waiting for actor payload"))
	}
	samplecommon.Must(gatewayNode.LeaveActor(actorRef, playSpotRID, time.Second))
	samplecommon.Must(gatewayNode.DestroyRemoteActor(actorRef, time.Second))
}

func mustRID(rid zlink.RoutingID, err error) zlink.RoutingID {
	samplecommon.Must(err)
	return rid
}

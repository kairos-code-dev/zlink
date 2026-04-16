package main

import (
	"bytes"
	"fmt"
	"time"
	"zlink"
	"zlink/samples/internal/samplecommon"
)

func main() {
	ctx, err := zlink.NewContext()
	samplecommon.Must(err)
	defer func() { samplecommon.MustStep("ctx.Close", ctx.Close()) }()

	requesterNode, err := ctx.SpotNode()
	samplecommon.MustStep("requesterNode", err)
	defer func() { samplecommon.MustStep("requesterNode.Close", requesterNode.Close()) }()
	responderNode, err := ctx.SpotNode()
	samplecommon.MustStep("responderNode", err)
	defer func() { samplecommon.MustStep("responderNode.Close", responderNode.Close()) }()

	requester, err := requesterNode.Spot()
	samplecommon.MustStep("requester", err)
	defer func() { samplecommon.MustStep("requester.Close", requester.Close()) }()
	responder, err := responderNode.Spot()
	samplecommon.MustStep("responder", err)
	defer func() { samplecommon.MustStep("responder.Close", responder.Close()) }()

	const serviceName = "sample"
	const requestPayload = "spot-ping"
	const replyPayload = "spot-pong"
	_ = serviceName
	samplecommon.MustStep(
		"requesterNode.SetRoutingID",
		requesterNode.SetRoutingID(zlink.NewRoutingID([]byte("spot-requester-node"))),
	)
	samplecommon.MustStep(
		"responderNode.SetRoutingID",
		responderNode.SetRoutingID(zlink.NewRoutingID([]byte("spot-responder-node"))),
	)
	samplecommon.MustStep(
		"requester.SetRoutingID",
		requester.SetRoutingID(zlink.NewRoutingID([]byte("spot-requester"))),
	)
	samplecommon.MustStep(
		"responder.SetRoutingID",
		responder.SetRoutingID(zlink.NewRoutingID([]byte("spot-responder"))),
	)

	endpoint := samplecommon.UniqueTCP("spot-request-async")
	samplecommon.MustStep("responderNode.Bind", responderNode.Bind(endpoint))
	samplecommon.MustStep("requesterNode.ConnectPeer", requesterNode.ConnectPeer(endpoint))
	samplecommon.WaitSpotPeerConnected(requesterNode, 5*time.Second)
	samplecommon.WaitUntil(5*time.Second, "spot peer metadata", func() bool {
		peers, err := requesterNode.PeersSnapshot()
		if err != nil || len(peers) == 0 {
			return false
		}
		for _, peer := range peers {
			if peer.State == zlink.SpotPeerStateConnected {
				return true
			}
		}
		return false
	})
	responderNodeRID, err := responderNode.RoutingID()
	samplecommon.MustStep("responderNode.RoutingID", err)
	responderSpotRID, err := responder.RoutingID()
	samplecommon.MustStep("responder.RoutingID", err)

	serverDone := make(chan error, 1)
	samplecommon.MustStep("responder.OnRoutedReceive", responder.OnRoutedReceive(
		func(sourceNodeRID zlink.RoutingID, sourceSpotRID zlink.RoutingID, requestSeq uint64, parts []*zlink.Message) {
			if len(parts) != 1 || !bytes.Equal(parts[0].Data(), []byte(requestPayload)) {
				serverDone <- fmt.Errorf("unexpected spot request payload")
				return
			}
			serverDone <- responder.ReplyToSpot(
				sourceNodeRID,
				sourceSpotRID,
				requestSeq,
				zlink.SendFlagsNone,
				samplecommon.Message(replyPayload),
			)
		},
	))

	type result struct {
		requestResult zlink.RequestResult
		replyParts    []*zlink.Message
	}
	replyCh := make(chan result, 1)
	samplecommon.MustStep(
		"requester.RequestToSpot",
		requester.RequestToSpot(
			responderNodeRID,
			responderSpotRID,
			func(requestResult zlink.RequestResult, replyParts []*zlink.Message) {
				replyCh <- result{requestResult: requestResult, replyParts: replyParts}
			},
			zlink.SendFlagsNone,
			5*time.Second,
			samplecommon.Message(requestPayload),
		),
	)

	out := <-replyCh
	if out.requestResult != zlink.RequestOK {
		samplecommon.Must(fmt.Errorf("unexpected request result %v", out.requestResult))
	}
	defer func() {
		for _, part := range out.replyParts {
			part.Close()
		}
	}()
	if len(out.replyParts) != 1 {
		samplecommon.Must(fmt.Errorf("unexpected reply part count %d", len(out.replyParts)))
	}
	if !bytes.Equal(out.replyParts[0].Data(), []byte(replyPayload)) {
		samplecommon.Must(fmt.Errorf("unexpected spot reply %q", string(out.replyParts[0].Data())))
	}
	samplecommon.Must(<-serverDone)

	fmt.Printf("[spot/request/async] request: %q -> reply: %q\n", requestPayload, replyPayload)
}

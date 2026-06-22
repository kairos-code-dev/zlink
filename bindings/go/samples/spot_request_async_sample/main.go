package main

import (
	"bytes"
	"fmt"
	"time"
	zlink "zlink.systems/zlink"
	"zlink.systems/zlink/samples/internal/samplecommon"
)

func main() {
	ctx, err := zlink.NewContext()
	samplecommon.Must(err)
	defer func() { samplecommon.MustStep("ctx.Close", ctx.Close()) }()

	requesterNode, err := ctx.SpotNode()
	samplecommon.MustStep("requesterNode", err)
	defer func() { samplecommon.MustStep("requesterNode.Close", requesterNode.Close()) }()
	requesterDealer, err := ctx.DealerSocket()
	samplecommon.MustStep("requesterDealer", err)
	defer func() { samplecommon.MustStep("requesterDealer.Close", requesterDealer.Close()) }()
	responderRouter, err := ctx.RouterSocket()
	samplecommon.MustStep("responderRouter", err)
	defer func() { samplecommon.MustStep("responderRouter.Close", responderRouter.Close()) }()
	bridge, err := requesterNode.CreateRouteBridge(nil)
	samplecommon.MustStep("requesterNode.CreateRouteBridge", err)
	defer func() { samplecommon.MustStep("bridge.Close", bridge.Close()) }()

	const channelName = "orders"
	const requestPayload = "spot-ping"
	const replyPayload = "spot-pong"
	endpoint := samplecommon.UniqueTCP("spot-request-async")
	samplecommon.MustStep("responderRouter.Bind", responderRouter.Bind(endpoint))
	samplecommon.MustStep("requesterDealer.Connect", requesterDealer.Connect(endpoint))
	samplecommon.MustStep(
		"bridge.AttachDealerChannel",
		bridge.AttachDealerChannel(channelName, requesterDealer, nil),
	)

	serverDone := make(chan error, 1)
	go func() {
		var received zlink.Received
		_, err := responderRouter.Recv(&received, zlink.RecvFlagsNone)
		if err != nil {
			serverDone <- err
			return
		}
		defer received.Close()
		if !received.HasRequestSeq() {
			serverDone <- fmt.Errorf("missing request sequence")
			return
		}
		parts := received.Parts()
		if len(parts) < 3 || !bytes.Equal(parts[2].Data(), []byte(requestPayload)) {
			serverDone <- fmt.Errorf("unexpected spot request payload")
			return
		}
		replyErr := responderRouter.Reply(
			received.RoutingID(),
			received.RequestSeq(),
		).Message(samplecommon.Message(replyPayload)).Submit(nil)
		serverDone <- replyErr
	}()

	type result struct {
		requestResult zlink.RequestResult
		replyParts    []*zlink.Message
	}
	replyCh := make(chan result, 1)
	_, reqErr := bridge.Request(channelName, zlink.NewRoutingID([]byte("requester-spot")), zlink.SendFlagsNone, 5*time.Second, func(requestResult zlink.RequestResult, replyParts []*zlink.Message) {
		replyCh <- result{requestResult: requestResult, replyParts: replyParts}
	}, samplecommon.Message(requestPayload))
	samplecommon.MustStep("bridge.Request", reqErr)

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

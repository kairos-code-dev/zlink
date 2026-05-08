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
	requester, err := requesterNode.Spot()
	samplecommon.MustStep("requester", err)
	defer func() { samplecommon.MustStep("requester.Close", requester.Close()) }()
	requesterDealer, err := ctx.DealerSocket()
	samplecommon.MustStep("requesterDealer", err)
	defer func() { samplecommon.MustStep("requesterDealer.Close", requesterDealer.Close()) }()
	responderRouter, err := ctx.RouterSocket()
	samplecommon.MustStep("responderRouter", err)
	defer func() { samplecommon.MustStep("responderRouter.Close", responderRouter.Close()) }()

	const channelName = "orders"
	const requestPayload = "spot-ping"
	const replyPayload = "spot-pong"
	endpoint := samplecommon.UniqueTCP("spot-request-async")
	samplecommon.MustStep("responderRouter.Bind", responderRouter.Bind(endpoint))
	samplecommon.MustStep("requesterDealer.Connect", requesterDealer.Connect(endpoint))
	samplecommon.MustStep(
		"requesterNode.AttachChannelDealerManual",
		requesterNode.AttachChannelDealerManual(channelName, requesterDealer),
	)

	serverDone := make(chan error, 1)
	go func() {
		received, err := responderRouter.Recv(zlink.RecvFlagsNone)
		if err != nil {
			serverDone <- err
			return
		}
		defer received.Close()
		part, err := received.SinglePartOrError()
		if err != nil {
			serverDone <- err
			return
		}
		if !received.HasRequestSeq() {
			serverDone <- fmt.Errorf("missing request sequence")
			return
		}
		if len(received.Parts()) != 1 || !bytes.Equal(part.Data(), []byte(requestPayload)) {
			serverDone <- fmt.Errorf("unexpected spot request payload")
			return
		}
		_, replyErr := responderRouter.Reply(
			received.RoutingID(),
			received.RequestSeq(),
			zlink.SendFlagsNone,
			samplecommon.Message(replyPayload),
		)
		serverDone <- replyErr
	}()

	type result struct {
		requestResult zlink.RequestResult
		replyParts    []*zlink.Message
	}
	replyCh := make(chan result, 1)
	_, reqErr := requester.RequestChannel(channelName).Message(samplecommon.Message(requestPayload)).Timeout(5*time.Second).SubmitCallback(nil, func(requestResult zlink.RequestResult, replyParts []*zlink.Message) {
		replyCh <- result{requestResult: requestResult, replyParts: replyParts}
	})
	samplecommon.MustStep("requester.RequestChannel", reqErr)

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

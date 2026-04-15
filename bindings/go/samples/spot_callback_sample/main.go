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
	defer ctx.Close()

	publisherNode, err := ctx.SpotNode()
	samplecommon.MustStep("publisherNode", err)
	defer publisherNode.Close()
	subscriberNode, err := ctx.SpotNode()
	samplecommon.MustStep("subscriberNode", err)
	defer subscriberNode.Close()

	publisher, err := publisherNode.Spot()
	samplecommon.MustStep("publisher", err)
	defer publisher.Close()
	subscriber, err := subscriberNode.Spot()
	samplecommon.MustStep("subscriber", err)
	defer subscriber.Close()

	topic := "room:lobby"
	serviceName := "bench"
	payload := "hello-spot"

	endpoint := samplecommon.UniqueTCP("spot-callback")
	samplecommon.MustStep("publisherNode.Bind", publisherNode.Bind(endpoint))
	samplecommon.MustStep("subscriberNode.ConnectPeer", subscriberNode.ConnectPeer(endpoint))
	samplecommon.MustStep("subscriber.SetSubscription", subscriber.SetSubscription(topic))
	samplecommon.WaitSpotPeerConnected(subscriberNode, 5*time.Second)

	samplecommon.MustStep("publisher.Publish", publisher.Publish(serviceName, topic, zlink.SendFlagsNone, samplecommon.Message(payload)))

	message, err := subscriber.Subscribe(zlink.RecvFlagsNone)
	samplecommon.MustStep("subscriber.Subscribe", err)
	defer message.Close()
	part, err := message.SinglePartOrError()
	samplecommon.MustStep("message.SinglePartOrError", err)
	got := message.Topic() + "/" + string(part.Data())
	want := topic + "/" + payload
	if !bytes.Equal([]byte(got), []byte(want)) {
		samplecommon.Must(fmt.Errorf("unexpected callback payload %q", got))
	}

	fmt.Printf("[spot/callback] publish: %q -> subscribe: %q\n", want, got)
}

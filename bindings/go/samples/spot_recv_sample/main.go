package main

import (
	"bytes"
	"fmt"
	"runtime"
	"time"
	"zlink"
	"zlink/samples/internal/samplecommon"
)

func main() {
	ctx, err := zlink.NewContext()
	samplecommon.MustStep("NewContext", err)
	defer func() { samplecommon.MustStep("ctx.Close", ctx.Close()) }()

	publisherNode, err := ctx.SpotNode()
	samplecommon.MustStep("publisherNode", err)
	defer func() { samplecommon.MustStep("publisherNode.Close", publisherNode.Close()) }()
	subscriberNode, err := ctx.SpotNode()
	samplecommon.MustStep("subscriberNode", err)
	defer func() { samplecommon.MustStep("subscriberNode.Close", subscriberNode.Close()) }()

	publisher, err := publisherNode.Spot()
	samplecommon.MustStep("publisher", err)
	defer func() { samplecommon.MustStep("publisher.Close", publisher.Close()) }()
	subscriber, err := subscriberNode.Spot()
	samplecommon.MustStep("subscriber", err)
	defer func() { samplecommon.MustStep("subscriber.Close", subscriber.Close()) }()

	topic := "room:lobby"
	payload := "hello-spot"
	endpoint := samplecommon.UniqueTCP("spot-recv")
	samplecommon.MustStep("publisherNode.Bind", publisherNode.Bind(endpoint))
	samplecommon.MustStep("subscriberNode.ConnectPeer", subscriberNode.ConnectPeer(endpoint))
	samplecommon.MustStep("subscriber.SetSubscription", subscriber.SetSubscription(topic))
	samplecommon.WaitSpotPeerConnected(subscriberNode, 5*time.Second)

	var message *zlink.TopicMessage
	deadline := time.Now().Add(5 * time.Second)
	for time.Now().Before(deadline) {
		samplecommon.MustStep("publisher.Publish", publisher.Publish(topic, zlink.SendFlagsNone, samplecommon.Message(payload)))
		received, err := subscriber.Subscribe(zlink.RecvFlagsDontWait)
		if err != nil {
			runtime.Gosched()
			continue
		}
		if received != nil {
			message = received
			break
		}
		runtime.Gosched()
	}
	if message == nil {
		samplecommon.Must(fmt.Errorf("spot delivery did not arrive within 5s"))
	}
	defer func() { samplecommon.MustStep("message.Close", message.Close()) }()
	part, err := message.SinglePartOrError()
	samplecommon.MustStep("message.SinglePartOrError", err)
	if message.Topic() != topic || !bytes.Equal(part.Data(), []byte(payload)) {
		samplecommon.Must(fmt.Errorf("unexpected spot payload %q/%q", message.Topic(), string(part.Data())))
	}

	fmt.Printf("[spot/recv] publish: %q -> subscribe: %q\n", topic+"/"+payload, message.Topic()+"/"+string(part.Data()))
}

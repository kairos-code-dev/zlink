package main

import (
	"bytes"
	"fmt"
	"time"
	zlink "zlink.systems/zlink/contracts"
	"zlink.systems/zlink/samples/internal/samplecommon"
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

	topic := "room:lobby"
	channelName := "direct"
	payload := "hello-spot"
	publisherEndpoint := samplecommon.UniqueTCP("spot-recv-pub")
	subscriberEndpoint := samplecommon.UniqueTCP("spot-recv-sub")
	samplecommon.MustStep("publisherNode.SetRoutingID", publisherNode.SetRoutingID(zlink.NewRoutingID([]byte("z-go-spot-recv-publisher"))))
	samplecommon.MustStep("subscriberNode.SetRoutingID", subscriberNode.SetRoutingID(zlink.NewRoutingID([]byte("a-go-spot-recv-subscriber"))))
	samplecommon.MustStep("publisherNode.SetPubBind", publisherNode.SetPubBind(publisherEndpoint))
	samplecommon.MustStep("subscriberNode.SetPubBind", subscriberNode.SetPubBind(subscriberEndpoint))
	samplecommon.MustStep("publisherNode.ConnectPeer", publisherNode.ConnectPeer(subscriberEndpoint))
	samplecommon.MustStep("subscriberNode.ConnectPeer", subscriberNode.ConnectPeer(publisherEndpoint))
	publisher, err := publisherNode.Spot()
	samplecommon.MustStep("publisher", err)
	defer func() { samplecommon.MustStep("publisher.Close", publisher.Close()) }()
	subscriber, err := subscriberNode.Spot()
	samplecommon.MustStep("subscriber", err)
	defer func() { samplecommon.MustStep("subscriber.Close", subscriber.Close()) }()
	samplecommon.MustStep("publisher.SetRoutingID", publisher.SetRoutingID(zlink.NewRoutingID([]byte("z-go-spot-recv-publisher-spot"))))
	samplecommon.MustStep("subscriber.SetRoutingID", subscriber.SetRoutingID(zlink.NewRoutingID([]byte("a-go-spot-recv-subscriber-spot"))))
	samplecommon.MustStep("subscriber.SetSubscription", subscriber.SetSubscription(topic))
	samplecommon.WaitSpotPeerConnected(publisherNode, 15*time.Second)
	samplecommon.WaitSpotPeerConnected(subscriberNode, 15*time.Second)

	var message zlink.TopicMessage
	messageReceived := false
	deadline := time.Now().Add(5 * time.Second)
	for time.Now().Before(deadline) {
		_, _ = publisherNode.Status()
		_, _ = subscriberNode.Status()
		_, pubErr := publisher.Publish(topic).Message(samplecommon.Message(payload)).Submit(nil)
		if pubErr != nil {
			time.Sleep(10 * time.Millisecond)
			continue
		}
		received, err := subscriber.Subscribe(&message, zlink.RecvFlagsDontWait)
		if err != nil {
			time.Sleep(10 * time.Millisecond)
			continue
		}
		if received {
			messageReceived = true
			break
		}
		time.Sleep(10 * time.Millisecond)
	}
	if !messageReceived {
		samplecommon.Must(fmt.Errorf("spot delivery did not arrive within 5s"))
	}
	defer func() { samplecommon.MustStep("message.Close", message.Close()) }()
	part, err := message.SinglePartOrError()
	samplecommon.MustStep("message.SinglePartOrError", err)
	if message.Topic() != topic || !bytes.Equal(part.Data(), []byte(payload)) {
		samplecommon.Must(fmt.Errorf("unexpected spot payload %q/%q", message.Topic(), string(part.Data())))
	}

	fmt.Printf("[spot/recv] channel: %q tick: 1 publish: %q -> recv: %q\n", channelName, topic+"/"+payload, message.Topic()+"/"+string(part.Data()))
}

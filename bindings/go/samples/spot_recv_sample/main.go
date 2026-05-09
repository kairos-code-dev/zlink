package main

import (
	"bytes"
	"fmt"
	"time"
	"zlink.systems/zlink"
	"zlink.systems/zlink/samples/internal/samplecommon"
)

func main() {
	ctx, err := zlink.NewContext()
	samplecommon.MustStep("NewContext", err)
	defer func() { samplecommon.MustStep("ctx.Close", ctx.Close()) }()

	registry, err := ctx.Registry()
	samplecommon.MustStep("registry", err)
	defer func() { samplecommon.MustStep("registry.Close", registry.Close()) }()
	publisherDiscovery, err := ctx.Discovery(zlink.AutoConnectSpotMesh, "sample")
	samplecommon.MustStep("publisherDiscovery", err)
	defer func() { samplecommon.MustStep("publisherDiscovery.Close", publisherDiscovery.Close()) }()
	subscriberDiscovery, err := ctx.Discovery(zlink.AutoConnectSpotMesh, "sample")
	samplecommon.MustStep("subscriberDiscovery", err)
	defer func() { samplecommon.MustStep("subscriberDiscovery.Close", subscriberDiscovery.Close()) }()
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
	channelName := "sample"
	payload := "hello-spot"
	publisherEndpoint := samplecommon.UniqueTCP("spot-recv-pub")
	subscriberEndpoint := samplecommon.UniqueTCP("spot-recv-sub")
	registryPub := samplecommon.UniqueTCP("spot-registry-pub")
	registryRouter := samplecommon.UniqueTCP("spot-registry-router")
	samplecommon.MustStep("publisherNode.SetRoutingID", publisherNode.SetRoutingID(zlink.NewRoutingID([]byte("z-go-spot-recv-publisher"))))
	samplecommon.MustStep("subscriberNode.SetRoutingID", subscriberNode.SetRoutingID(zlink.NewRoutingID([]byte("a-go-spot-recv-subscriber"))))
	samplecommon.MustStep("publisher.SetRoutingID", publisher.SetRoutingID(zlink.NewRoutingID([]byte("z-go-spot-recv-publisher-spot"))))
	samplecommon.MustStep("subscriber.SetRoutingID", subscriber.SetRoutingID(zlink.NewRoutingID([]byte("a-go-spot-recv-subscriber-spot"))))
	samplecommon.MustStep("registry.Bind", registry.Bind(registryPub, registryRouter))
	samplecommon.MustStep("registry.SetBroadcastInterval", registry.SetBroadcastInterval(50))
	samplecommon.MustStep("publisherDiscovery.ConnectRegistry", publisherDiscovery.ConnectRegistry(registryRouter))
	samplecommon.MustStep("subscriberDiscovery.ConnectRegistry", subscriberDiscovery.ConnectRegistry(registryRouter))
	samplecommon.MustStep("publisherNode.Bind", publisherNode.Bind(publisherEndpoint))
	samplecommon.MustStep("subscriberNode.Bind", subscriberNode.Bind(subscriberEndpoint))
	samplecommon.MustStep("publisherNode.AttachDiscovery", publisherNode.AttachDiscovery(publisherDiscovery))
	samplecommon.MustStep("subscriberNode.AttachDiscovery", subscriberNode.AttachDiscovery(subscriberDiscovery))
	samplecommon.MustStep("subscriber.SetSubscription", subscriber.SetSubscription(topic))
	samplecommon.WaitSpotPeerConnected(publisherNode, 5*time.Second)
	samplecommon.WaitSpotPeerConnected(subscriberNode, 5*time.Second)

	var message *zlink.TopicMessage
	deadline := time.Now().Add(5 * time.Second)
	for time.Now().Before(deadline) {
		_, pubErr := publisher.Publish(topic).Message(samplecommon.Message(payload)).Submit(nil)
		samplecommon.MustStep("publisher.Publish", pubErr)
		received, err := subscriber.Subscribe(zlink.RecvFlagsDontWait)
		if err != nil {
			continue
		}
		if received != nil {
			message = received
			break
		}
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

	fmt.Printf("[spot/recv] channel: %q tick: 1 publish: %q -> recv: %q\n", channelName, topic+"/"+payload, message.Topic()+"/"+string(part.Data()))
}

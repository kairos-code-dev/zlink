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
	samplecommon.MustStep("NewContext", err)
	defer func() { samplecommon.MustStep("ctx.Close", ctx.Close()) }()

	registry, err := ctx.Registry()
	samplecommon.MustStep("registry", err)
	defer func() { samplecommon.MustStep("registry.Close", registry.Close()) }()
	discovery, err := ctx.Discovery(zlink.ServiceTypeSpot, "sample")
	samplecommon.MustStep("discovery", err)
	defer func() { samplecommon.MustStep("discovery.Close", discovery.Close()) }()
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
	serviceName := "sample"
	payload := "hello-spot"
	publisherEndpoint := samplecommon.UniqueTCP("spot-recv-pub")
	subscriberEndpoint := samplecommon.UniqueTCP("spot-recv-sub")
	registryPub := samplecommon.UniqueTCP("spot-registry-pub")
	registryRouter := samplecommon.UniqueTCP("spot-registry-router")
	samplecommon.MustStep("registry.Bind", registry.Bind(registryPub, registryRouter))
	samplecommon.MustStep("discovery.ConnectRegistry", discovery.ConnectRegistry(registryRouter))
	samplecommon.MustStep("publisherNode.AttachDiscovery", publisherNode.AttachDiscovery(discovery))
	samplecommon.MustStep("subscriberNode.AttachDiscovery", subscriberNode.AttachDiscovery(discovery))
	samplecommon.MustStep("publisherNode.Bind", publisherNode.Bind(publisherEndpoint))
	samplecommon.MustStep("subscriberNode.Bind", subscriberNode.Bind(subscriberEndpoint))
	samplecommon.MustStep("subscriber.SetSubscription", subscriber.SetSubscription(topic))

	var message *zlink.TopicMessage
	deadline := time.Now().Add(5 * time.Second)
	for time.Now().Before(deadline) {
		samplecommon.MustStep("publisher.Publish", publisher.Publish(serviceName, topic, zlink.SendFlagsNone, samplecommon.Message(payload)))
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
	if message.ServiceName() != serviceName || message.Topic() != topic || !bytes.Equal(part.Data(), []byte(payload)) {
		samplecommon.Must(fmt.Errorf("unexpected spot payload %q/%q/%q", message.ServiceName(), message.Topic(), string(part.Data())))
	}

	fmt.Printf("[spot/recv] service: %q tick: 1 publish: %q -> recv: %q\n", serviceName, topic+"/"+payload, message.Topic()+"/"+string(part.Data()))
}

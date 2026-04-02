package main

import (
	"bytes"
	"fmt"
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

	pubMon, err := publisher.MonitorOpen(zlink.ServiceMonitorEventSpotFirstDeliveryReadyChanged)
	samplecommon.MustStep("pubMon", err)
	defer func() { samplecommon.MustStep("pubMon.Close", pubMon.Close()) }()
	subMon, err := subscriber.MonitorOpen(zlink.ServiceMonitorEventSpotFilterApplied | zlink.ServiceMonitorEventSpotSubscriptionReadyChanged)
	samplecommon.MustStep("subMon", err)
	defer func() { samplecommon.MustStep("subMon.Close", subMon.Close()) }()

	topic := "room:lobby"
	payload := "hello-spot"
	endpoint := samplecommon.UniqueTCP("spot-recv")
	samplecommon.MustStep("publisherNode.Bind", publisherNode.Bind(endpoint))
	samplecommon.MustStep("subscriberNode.ConnectPeer", subscriberNode.ConnectPeer(endpoint))
	samplecommon.MustStep("subscriber.SetSubscription", subscriber.SetSubscription(topic))

	filterEvent := samplecommon.WaitServiceEvent(subMon, zlink.ServiceMonitorEventSpotFilterApplied)
	if filterEvent.Subject != topic {
		samplecommon.Must(fmt.Errorf("unexpected filter subject %q", filterEvent.Subject))
	}
	readyEvent := samplecommon.WaitServiceEvent(subMon, zlink.ServiceMonitorEventSpotSubscriptionReadyChanged)
	if readyEvent.Endpoint != endpoint {
		samplecommon.Must(fmt.Errorf("unexpected subscription-ready endpoint %q", readyEvent.Endpoint))
	}
	pubSnapshot, err := pubMon.Snapshot()
	samplecommon.MustStep("pubMon.Snapshot", err)
	if !pubSnapshot.IsSendReady() {
		samplecommon.Must(fmt.Errorf("publisher spot monitor is not send-ready"))
	}

	samplecommon.MustStep("publisher.Publish", publisher.Publish(topic, samplecommon.Message(payload)))

	message, err := subscriber.Subscribe()
	samplecommon.MustStep("subscriber.Subscribe", err)
	defer func() { samplecommon.MustStep("message.Close", message.Close()) }()
	part, err := message.SinglePartOrError()
	samplecommon.MustStep("message.SinglePartOrError", err)
	if message.Topic() != topic || !bytes.Equal(part.Data(), []byte(payload)) {
		samplecommon.Must(fmt.Errorf("unexpected spot payload %q/%q", message.Topic(), string(part.Data())))
	}

	fmt.Printf("[spot/recv] publish: %q -> subscribe: %q\n", topic+"/"+payload, message.Topic()+"/"+string(part.Data()))
}

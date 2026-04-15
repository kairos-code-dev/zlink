package main

import (
	"fmt"
	"zlink"
	"zlink/samples/internal/samplecommon"
)

func main() {
	ctx, err := zlink.NewContext()
	samplecommon.Must(err)
	defer ctx.Close()

	publisher, err := ctx.XPubSocket()
	samplecommon.Must(err)
	defer publisher.Close()
	subscriber, err := ctx.SubSocket()
	samplecommon.Must(err)
	defer subscriber.Close()

	pubMon := samplecommon.OpenMonitor(publisher)
	defer pubMon.Close()
	subMon := samplecommon.OpenMonitor(subscriber)
	defer subMon.Close()

	endpoint := samplecommon.UniqueTCP("pubsub-callback")
	samplecommon.Must(publisher.Bind(endpoint))
	samplecommon.Must(subscriber.Connect(endpoint))
	samplecommon.WaitConnected(pubMon, subMon)

	topic := "prices"
	samplecommon.Must(subscriber.SetSubscription(topic))
	event, err := publisher.ReceiveSubscriptionEvent(zlink.RecvFlagsNone)
	samplecommon.Must(err)
	if event.Topic() != topic {
		samplecommon.Must(fmt.Errorf("unexpected subscription event topic %q", event.Topic()))
	}

	payload := "101.25"
	samplecommon.Must(publisher.Publish(topic, zlink.SendFlagsNone, samplecommon.Message(payload)))
	message, err := subscriber.Subscribe(zlink.RecvFlagsNone)
	samplecommon.Must(err)
	defer message.Close()
	part, err := message.SinglePartOrError()
	samplecommon.Must(err)
	got := message.Topic() + "/" + string(part.Data())

	fmt.Printf("[pubsub/callback] publish: %q -> subscribe: %q\n", topic+"/"+payload, got)
}

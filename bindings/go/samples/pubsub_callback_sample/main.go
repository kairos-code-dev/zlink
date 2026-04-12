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
	_, err = publisher.ReceiveSubscriptionEvent(zlink.RecvFlagsNone)
	samplecommon.Must(err)

	type result struct {
		value string
		err   error
	}
	delivered := make(chan result, 1)
	samplecommon.Must(subscriber.OnSubscribe(func(message *zlink.TopicMessage) {
		defer message.Close()
		part, err := message.SinglePartOrError()
		if err != nil {
			delivered <- result{err: err}
			return
		}
		delivered <- result{value: message.Topic() + "/" + string(part.Data())}
	}))

	payload := "101.25"
	samplecommon.Must(publisher.Publish(topic, zlink.SendFlagsNone, samplecommon.Message(payload)))
	out := <-delivered
	samplecommon.Must(out.err)
	got := out.value

	fmt.Printf("[pubsub/callback] publish: %q -> subscribe: %q\n", topic+"/"+payload, got)
}

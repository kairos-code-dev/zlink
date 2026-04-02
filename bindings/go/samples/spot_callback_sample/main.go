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

	pubMon, err := publisher.MonitorOpen(zlink.ServiceMonitorEventSpotFirstDeliveryReadyChanged)
	samplecommon.MustStep("pubMon", err)
	defer pubMon.Close()
	subMon, err := subscriber.MonitorOpen(zlink.ServiceMonitorEventSpotFilterApplied | zlink.ServiceMonitorEventSpotSubscriptionReadyChanged)
	samplecommon.MustStep("subMon", err)
	defer subMon.Close()

	topic := "room:lobby"
	payload := "hello-spot"
	type result struct {
		value string
		err   error
	}
	delivered := make(chan result, 1)
	samplecommon.MustStep("subscriber.OnSubscribe", subscriber.OnSubscribe(func(message *zlink.TopicMessage) {
		defer message.Close()
		part, err := message.SinglePartOrError()
		if err != nil {
			delivered <- result{err: err}
			return
		}
		select {
		case delivered <- result{value: message.Topic() + "/" + string(part.Data())}:
		default:
		}
	}))

	endpoint := samplecommon.UniqueTCP("spot-callback")
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

	var out result
	select {
	case out = <-delivered:
	case <-time.After(5 * time.Second):
		samplecommon.Must(fmt.Errorf("spot callback sample timed out"))
	}
	samplecommon.Must(out.err)
	got := out.value

	want := topic + "/" + payload
	if !bytes.Equal([]byte(got), []byte(want)) {
		samplecommon.Must(fmt.Errorf("unexpected callback payload %q", got))
	}

	fmt.Printf("[spot/callback] publish: %q -> subscribe: %q\n", want, got)
}

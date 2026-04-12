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
	samplecommon.WaitSpotPeerConnected(subscriberNode, 5*time.Second)

	var got string
	deadline := time.Now().Add(5 * time.Second)
	for time.Now().Before(deadline) {
		samplecommon.MustStep("publisher.Publish", publisher.Publish(topic, zlink.SendFlagsNone, samplecommon.Message(payload)))
		select {
		case out := <-delivered:
			samplecommon.Must(out.err)
			got = out.value
			goto done
		default:
			runtime.Gosched()
		}
	}
	samplecommon.Must(fmt.Errorf("spot callback sample timed out"))

done:

	want := topic + "/" + payload
	if !bytes.Equal([]byte(got), []byte(want)) {
		samplecommon.Must(fmt.Errorf("unexpected callback payload %q", got))
	}

	fmt.Printf("[spot/callback] publish: %q -> subscribe: %q\n", want, got)
}

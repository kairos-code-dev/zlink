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
	spot, err := publisherNode.Spot()
	samplecommon.MustStep("spot", err)
	defer func() { samplecommon.MustStep("spot.Close", spot.Close()) }()

	topic := "room:lobby"
	serviceName := "sample"
	payload := "hello-spot"
	pubSock, err := ctx.PubSocket()
	samplecommon.MustStep("pubSock", err)
	defer func() { samplecommon.MustStep("pubSock.Close", pubSock.Close()) }()
	subSock, err := ctx.SubSocket()
	samplecommon.MustStep("subSock", err)
	defer func() { samplecommon.MustStep("subSock.Close", subSock.Close()) }()

	serviceEndpoint := samplecommon.UniqueTCP("spot-recv-service")
	samplecommon.MustStep("pubSock.Bind", pubSock.Bind(serviceEndpoint))
	samplecommon.MustStep("subSock.Connect", subSock.Connect(serviceEndpoint))
	samplecommon.MustStep(
		"publisherNode.AttachPubSub",
		publisherNode.AttachPubSub(serviceName, pubSock, subSock),
	)
	samplecommon.MustStep("spot.SetSubscription", spot.SetSubscription(topic))

	var message *zlink.TopicMessage
	deadline := time.Now().Add(5 * time.Second)
	for time.Now().Before(deadline) {
		samplecommon.MustStep("spot.Publish", spot.Publish(serviceName, topic, zlink.SendFlagsNone, samplecommon.Message(payload)))
		received, err := spot.Subscribe(zlink.RecvFlagsDontWait)
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

	fmt.Printf("[spot/recv] service: %q tick: 1 publish: %q -> recv: %q\n", serviceName, topic+"/"+payload, message.Topic()+"/"+string(part.Data()))
}

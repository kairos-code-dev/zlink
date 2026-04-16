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
	var attachErr error
	samplecommon.WaitUntil(5*time.Second, "spot service attachment", func() bool {
		attachErr = publisherNode.AttachPubSub(serviceName, pubSock, subSock)
		return attachErr == nil
	})
	samplecommon.MustStep("publisherNode.AttachPubSub", attachErr)

	type recvResult struct {
		service string
		topic   string
		payload []byte
		err     error
	}
	delivered := make(chan recvResult, 1)
	samplecommon.MustStep("spot.OnDispatchEvent", spot.OnDispatchEvent(func(event zlink.SpotDispatchEvent) {
		if event != zlink.SpotDispatchEventSubscribeReadable {
			return
		}
		message, err := spot.Subscribe(zlink.RecvFlagsDontWait)
		if err != nil || message == nil {
			if err != nil {
				delivered <- recvResult{err: err}
			}
			return
		}
		defer message.Close()
		part, err := message.SinglePartOrError()
		if err != nil {
			delivered <- recvResult{err: err}
			return
		}
		delivered <- recvResult{
			service: message.ServiceName(),
			topic:   message.Topic(),
			payload: append([]byte(nil), part.Data()...),
		}
	}))
	samplecommon.MustStep("spot.SetSubscription", spot.SetSubscription(topic))

	var result recvResult
	deadline := time.Now().Add(5 * time.Second)
	for {
		samplecommon.MustStep("spot.Publish", spot.Publish(serviceName, topic, zlink.SendFlagsNone, samplecommon.Message(payload)))
		select {
		case result = <-delivered:
			goto delivered
		case <-time.After(50 * time.Millisecond):
			if time.Now().After(deadline) {
				samplecommon.Must(fmt.Errorf("spot dispatch callback did not deliver within 5s"))
			}
		}
	}

delivered:
	samplecommon.MustStep("dispatch callback result", result.err)
	if result.service != serviceName || result.topic != topic || !bytes.Equal(result.payload, []byte(payload)) {
		samplecommon.Must(fmt.Errorf("unexpected spot payload %q/%q/%q", result.service, result.topic, string(result.payload)))
	}

	fmt.Printf("[spot/recv] service: %q tick: 1 publish: %q -> recv: %q\n", serviceName, topic+"/"+payload, result.topic+"/"+string(result.payload))
}

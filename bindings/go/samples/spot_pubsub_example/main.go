// 자립형 가이드 예제: SPOT 토픽 pub/sub.
// 한 노드가 토픽에 publish하면, 그 토픽을 구독한 다른 노드가 받는다.
// 한 파일로 빌드·실행된다:  go run ./samples/spot_pubsub_example
package main

import (
	"fmt"
	"time"

	zlink "zlink.systems/zlink"
	"zlink.systems/zlink/samples/internal/samplecommon"
)

func main() {
	// --8<-- [start:doc]
	ctx, err := zlink.NewContext()
	samplecommon.Must(err)
	defer ctx.Close()

	// 토픽을 발행하는 노드와 구독하는 노드.
	publisherNode, err := ctx.SpotNode()
	samplecommon.Must(err)
	defer publisherNode.Close()
	subscriberNode, err := ctx.SpotNode()
	samplecommon.Must(err)
	defer subscriberNode.Close()

	topic := "room:lobby"
	pubEndpoint := samplecommon.UniqueTCP("spot-pubsub-pub")
	subEndpoint := samplecommon.UniqueTCP("spot-pubsub-sub")
	samplecommon.Must(publisherNode.SetPubBind(pubEndpoint))
	samplecommon.Must(subscriberNode.SetPubBind(subEndpoint))
	samplecommon.Must(publisherNode.ConnectPeer(subEndpoint))
	samplecommon.Must(subscriberNode.ConnectPeer(pubEndpoint))

	publisher, err := publisherNode.Spot()
	samplecommon.Must(err)
	defer publisher.Close()
	subscriber, err := subscriberNode.Spot()
	samplecommon.Must(err)
	defer subscriber.Close()
	// 구독자는 받을 토픽을 등록한다.
	samplecommon.Must(subscriber.SetSubscription(topic))
	samplecommon.WaitSpotPeerConnected(publisherNode, 15*time.Second)
	samplecommon.WaitSpotPeerConnected(subscriberNode, 15*time.Second)

	// 연결 직후 첫 publish가 구독자에게 닿기 전일 수 있어, 도착할 때까지 반복 발행한다.
	var received zlink.TopicMessage
	delivered := false
	deadline := time.Now().Add(5 * time.Second)
	for time.Now().Before(deadline) {
		if _, pubErr := publisher.Publish(topic).Message(samplecommon.Message("hello-everyone")).Submit(nil); pubErr != nil {
			time.Sleep(10 * time.Millisecond)
			continue
		}
		if ok, subErr := subscriber.Subscribe(&received, zlink.RecvFlagsDontWait); subErr == nil && ok {
			delivered = true
			break
		}
		time.Sleep(10 * time.Millisecond)
	}
	if !delivered {
		panic("spot delivery did not arrive")
	}
	defer received.Close()

	part, err := received.SinglePartOrError()
	samplecommon.Must(err)
	fmt.Printf("[spot/pubsub] topic %q -> recv: %q\n", received.Topic(), string(part.Data()))
	// --8<-- [end:doc]
}

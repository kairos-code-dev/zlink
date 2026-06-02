// 자립형 가이드 예제: SPOT 토픽 pub/sub.
// 한 노드가 토픽에 publish하면, 그 토픽을 구독한 다른 노드가 받는다.
// 한 파일로 빌드·실행된다:  go run ./samples/spot_pubsub_example
package main

import (
	"fmt"
	"net"
	"time"

	zlink "zlink.systems/zlink/contracts"
)

// --8<-- [start:doc]
func must(err error) {
	if err != nil {
		panic(err)
	}
}

func message(text string) *zlink.Message {
	msg, err := zlink.NewMessageString(text)
	must(err)
	return msg
}

// 빈 TCP 포트를 잡아 endpoint를 만든다.
func uniqueTCP() string {
	listener, err := net.Listen("tcp", "127.0.0.1:0")
	must(err)
	addr := listener.Addr().(*net.TCPAddr)
	_ = listener.Close()
	return fmt.Sprintf("tcp://127.0.0.1:%d", addr.Port)
}

func waitPeer(node *zlink.SpotNode) {
	deadline := time.Now().Add(15 * time.Second)
	for time.Now().Before(deadline) {
		status, err := node.Status()
		if err == nil && status != nil && status.ConnectedPeerCount > 0 {
			return
		}
		time.Sleep(10 * time.Millisecond)
	}
	panic("spot peer not connected")
}

func main() {
	ctx, err := zlink.NewContext()
	must(err)
	defer ctx.Close()

	// 토픽을 발행하는 노드와 구독하는 노드.
	publisherNode, err := ctx.SpotNode()
	must(err)
	defer publisherNode.Close()
	subscriberNode, err := ctx.SpotNode()
	must(err)
	defer subscriberNode.Close()

	topic := "room:lobby"
	pubEndpoint := uniqueTCP()
	subEndpoint := uniqueTCP()
	must(publisherNode.SetPubBind(pubEndpoint))
	must(subscriberNode.SetPubBind(subEndpoint))
	must(publisherNode.ConnectPeer(subEndpoint))
	must(subscriberNode.ConnectPeer(pubEndpoint))

	publisher, err := publisherNode.Spot()
	must(err)
	defer publisher.Close()
	subscriber, err := subscriberNode.Spot()
	must(err)
	defer subscriber.Close()
	// 구독자는 받을 토픽을 등록한다.
	must(subscriber.SetSubscription(topic))
	waitPeer(publisherNode)
	waitPeer(subscriberNode)

	// 연결 직후 첫 publish가 구독자에게 닿기 전일 수 있어, 도착할 때까지 반복 발행한다.
	var received zlink.TopicMessage
	delivered := false
	deadline := time.Now().Add(5 * time.Second)
	for time.Now().Before(deadline) {
		if _, pubErr := publisher.Publish(topic).Message(message("hello-everyone")).Submit(nil); pubErr != nil {
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
	must(err)
	fmt.Printf("[spot/pubsub] topic %q -> recv: %q\n", received.Topic(), string(part.Data()))
}
// --8<-- [end:doc]

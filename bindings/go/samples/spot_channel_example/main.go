// 자립형 가이드 예제: SPOT → 채널(DEALER→ROUTER) 요청.
// 게임룸(Spot)이 API 서버(채널 서비스)에 outgame 데이터를 요청한다.
// 채널 호출은 "어느 서비스 인스턴스든" 처리하는 로드밸런싱 경로다.
//   go run ./samples/spot_channel_example
package main

import (
	"fmt"
	"net"
	"time"

	zlink "zlink.systems/zlink/contracts"
)

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

func uniqueTCP() string {
	listener, err := net.Listen("tcp", "127.0.0.1:0")
	must(err)
	addr := listener.Addr().(*net.TCPAddr)
	_ = listener.Close()
	return fmt.Sprintf("tcp://127.0.0.1:%d", addr.Port)
}

func main() {
	ctx, err := zlink.NewContext()
	must(err)
	defer ctx.Close()

	// 게임룸(Spot) 쪽과, API 서버(raw ROUTER) 쪽.
	roomNode, err := ctx.SpotNode()
	must(err)
	defer roomNode.Close()
	room, err := roomNode.Spot()
	must(err)
	defer room.Close()
	roomDealer, err := ctx.DealerSocket()
	must(err)
	defer roomDealer.Close()
	apiRouter, err := ctx.RouterSocket()
	must(err)
	defer apiRouter.Close()

	const channel = "api"
	endpoint := uniqueTCP()
	must(apiRouter.Bind(endpoint))
	must(roomDealer.Connect(endpoint))
	// "api" 채널 호출을 이 DEALER로 내보내도록 노드에 등록한다.
	must(roomNode.AttachChannelDealerManual(channel, roomDealer))

	// API 서버: 요청을 받아 응답한다.
	serverDone := make(chan error, 1)
	go func() {
		var received zlink.Received
		if _, e := apiRouter.Recv(&received, zlink.RecvFlagsNone); e != nil {
			serverDone <- e
			return
		}
		defer received.Close()
		serverDone <- apiRouter.Reply(received.RoutingID(), received.RequestSeq()).
			Message(message("profile:level-7")).Submit(nil)
	}()

	// 게임룸이 API 채널로 outgame 요청을 보낸다.
	replyCh := make(chan string, 1)
	_, reqErr := room.RequestToChannel(channel).
		Message(message("get-profile")).
		Timeout(5 * time.Second).
		Submit(nil, func(result zlink.RequestResult, parts []*zlink.Message) {
			if result == zlink.RequestOK && len(parts) > 0 {
				replyCh <- string(parts[0].Data())
			}
			zlink.MultipartClose(parts)
		})
	must(reqErr)

	reply := <-replyCh
	must(<-serverDone)
	fmt.Printf("[spot/channel] request \"get-profile\" -> reply %q\n", reply)
}

// 자립형 가이드 예제: SPOT → 채널(ROUTER→ROUTER) 요청.
// 게임룸(Spot)이 API 서버(채널 서비스)에 outgame 데이터를 요청한다.
// 채널 호출은 "어느 서비스 인스턴스든" 처리하는 로드밸런싱 경로다.
//
//	go run ./samples/spot_channel_example
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

	// 게임룸(Spot) 쪽과, API 서버(raw ROUTER) 쪽.
	roomNode, err := ctx.SpotNode()
	samplecommon.Must(err)
	defer roomNode.Close()
	roomRouter, err := ctx.RouterSocket()
	samplecommon.Must(err)
	defer roomRouter.Close()
	apiRouter, err := ctx.RouterSocket()
	samplecommon.Must(err)
	defer apiRouter.Close()
	bridge, err := roomNode.CreateRouteBridge(nil)
	samplecommon.Must(err)
	defer bridge.Close()

	const channel = "api"
	endpoint := samplecommon.UniqueTCP("spot-channel")
	roomRouterRID := zlink.NewRoutingID([]byte("room-channel-client"))
	apiRouterRID := zlink.NewRoutingID([]byte("room-channel-server"))
	samplecommon.Must(roomRouter.SetRoutingID(roomRouterRID))
	samplecommon.Must(apiRouter.SetRoutingID(apiRouterRID))
	samplecommon.Must(apiRouter.Bind(endpoint))
	samplecommon.Must(roomRouter.Connect(endpoint))
	// "api" 채널 호출을 이 ROUTER로 내보내도록 bridge에 등록한다.
	samplecommon.Must(bridge.AttachRouterChannel(channel, roomRouter, nil))

	// API 서버: 요청을 받아 응답한다.
	serverDone := make(chan error, 1)
	go func() {
		var received zlink.Received
		if _, e := apiRouter.Recv(&received, zlink.RecvFlagsNone); e != nil {
			serverDone <- e
			return
		}
		defer received.Close()
		parts := received.Parts()
		if len(parts) < 3 || string(parts[2].Data()) != "get-profile" {
			serverDone <- fmt.Errorf("unexpected request frame")
			return
		}
		serverDone <- apiRouter.Reply(received.RoutingID(), received.RequestSeq()).
			Message(samplecommon.Message("profile:level-7")).Submit(nil)
	}()

	// 게임룸이 API 채널로 outgame 요청을 보낸다.
	replyCh := make(chan string, 1)
	_, reqErr := bridge.Request(channel, apiRouterRID, zlink.NewRoutingID([]byte("room")), zlink.SendFlagsNone, 5*time.Second, func(result zlink.RequestResult, parts []*zlink.Message) {
		if result == zlink.RequestOK && len(parts) > 0 {
			replyCh <- string(parts[0].Data())
		}
		zlink.MultipartClose(parts)
	}, samplecommon.Message("get-profile"))
	samplecommon.Must(reqErr)

	reply := <-replyCh
	samplecommon.Must(<-serverDone)
	fmt.Printf("[spot/channel] request \"get-profile\" -> reply %q\n", reply)
	// --8<-- [end:doc]
}

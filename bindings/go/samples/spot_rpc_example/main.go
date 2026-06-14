// 자립형 가이드 예제: SPOT 라우티드 RPC (Spot ↔ Spot 요청/응답).
// 한 노드의 Spot이 다른 노드의 Spot으로 직접 요청을 보내고, 상대가 응답한다.
// 한 파일로 빌드·실행된다:  go run ./samples/spot_rpc_example
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

	// 요청을 처리하는 서버 노드와 요청을 보내는 클라이언트 노드.
	serverNode, err := ctx.SpotNode()
	samplecommon.Must(err)
	defer serverNode.Close()
	clientNode, err := ctx.SpotNode()
	samplecommon.Must(err)
	defer clientNode.Close()

	server, err := serverNode.Spot()
	samplecommon.Must(err)
	defer server.Close()
	client, err := clientNode.Spot()
	samplecommon.Must(err)
	defer client.Close()

	// node·spot routing id를 먼저 부여한다 (라우트 발행 전 config 단계). 그 다음
	// pub bind, 마지막에 peer 연결 — 이 순서라야 spot owner route가 올바르게 퍼진다.
	samplecommon.Must(serverNode.SetRoutingID(zlink.NewRoutingIDString("rpc-server-node")))
	samplecommon.Must(clientNode.SetRoutingID(zlink.NewRoutingIDString("rpc-client-node")))
	samplecommon.Must(server.SetRoutingID(zlink.NewRoutingIDString("rpc-server-spot")))
	samplecommon.Must(client.SetRoutingID(zlink.NewRoutingIDString("rpc-client-spot")))

	// 라우티드(요청/응답) 평면은 ROUTER bind가 필요하다 — ALL mode는 router bind를
	// pub bind보다 먼저 호출한다.
	samplecommon.Must(serverNode.SetRouterBind(samplecommon.UniqueTCP("spot-rpc-server-router")))
	samplecommon.Must(clientNode.SetRouterBind(samplecommon.UniqueTCP("spot-rpc-client-router")))
	serverEndpoint := samplecommon.UniqueTCP("spot-rpc-server")
	clientEndpoint := samplecommon.UniqueTCP("spot-rpc-client")
	samplecommon.Must(serverNode.SetPubBind(serverEndpoint))
	samplecommon.Must(clientNode.SetPubBind(clientEndpoint))
	samplecommon.Must(serverNode.ConnectPeer(clientEndpoint))
	samplecommon.Must(clientNode.ConnectPeer(serverEndpoint))

	// 서버 Spot은 라우티드 요청을 받아 같은 평면으로 응답한다.
	samplecommon.Must(server.OnDispatchEvent(func(s *zlink.Spot, info zlink.SpotDispatchInfo) {
		if info.Event != zlink.SpotDispatchEventRoutedReadable {
			return
		}
		for {
			var received zlink.Received
			ok, recvErr := s.RecvRouted(&received, zlink.RecvFlagsDontWait)
			if recvErr != nil || !ok {
				return
			}
			_ = received.Reply().Message(samplecommon.Message("pong")).Submit(nil)
			_ = received.Close()
		}
	}))

	samplecommon.WaitSpotPeerConnected(serverNode, 15*time.Second)
	samplecommon.WaitSpotPeerConnected(clientNode, 15*time.Second)
	serverNodeRid, err := serverNode.RoutingID()
	samplecommon.Must(err)
	serverSpotRid, err := server.RoutingID()
	samplecommon.Must(err)

	// 클라이언트 Spot이 서버 Spot으로 요청한다. 요청 자체가 라우트 전파를
	// 내부에서 타임아웃까지 기다린다.
	replyCh := make(chan string, 1)
	_, reqErr := client.RequestToSpot(serverNodeRid, serverSpotRid).
		Message(samplecommon.Message("ping")).
		Timeout(3*time.Second).
		Submit(nil, func(result zlink.RequestResult, parts []*zlink.Message) {
			if result == zlink.RequestOK && len(parts) > 0 {
				replyCh <- string(parts[0].Data())
			}
			zlink.MultipartClose(parts)
		})
	samplecommon.Must(reqErr)

	select {
	case reply := <-replyCh:
		fmt.Printf("[spot/rpc] request \"ping\" -> reply %q\n", reply)
	case <-time.After(5 * time.Second):
		panic("spot rpc: no reply")
	}
	// --8<-- [end:doc]
}

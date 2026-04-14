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

	routerSocket, err := ctx.RouterSocket()
	samplecommon.Must(err)
	dealerSocket, err := ctx.DealerSocket()
	samplecommon.Must(err)

	routerMon := samplecommon.OpenMonitor(routerSocket)
	defer routerMon.Close()
	dealerMon := samplecommon.OpenMonitor(dealerSocket)
	defer dealerMon.Close()

	endpoint := samplecommon.UniqueTCP("request-reply-callback")
	rid := zlink.NewRoutingID([]byte("request-reply-client"))
	samplecommon.Must(routerSocket.Bind(endpoint))
	samplecommon.Must(dealerSocket.SetRoutingID(rid))
	samplecommon.Must(dealerSocket.Connect(endpoint))
	samplecommon.WaitConnected(routerMon, dealerMon)

	requestDone := make(chan error, 1)
	replyDone := make(chan error, 1)

	samplecommon.Must(routerSocket.OnReceive(func(received *zlink.Received) {
		defer received.Close()
		part, err := received.SinglePartOrError()
		if err != nil {
			requestDone <- err
			return
		}
		if received.RoutingID().String() != rid.String() {
			requestDone <- fmt.Errorf("unexpected routing id %q", received.RoutingID().String())
			return
		}
		if !bytes.Equal(part.Data(), []byte("ping")) {
			requestDone <- fmt.Errorf("unexpected request %q", string(part.Data()))
			return
		}
		requestSeq := received.RequestSeq()
		if !received.HasRequestSeq() {
			requestDone <- fmt.Errorf("missing request sequence")
			return
		}
		requestDone <- routerSocket.Reply(received.RoutingID(), requestSeq, zlink.SendFlagsNone, samplecommon.Message("pong"))
	}))

	samplecommon.Must(dealerSocket.RequestCallback([][]byte{[]byte("ping")}, func(result zlink.RequestResult, reply []*zlink.Message) {
		if result != zlink.RequestOK {
			replyDone <- fmt.Errorf("request failed: %d", result)
			return
		}
		if len(reply) != 1 {
			replyDone <- fmt.Errorf("unexpected reply part count %d", len(reply))
			return
		}
		if !bytes.Equal(reply[0].Data(), []byte("pong")) {
			replyDone <- fmt.Errorf("unexpected reply %q", string(reply[0].Data()))
			return
		}
		replyDone <- nil
	}, zlink.SendFlagsNone, 2*time.Second))

	samplecommon.Must(<-requestDone)
	samplecommon.Must(<-replyDone)
	fmt.Printf("[dealer-router/request-reply/callback] send: %q -> recv: %q\n", "ping", "pong")
}

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

	router := zlink.NewRequestRouter(routerSocket)
	defer router.Close()
	dealer := zlink.NewRequestDealer(dealerSocket)
	defer dealer.Close()

	endpoint := samplecommon.UniqueTCP("request-reply-callback")
	rid, err := zlink.NewRoutingID([]byte("request-reply-client"))
	samplecommon.Must(err)
	samplecommon.Must(routerSocket.Bind(endpoint))
	samplecommon.Must(dealerSocket.SetRoutingID(rid))
	samplecommon.Must(dealerSocket.Connect(endpoint))
	samplecommon.WaitConnected(routerMon, dealerMon)

	requestDone := make(chan error, 1)
	replyDone := make(chan error, 1)

	samplecommon.Must(router.OnReceive(func(received *zlink.Received) {
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
		requestSeq, ok := received.RequestSeq()
		if !ok {
			requestDone <- fmt.Errorf("missing request sequence")
			return
		}
		requestDone <- router.Reply(received.RoutingID(), requestSeq, zlink.SendFlagsNone, samplecommon.Message("pong"))
	}))

	samplecommon.Must(dealer.RequestCallback(func(result zlink.RequestResult, reply *zlink.Received) {
		if result != zlink.RequestOK {
			replyDone <- fmt.Errorf("request failed: %d", result)
			return
		}
		defer reply.Close()
		part, err := reply.SinglePartOrError()
		if err != nil {
			replyDone <- err
			return
		}
		if !bytes.Equal(part.Data(), []byte("pong")) {
			replyDone <- fmt.Errorf("unexpected reply %q", string(part.Data()))
			return
		}
		replyDone <- nil
	}, zlink.SendFlagsNone, 2*time.Second, samplecommon.Message("ping")))

	samplecommon.Must(<-requestDone)
	samplecommon.Must(<-replyDone)
	fmt.Printf("[dealer-router/request-reply/callback] send: %q -> recv: %q\n", "ping", "pong")
}

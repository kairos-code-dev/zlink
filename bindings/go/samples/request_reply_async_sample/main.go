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

	endpoint := samplecommon.UniqueTCP("request-reply-async")
	rid := zlink.NewRoutingID([]byte("request-reply-client"))
	samplecommon.Must(routerSocket.Bind(endpoint))
	samplecommon.Must(dealerSocket.SetRoutingID(rid))
	samplecommon.Must(dealerSocket.Connect(endpoint))
	samplecommon.WaitConnected(routerMon, dealerMon)

	requestDone := make(chan error, 1)
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

	replyCh := make(chan []*zlink.Message, 1)
	errCh := make(chan error, 1)
	samplecommon.Must(dealerSocket.RequestCallback([][]byte{[]byte("ping")}, func(result zlink.RequestResult, reply []*zlink.Message) {
		if result != zlink.RequestOK {
			errCh <- fmt.Errorf("request failed: %d", result)
			return
		}
		replyCh <- reply
	}, zlink.SendFlagsNone, 2*time.Second))

	var reply []*zlink.Message
	select {
	case err := <-errCh:
		samplecommon.Must(err)
	case reply = <-replyCh:
	}
	defer reply[0].Close()

	if len(reply) != 1 {
		samplecommon.Must(fmt.Errorf("unexpected reply part count %d", len(reply)))
	}
	if !bytes.Equal(reply[0].Data(), []byte("pong")) {
		samplecommon.Must(fmt.Errorf("unexpected reply %q", string(reply[0].Data())))
	}
	samplecommon.Must(<-requestDone)

	fmt.Printf("[dealer-router/request-reply/async] send: %q -> recv: %q\n", "ping", string(reply[0].Data()))
}

package main

import (
	"fmt"
	"zlink"
	"zlink/samples/internal/samplecommon"
)

func main() {
	ctx, err := zlink.NewContext()
	samplecommon.Must(err)
	defer ctx.Close()

	router, err := ctx.RouterSocket()
	samplecommon.Must(err)
	defer router.Close()
	dealer, err := ctx.DealerSocket()
	samplecommon.Must(err)
	defer dealer.Close()

	routerMon := samplecommon.OpenMonitor(router)
	defer routerMon.Close()
	dealerMon := samplecommon.OpenMonitor(dealer)
	defer dealerMon.Close()

	endpoint := samplecommon.UniqueTCP("dealer-router-callback")
	rid, err := zlink.NewRoutingID([]byte("dealer-sample"))
	samplecommon.Must(err)
	samplecommon.Must(router.Bind(endpoint))
	samplecommon.Must(dealer.SetRoutingID(rid))
	samplecommon.Must(dealer.Connect(endpoint))
	samplecommon.WaitConnected(routerMon, dealerMon)

	type result struct {
		payload string
		err     error
	}
	done := make(chan result, 1)
	samplecommon.Must(router.OnReceive(func(received *zlink.Received) {
		defer received.Close()
		if err := router.SendTo(received.RoutingID(), zlink.SendFlagsNone, samplecommon.Message("pong")); err != nil {
			done <- result{err: err}
			return
		}
		done <- result{payload: "pong"}
	}))

	samplecommon.Must(dealer.Send(zlink.SendFlagsNone, samplecommon.Message("ping")))
	reply, err := dealer.Recv(zlink.RecvFlagsNone)
	samplecommon.Must(err)
	defer reply.Close()
	out := <-done
	samplecommon.Must(out.err)
	part, err := reply.SinglePartOrError()
	samplecommon.Must(err)

	fmt.Printf("[dealer-router/callback] send: %q -> recv: %q\n", "ping", string(part.Data()))
}

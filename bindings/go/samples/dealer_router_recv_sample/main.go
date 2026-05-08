package main

import (
	"bytes"
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

	endpoint := samplecommon.UniqueTCP("dealer-router-recv")
	rid := zlink.NewRoutingID([]byte("dealer-sample"))
	samplecommon.Must(router.Bind(endpoint))
	samplecommon.Must(dealer.SetRoutingID(rid))
	samplecommon.Must(dealer.Connect(endpoint))
	samplecommon.WaitConnected(routerMon, dealerMon)

	_, err = dealer.Send(zlink.SendFlagsNone, samplecommon.Message("ping"))
	samplecommon.Must(err)

	request, err := router.Recv(zlink.RecvFlagsNone)
	samplecommon.Must(err)
	defer request.Close()
	_, err = router.SendTo(request.RoutingID(), zlink.SendFlagsNone, samplecommon.Message("pong"))
	samplecommon.Must(err)

	reply, err := dealer.Recv(zlink.RecvFlagsNone)
	samplecommon.Must(err)
	defer reply.Close()
	part, err := reply.SinglePartOrError()
	samplecommon.Must(err)
	if !bytes.Equal(part.Data(), []byte("pong")) {
		samplecommon.Must(fmt.Errorf("unexpected reply %q", string(part.Data())))
	}

	fmt.Printf("[dealer-router/recv] send: %q -> recv: %q\n", "ping", string(part.Data()))
}

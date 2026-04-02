package main

import (
	"time"

	"zlink"
	"zlink/perf/internal/perfcommon"
)

func runMultiDealerRouter(cfg multiConfig) perfcommon.Result {
	stats := perfcommon.NewStats()
	segmentWarmup := splitDuration(cfg.warmup, cfg.clients)
	segmentDuration := splitDuration(cfg.duration, cfg.clients)

	for i := 0; i < cfg.clients; i++ {
		ctx, err := zlink.NewContext()
		perfcommon.Must(err)

		router, err := ctx.RouterSocket()
		perfcommon.Must(err)
		dealer, err := ctx.DealerSocket()
		perfcommon.Must(err)
		routerMon := perfcommon.OpenMonitor(router)
		dealerMon := perfcommon.OpenMonitor(dealer)

		rid, err := zlink.NewRoutingID([]byte(time.Now().Add(time.Duration(i)).Format("150405.000000000")))
		perfcommon.Must(err)

		endpoint := perfcommon.UniqueTCPEndpoint("perf-multi-dealer-router")
		perfcommon.Must(router.Bind(endpoint))
		perfcommon.Must(dealer.SetRoutingID(rid))
		perfcommon.Must(dealer.Connect(endpoint))
		perfcommon.WaitConnected(routerMon, dealerMon)
		perfcommon.Must(dealer.SetRecvTimeout(500 * time.Millisecond))
		perfcommon.Must(dealer.SetSendTimeout(500 * time.Millisecond))
		startMultiRouterCallbackEchoServer(router)
		waitForDealerReady(dealer)

		payload := perfcommon.PreparePayload(cfg.msgSize)
		stopAt := time.Now().Add(segmentWarmup + segmentDuration)
		activeAt := time.Now().Add(segmentWarmup)
		for time.Now().Before(stopAt) {
			perfcommon.StampPayload(payload)
			err := dealer.Send(perfcommon.NewMessage(payload))
			if err != nil {
				if perfcommon.IsTransient(err) {
					continue
				}
				perfcommon.Must(err)
			}
			reply, err := dealer.Recv()
			if err != nil {
				if perfcommon.IsTransient(err) {
					continue
				}
				perfcommon.Must(err)
			}
			part, err := reply.SinglePartOrError()
			if err == nil {
				if sentAt, ok := perfcommon.SentAtFromMessage(part); ok && time.Now().After(activeAt) {
					stats.Add(sentAt)
				}
			}
			_ = reply.Close()
		}

		_ = dealerMon.Close()
		_ = routerMon.Close()
		_ = dealer.Close()
		_ = router.Close()
		_ = ctx.Close()
	}

	return stats.Snapshot(cfg.duration, cfg.msgSize)
}

func waitForDealerReady(dealer *zlink.DealerSocket) {
	payload := perfcommon.PreparePayload(64)
	deadline := time.Now().Add(5 * time.Second)
	for time.Now().Before(deadline) {
		perfcommon.StampPayload(payload)
		err := dealer.Send(perfcommon.NewMessage(payload))
		if err != nil {
			if perfcommon.IsTransient(err) {
				continue
			}
			perfcommon.Must(err)
		}
		reply, err := dealer.Recv()
		if err != nil {
			if perfcommon.IsTransient(err) {
				continue
			}
			perfcommon.Must(err)
		}
		perfcommon.Must(reply.Close())
		return
	}
	perfcommon.Must(&multiDealerRouterReadyError{})
}

type multiDealerRouterReadyError struct{}

func (e *multiDealerRouterReadyError) Error() string {
	return "multi dealer/router perf endpoint did not become ready"
}

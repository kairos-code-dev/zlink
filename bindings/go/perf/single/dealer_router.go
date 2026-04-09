package main

import (
	"time"

	"zlink"
	"zlink/perf/internal/perfcommon"
)

func runDealerRouter(cfg benchmarkConfig) perfcommon.Result {
	ctx, err := zlink.NewContext()
	perfcommon.Must(err)
	defer ctx.Close()

	router, err := ctx.RouterSocket()
	perfcommon.Must(err)
	defer router.Close()
	dealer, err := ctx.DealerSocket()
	perfcommon.Must(err)
	defer dealer.Close()
	routerMon := perfcommon.OpenMonitor(router)
	defer routerMon.Close()
	dealerMon := perfcommon.OpenMonitor(dealer)
	defer dealerMon.Close()

	rid, err := zlink.NewRoutingID([]byte("perf-dealer"))
	perfcommon.Must(err)

	perfcommon.Must(perfcommon.ConfigureTLSServer(router, cfg.transport))
	perfcommon.Must(perfcommon.ConfigureTLSClient(dealer, cfg.transport))
	endpoint := perfcommon.BindAndResolveEndpoint(router, cfg.transport, "perf-dealer-router")
	perfcommon.Must(dealer.SetRoutingID(rid))
	perfcommon.Must(dealer.Connect(endpoint))
	perfcommon.WaitConnected(routerMon, dealerMon)
	perfcommon.Must(dealer.SetRecvTimeout(perfcommon.BenchmarkSocketTimeout))
	perfcommon.Must(dealer.SetSendTimeout(perfcommon.BenchmarkSocketTimeout))
	startRouterEchoServer(router)

	stats := perfcommon.NewStats()
	payload := perfcommon.PreparePayload(cfg.msgSize)
	window := perfcommon.NewBenchmarkWindow(0, cfg.duration)

	for time.Now().Before(window.StopAt) {
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
		perfcommon.Must(err)
		perfcommon.RecordMessageLatency(stats, window.ActiveAt, part)
		perfcommon.Must(reply.Close())
	}

	return stats.Snapshot(cfg.duration, cfg.msgSize)
}

func waitForDealerRouterReady(dealer *zlink.DealerSocket) {
	payload := perfcommon.PreparePayload(64)
	perfcommon.Must(perfcommon.WaitReady(perfcommon.ReadyConfig{
		Name: "dealer/router perf endpoint",
		Probe: func() (bool, error) {
			perfcommon.StampPayload(payload)
			err := dealer.Send(perfcommon.NewMessage(payload))
			if err != nil {
				if perfcommon.IsTransient(err) {
					return false, nil
				}
				return false, err
			}
			reply, err := dealer.Recv()
			if err != nil {
				if perfcommon.IsTransient(err) {
					return false, nil
				}
				return false, err
			}
			return true, reply.Close()
		},
	}))
}

func startRouterEchoServer(router *zlink.RouterSocket) {
	perfcommon.Must(router.SetRecvTimeout(perfcommon.BenchmarkSocketTimeout))
	go func() {
		for {
			received, err := router.Recv()
			if err != nil {
				if perfcommon.IsTransient(err) {
					continue
				}
				return
			}
			err = router.SendTo(received.RoutingID(),
				perfcommon.CloneMessages(received.Parts())...)
			if err != nil && !perfcommon.IsTransient(err) {
				perfcommon.Must(err)
			}
		}
	}()
}

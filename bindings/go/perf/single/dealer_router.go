package main

import (
	"runtime"
	"sync"
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

	rid := zlink.NewRoutingID([]byte("perf-dealer"))

	perfcommon.Must(perfcommon.ConfigureTLSServer(router, cfg.transport))
	perfcommon.Must(perfcommon.ConfigureTLSClient(dealer, cfg.transport))
	perfcommon.ApplySingleHWM(router)
	perfcommon.ApplySingleHWM(dealer)
	endpoint := perfcommon.BindAndResolveEndpoint(router, cfg.transport, "perf-dealer-router")
	perfcommon.Must(dealer.SetRoutingID(rid))
	perfcommon.Must(dealer.Connect(endpoint))
	perfcommon.WaitConnected(routerMon, dealerMon)
	perfcommon.ApplySingleBenchmarkSocketOptions(router, cfg.transport)
	perfcommon.ApplySingleBenchmarkSocketOptions(dealer, cfg.transport)
	perfcommon.Must(dealer.SetRecvTimeout(perfcommon.BenchmarkSocketTimeout))
	perfcommon.Must(dealer.SetSendTimeout(perfcommon.BenchmarkSocketTimeout))
	stopRouterEchoServer := startRouterEchoServer(router)
	defer stopRouterEchoServer()
	waitForDealerRouterReady(dealer)

	stats := perfcommon.NewStats()
	payload := perfcommon.PreparePayload(cfg.msgSize)
	window := perfcommon.NewBenchmarkWindow(0, cfg.duration)

	for time.Now().Before(window.StopAt) {
		perfcommon.StampPayload(payload)
		err := dealer.Send(zlink.SendFlagsNone, perfcommon.NewMessage(payload))
		if err != nil {
			if perfcommon.IsTransient(err) {
				continue
			}
			perfcommon.Must(err)
		}
		reply, err := dealer.Recv(zlink.RecvFlagsNone)
		if err != nil {
			if perfcommon.IsTransient(err) {
				continue
			}
			perfcommon.Must(err)
		}
		part, err := reply.SinglePartOrError()
		perfcommon.Must(err)
		runtime.KeepAlive(reply)
		perfcommon.RecordMessageLatency(stats, window.ActiveAt, part)
		if err := reply.Close(); err != nil {
			perfcommon.Must(err)
		}
		runtime.KeepAlive(part)
		runtime.KeepAlive(reply)
	}
	return stats.Snapshot(cfg.duration, cfg.msgSize)
}

func waitForDealerRouterReady(dealer *zlink.DealerSocket) {
	payload := perfcommon.PreparePayload(64)
	readyHits := 0
	perfcommon.Must(perfcommon.WaitReady(perfcommon.ReadyConfig{
		Name: "dealer/router perf endpoint",
		Probe: func() (bool, error) {
			perfcommon.StampPayload(payload)
			err := dealer.Send(zlink.SendFlagsNone, perfcommon.NewMessage(payload))
			if err != nil {
				if perfcommon.IsTransient(err) {
					return false, nil
				}
				return false, err
			}
			reply, err := dealer.Recv(zlink.RecvFlagsNone)
			if err != nil {
				if perfcommon.IsTransient(err) {
					return false, nil
				}
				return false, err
			}
			if err := reply.Close(); err != nil {
				return false, err
			}
			runtime.KeepAlive(reply)
			readyHits++
			return readyHits >= 2, nil
		},
	}))
}

func startRouterEchoServer(router *zlink.RouterSocket) func() {
	perfcommon.Must(router.SetRecvTimeout(500 * time.Millisecond))
	stop := make(chan struct{})
	var wg sync.WaitGroup
	wg.Add(1)
	go func() {
		defer wg.Done()
		for {
			select {
			case <-stop:
				return
			default:
			}
			received, err := router.Recv(zlink.RecvFlagsDontWait)
			if err != nil {
				if perfcommon.IsTransient(err) {
					continue
				}
				return
			}
			if received == nil {
				continue
			}
			err = router.SendTo(received.RoutingID(), zlink.SendFlagsNone,
				perfcommon.CloneMessages(received.Parts())...)
			if err != nil && !perfcommon.IsTransient(err) {
				perfcommon.Must(err)
			}
			if err := received.Close(); err != nil {
				perfcommon.Must(err)
			}
		}
	}()
	return func() {
		close(stop)
		wg.Wait()
	}
}

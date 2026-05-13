package main

import (
	"fmt"
	"sync"
	"time"

	"zlink.systems/zlink"
	"zlink.systems/zlink/perf/internal/perfcommon"
)

func runMultiDealerRouter(cfg multiConfig) perfcommon.Result {
	serverCtx, err := zlink.NewContext()
	perfcommon.Must(err)
	defer serverCtx.Close()

	router, err := serverCtx.RouterSocket()
	perfcommon.Must(err)
	defer router.Close()

	perfcommon.Must(perfcommon.ConfigureTLSServer(router, cfg.transport))
	perfcommon.ApplyMultiHWM(router, cfg.pattern)
	perfcommon.ApplyMultiBenchmarkSocketOptions(router, cfg.transport)
	endpoint := perfcommon.BindAndResolveEndpoint(router, cfg.transport, "perf-multi-dealer-router")
	serverDone := make(chan struct{})
	go startMultiRouterRouterEchoServer(router, serverDone)

	stats := perfcommon.NewStats()
	var window perfcommon.BenchmarkWindow

	type dealerClient struct {
		ctx     *zlink.Context
		socket  *zlink.DealerSocket
		monitor *zlink.SocketMonitor
	}
	dealers := make([]dealerClient, 0, cfg.clients)
	for i := 0; i < cfg.clients; i++ {
		clientCtx, err := zlink.NewContext()
		perfcommon.Must(err)
		dealer, err := clientCtx.DealerSocket()
		perfcommon.Must(err)
		dealerMon := perfcommon.OpenMonitor(dealer)
		perfcommon.Must(perfcommon.ConfigureTLSClient(dealer, cfg.transport))
		perfcommon.ApplyMultiHWM(dealer, cfg.pattern)
		perfcommon.ApplyMultiBenchmarkSocketOptions(dealer, cfg.transport)

		rid := zlink.NewRoutingID([]byte(fmt.Sprintf("dealer-%06d", i)))

		perfcommon.Must(dealer.SetRoutingID(rid))
		if err := dealer.Connect(endpoint); err != nil {
			perfcommon.Must(fmt.Errorf("multi dealer/router connect client[%d]: %w", i, err))
		}
		perfcommon.WaitConnectedWithTimeout(perfcommon.MultiReadyTimeout(), dealerMon)

		dealers = append(dealers, dealerClient{
			ctx:     clientCtx,
			socket:  dealer,
			monitor: dealerMon,
		})
	}
	defer func() {
		for _, dealer := range dealers {
			_ = dealer.monitor.Close()
			_ = dealer.socket.Close()
			_ = dealer.ctx.Close()
		}
	}()
	window = perfcommon.NewBenchmarkWindow(cfg.duration)

	var wg sync.WaitGroup
	for _, dealer := range dealers {
		wg.Add(1)
		go func(socket *zlink.DealerSocket) {
			defer wg.Done()

			payload := perfcommon.PreparePayload(cfg.msgSize)
			for time.Now().Before(window.StopAt) {
				perfcommon.StampWindowPayload(payload, window.ActiveAt)
				_, err := socket.Send().Message(perfcommon.NewMessage(payload)).Submit(nil)
				if err != nil {
					if perfcommon.IsTransient(err) {
						continue
					}
					perfcommon.Must(fmt.Errorf("multi dealer/router send: %w", err))
				}
				var reply zlink.Received
				ok, err := socket.Recv(&reply, zlink.RecvFlagsDontWait)
				if err != nil {
					if perfcommon.IsTransient(err) {
						continue
					}
					perfcommon.Must(fmt.Errorf("multi dealer/router recv: %w", err))
				}
				if !ok {
					continue
				}
				part, err := reply.SinglePartOrError()
				if err == nil {
					perfcommon.RecordMessageRTTLatency(stats, window.ActiveAt, cfg.msgSize, part)
				}
				_ = reply.Close()
			}
		}(dealer.socket)
	}

	wg.Wait()
	// PERF_MULTI_TEST_POLICY § 1.3.1: signal phase end via the
	// wire-level stop token. The first stop token received by the
	// server triggers shutdown.
	if len(dealers) > 0 {
		sendMultiDealerStopToken(dealers[0].socket)
	}
	<-serverDone
	return stats.Snapshot(cfg.duration, cfg.msgSize)
}

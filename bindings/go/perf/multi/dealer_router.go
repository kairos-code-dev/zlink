package main

import (
	"fmt"
	"sync"
	"time"

	"zlink"
	"zlink/perf/internal/perfcommon"
)

func runMultiDealerRouter(cfg multiConfig) perfcommon.Result {
	serverCtx, err := zlink.NewContext()
	perfcommon.Must(err)
	defer serverCtx.Close()

	router, err := serverCtx.RouterSocket()
	perfcommon.Must(err)
	defer router.Close()

	perfcommon.Must(perfcommon.ConfigureTLSServer(router, cfg.transport))
	endpoint := perfcommon.BindAndResolveEndpoint(router, cfg.transport, "perf-multi-dealer-router")
	startMultiRouterEchoServer(router)

	stats := perfcommon.NewStats()
	window := perfcommon.NewBenchmarkWindow(cfg.warmup, cfg.duration)

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

		rid := zlink.NewRoutingID([]byte(fmt.Sprintf("dealer-%06d", i)))

		perfcommon.Must(dealer.SetRoutingID(rid))
		if err := dealer.Connect(endpoint); err != nil {
			perfcommon.Must(fmt.Errorf("multi dealer/router connect client[%d]: %w", i, err))
		}
		perfcommon.WaitMonitorEvent(dealerMon)
		if err := dealer.SetRecvTimeout(500 * time.Millisecond); err != nil {
			perfcommon.Must(fmt.Errorf("multi dealer/router set recv timeout client[%d]: %w", i, err))
		}
		if err := dealer.SetSendTimeout(500 * time.Millisecond); err != nil {
			perfcommon.Must(fmt.Errorf("multi dealer/router set send timeout client[%d]: %w", i, err))
		}
		waitForDealerReady(dealer)

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

	var wg sync.WaitGroup
	for _, dealer := range dealers {
		wg.Add(1)
		go func(socket *zlink.DealerSocket) {
			defer wg.Done()

			payload := perfcommon.PreparePayload(cfg.msgSize)
			for time.Now().Before(window.StopAt) {
				perfcommon.StampWindowPayload(payload, window.ActiveAt)
				err := socket.Send(zlink.SendFlagsNone, perfcommon.NewMessage(payload))
				if err != nil {
					if perfcommon.IsTransient(err) {
						continue
					}
					perfcommon.Must(fmt.Errorf("multi dealer/router send: %w", err))
				}
				reply, err := socket.Recv(zlink.RecvFlagsDontWait)
				if err != nil {
					if perfcommon.IsTransient(err) {
						continue
					}
					perfcommon.Must(fmt.Errorf("multi dealer/router recv: %w", err))
				}
				if reply == nil {
					continue
				}
				part, err := reply.SinglePartOrError()
				if err == nil {
					perfcommon.RecordMessageLatency(stats, window.ActiveAt, cfg.msgSize, part)
				}
				_ = reply.Close()
			}
		}(dealer.socket)
	}

	wg.Wait()
	return stats.Snapshot(cfg.duration, cfg.msgSize)
}

func waitForDealerReady(dealer *zlink.DealerSocket) {
	payload := perfcommon.PreparePayload(64)
	perfcommon.Must(perfcommon.WaitReady(perfcommon.ReadyConfig{
		Name: "multi dealer/router perf endpoint",
		Probe: func() (bool, error) {
			perfcommon.StampProbePayload(payload)
			err := dealer.Send(zlink.SendFlagsNone, perfcommon.NewMessage(payload))
			if err != nil {
				if perfcommon.IsTransient(err) {
					return false, nil
				}
				return false, fmt.Errorf("multi dealer/router ready send: %w", err)
			}
			reply, err := dealer.Recv(zlink.RecvFlagsDontWait)
			if err != nil {
				if perfcommon.IsTransient(err) {
					return false, nil
				}
				return false, fmt.Errorf("multi dealer/router ready recv: %w", err)
			}
			if reply == nil {
				return false, nil
			}
			return true, reply.Close()
		},
	}))
}

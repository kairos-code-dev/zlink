package main

import (
	"fmt"
	"sync"
	"time"

	zlink "zlink.systems/zlink/contracts"
	"zlink.systems/zlink/perf/internal/perfcommon"
)

func runMultiDealerRouterServer(cfg multiConfig) {
	serverCtx, err := perfcommon.NewMultiServerContext()
	perfcommon.Must(err)
	defer serverCtx.Close()

	router, err := serverCtx.RouterSocket()
	perfcommon.Must(err)
	defer router.Close()

	perfcommon.Must(perfcommon.ConfigureTLSServer(router, cfg.transport))
	perfcommon.ApplyMultiAutoHWMMsgUnit(router, cfg.msgSize)
	perfcommon.ApplyMultiHWM(router, cfg.pattern)
	perfcommon.ApplyMultiBenchmarkSocketOptions(router, cfg.transport)
	endpoint := perfcommon.BindAndResolveEndpoint(router, cfg.transport, "perf-multi-dealer-router")
	flushControlLine("READY,%s", endpoint)

	serverDone := make(chan struct{})
	go startMultiRouterRouterEchoServer(router, serverDone)
	select {
	case <-serverDone:
	case <-waitForStopAsync():
	}
}

// runMultiDealerRouterClient mirrors perf_multi_client_helpers.hpp
// run_echo_duration / run_echo_window_round_robin: exactly one
// outstanding request per dealer (awaiting_reply / send_pending), a
// single poller per socket arming POLLIN while awaiting a reply and
// POLLOUT while a send is pending, signal-driven wait (-1), and RTT/2
// latency. The phase ends purely on the active deadline (the C echo
// client emits no stop token; the runner stops the server).
func runMultiDealerRouterClient(cfg multiConfig, endpoint string) perfcommon.Result {
	stats := perfcommon.NewStats()
	type dealerClient struct {
		ctx     *zlink.Context
		socket  *zlink.DealerSocket
		monitor *zlink.SocketMonitor
	}
	dealers := make([]dealerClient, 0, cfg.clients)
	for i := 0; i < cfg.clients; i++ {
		clientCtx, err := perfcommon.NewMultiClientContext()
		perfcommon.Must(err)
		dealer, err := clientCtx.DealerSocket()
		perfcommon.Must(err)
		dealerMon := perfcommon.OpenMonitor(dealer)
		perfcommon.Must(perfcommon.ConfigureTLSClient(dealer, cfg.transport))
		perfcommon.ApplyMultiAutoHWMMsgUnit(dealer, cfg.msgSize)
		perfcommon.ApplyMultiHWM(dealer, cfg.pattern)
		perfcommon.ApplyMultiBenchmarkSocketOptions(dealer, cfg.transport)
		rid := zlink.NewRoutingID([]byte(fmt.Sprintf("dealer-%06d", i)))
		perfcommon.Must(dealer.SetRoutingID(rid))
		perfcommon.Must(dealer.Connect(endpoint))
		perfcommon.WaitConnectedWithTimeout(perfcommon.MultiReadyTimeout(), dealerMon)
		dealers = append(dealers, dealerClient{ctx: clientCtx, socket: dealer, monitor: dealerMon})
	}
	defer func() {
		for _, dealer := range dealers {
			_ = dealer.monitor.Close()
			_ = dealer.socket.Close()
			_ = dealer.ctx.Close()
		}
	}()

	window := activeDeadline(cfg.duration)
	var wg sync.WaitGroup
	for _, dealer := range dealers {
		wg.Add(1)
		go func(socket *zlink.DealerSocket) {
			defer wg.Done()
			runEchoDealer(socket, stats, cfg.msgSize, window)
		}(dealer.socket)
	}
	wg.Wait()
	return stats.Snapshot(cfg.duration, cfg.msgSize)
}

// runEchoDealer drives a single dealer socket with at most one
// outstanding request, mirroring the per-socket awaiting_reply /
// send_pending state machine in run_echo_window_round_robin.
func runEchoDealer(socket *zlink.DealerSocket, stats *perfcommon.Stats, msgSize int, window perfcommon.BenchmarkWindow) {
	poller := perfcommon.NewSocketPoller(socket, perfcommon.ZLinkPollOut)
	defer poller.Close()
	payload := perfcommon.PreparePayload(msgSize)
	awaitingReply := false
	for time.Now().Before(window.StopAt) {
		if !awaitingReply {
			perfcommon.StampWindowPayload(payload, window.ActiveAt)
			_, err := socket.Send().Message(perfcommon.NewMessage(payload)).Flags(zlink.SendFlagsDontWait).Submit(nil)
			if err == nil {
				awaitingReply = true
				perfcommon.Must(poller.ModifySocket(socket, perfcommon.ZLinkPollIn))
				continue
			}
			if !perfcommon.IsTransient(err) {
				perfcommon.Must(fmt.Errorf("multi dealer/router client send: %w", err))
			}
			// send_pending: wait POLLOUT (-1) until writable.
			if _, waitErr := poller.Wait(-1 * time.Millisecond); waitErr != nil {
				if perfcommon.IsTransient(waitErr) {
					continue
				}
				perfcommon.Must(fmt.Errorf("multi dealer/router client poll: %w", waitErr))
			}
			continue
		}

		// awaiting_reply: wait POLLIN (-1) for the echo.
		event, waitErr := poller.Wait(-1 * time.Millisecond)
		if waitErr != nil {
			if perfcommon.IsTransient(waitErr) {
				continue
			}
			perfcommon.Must(fmt.Errorf("multi dealer/router client poll: %w", waitErr))
		}
		if event == nil || event.Events&perfcommon.ZLinkPollIn == 0 {
			continue
		}
		var reply zlink.Received
		ok, err := socket.Recv(&reply, zlink.RecvFlagsDontWait)
		if err != nil {
			if perfcommon.IsTransient(err) {
				continue
			}
			perfcommon.Must(fmt.Errorf("multi dealer/router client recv: %w", err))
		}
		if !ok {
			continue
		}
		part, partErr := reply.SinglePartOrError()
		if partErr == nil {
			perfcommon.RecordMessageRTTLatency(stats, window.ActiveAt, window.StopAt, msgSize, part)
		}
		_ = reply.Close()
		awaitingReply = false
		perfcommon.Must(poller.ModifySocket(socket, perfcommon.ZLinkPollOut))
	}
}

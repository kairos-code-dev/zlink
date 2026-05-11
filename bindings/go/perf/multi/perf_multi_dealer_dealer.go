package main

import (
	"fmt"
	"sync"
	"time"

	"zlink.systems/zlink"
	"zlink.systems/zlink/perf/internal/perfcommon"
)

func runMultiDealerDealer(cfg multiConfig) perfcommon.Result {
	serverCtx, err := zlink.NewContext()
	perfcommon.Must(err)
	defer serverCtx.Close()

	server, err := serverCtx.DealerSocket()
	perfcommon.Must(err)
	defer server.Close()
	serverMon := perfcommon.OpenMonitor(server)
	defer serverMon.Close()

	stats := perfcommon.NewStats()
	var window perfcommon.BenchmarkWindow

	type dealerClient struct {
		ctx    *zlink.Context
		socket *zlink.DealerSocket
		mon    *zlink.SocketMonitor
	}
	clients := make([]dealerClient, 0, cfg.clients)
	perfcommon.Must(perfcommon.ConfigureTLSServer(server, cfg.transport))
	perfcommon.ApplyMultiHWM(server, cfg.pattern)
	endpoint := perfcommon.BindAndResolveEndpoint(server, cfg.transport, "perf-multi-dealer-dealer")
	perfcommon.ApplyMultiBenchmarkSocketOptions(server, cfg.transport)
	for i := 0; i < cfg.clients; i++ {
		clientCtx, err := zlink.NewContext()
		perfcommon.Must(err)
		client, err := clientCtx.DealerSocket()
		perfcommon.Must(err)
		clientMon := perfcommon.OpenMonitor(client)
		perfcommon.Must(perfcommon.ConfigureTLSClient(client, cfg.transport))
		perfcommon.ApplyMultiHWM(client, cfg.pattern)
		perfcommon.Must(client.Connect(endpoint))
		perfcommon.ApplyMultiBenchmarkSocketOptions(client, cfg.transport)
		perfcommon.WaitConnectedWithTimeout(perfcommon.MultiReadyTimeout(), serverMon, clientMon)
		clients = append(clients, dealerClient{ctx: clientCtx, socket: client, mon: clientMon})
	}
	defer func() {
		for _, client := range clients {
			_ = client.mon.Close()
			_ = client.socket.Close()
			_ = client.ctx.Close()
		}
	}()
	window = perfcommon.NewBenchmarkWindow(cfg.duration)

	// PERF_MULTI_TEST_POLICY § 1.3.1: server recv loop waits on the
	// poller with -1 (signal-driven) and exits on the first stop token
	// it observes. The cpp reference uses the same single-token
	// shutdown so transient back-pressure on the per-client send path
	// cannot wedge the server.
	recvDone := make(chan struct{})
	go func() {
		defer close(recvDone)
		poller := perfcommon.NewSocketPoller(server, perfcommon.ZLinkPollIn)
		defer poller.Close()

		stopRequested := false
		for !stopRequested {
			event, err := poller.Wait(-1 * time.Millisecond)
			if err != nil {
				if perfcommon.IsTransient(err) {
					continue
				}
				perfcommon.Must(fmt.Errorf("multi dealer/dealer server poll: %w", err))
			}
			if event == nil {
				continue
			}
			if event.Events&perfcommon.ZLinkPollIn == 0 {
				continue
			}
			var received zlink.Received
			for {
				ok, err := server.Recv(&received, zlink.RecvFlagsDontWait)
				if err != nil {
					if perfcommon.IsTransient(err) {
						break
					}
					perfcommon.Must(fmt.Errorf("multi dealer/dealer server recv: %w", err))
				}
				if !ok {
					break
				}
				part, err := received.SinglePartOrError()
				if err == nil {
					if perfcommon.IsStopTokenMessage(part) {
						stopRequested = true
						_ = received.Close()
						break
					}
					now := time.Now()
					if sentAt, ok := perfcommon.SentAtFromMessage(part, cfg.msgSize); ok && now.After(window.ActiveAt) && now.Before(window.StopAt) {
						stats.Add(sentAt)
					}
				}
				perfcommon.Must(received.Close())
			}
		}
	}()

	var wg sync.WaitGroup
	for _, client := range clients {
		wg.Add(1)
		go func(socket *zlink.DealerSocket) {
			defer wg.Done()

			payload := perfcommon.PreparePayload(cfg.msgSize)
			for time.Now().Before(window.StopAt) {
				perfcommon.StampWindowPayload(payload, window.ActiveAt)
				_, err := socket.Send(zlink.SendFlagsNone, perfcommon.NewMessage(payload))
				if err != nil {
					if perfcommon.IsTransient(err) {
						continue
					}
					perfcommon.Must(fmt.Errorf("multi dealer/dealer send: %w", err))
				}
			}
		}(client.socket)
	}

	wg.Wait()
	// PERF_MULTI_TEST_POLICY § 1.3.1: emit the wire-level stop token
	// once per benchmark, mirroring the cpp client. Server exits on
	// first observed token so any single client surviving the active
	// phase suffices.
	if len(clients) > 0 {
		sendMultiDealerStopToken(clients[0].socket)
	}
	<-recvDone
	return stats.Snapshot(cfg.duration, cfg.msgSize)
}

// sendMultiDealerStopToken pushes the wire-level stop token through the
// dealer socket. Bounded retry through transient backpressure mirrors
// the cpp / java / dotnet implementations.
func sendMultiDealerStopToken(socket *zlink.DealerSocket) {
	for retry := 0; retry < perfcommon.StopTokenSendRetries; retry++ {
		sent, err := socket.Send(zlink.SendFlagsNone, perfcommon.NewMessage(perfcommon.StopToken))
		if err == nil && sent {
			return
		}
		if err != nil && !perfcommon.IsTransient(err) {
			return
		}
		time.Sleep(perfcommon.StopTokenSendBackoff)
	}
}

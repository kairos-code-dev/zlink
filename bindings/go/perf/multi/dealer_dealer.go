package main

import (
	"fmt"
	"sync"
	"time"

	"zlink"
	"zlink/perf/internal/perfcommon"
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
	window := perfcommon.NewBenchmarkWindow(0, cfg.duration)
	recvStopAt := window.StopAt.Add(500 * time.Millisecond)

	type dealerClient struct {
		ctx    *zlink.Context
		socket *zlink.DealerSocket
		mon    *zlink.SocketMonitor
	}
	clients := make([]dealerClient, 0, cfg.clients)
	perfcommon.Must(perfcommon.ConfigureTLSServer(server, cfg.transport))
	endpoint := perfcommon.BindAndResolveEndpoint(server, cfg.transport, "perf-multi-dealer-dealer")
	perfcommon.Must(server.SetRecvTimeout(500 * time.Millisecond))
	for i := 0; i < cfg.clients; i++ {
		clientCtx, err := zlink.NewContext()
		perfcommon.Must(err)
		client, err := clientCtx.DealerSocket()
		perfcommon.Must(err)
		clientMon := perfcommon.OpenMonitor(client)
		perfcommon.Must(perfcommon.ConfigureTLSClient(client, cfg.transport))
		perfcommon.Must(client.Connect(endpoint))
		perfcommon.Must(client.SetSendTimeout(500 * time.Millisecond))
		perfcommon.WaitConnected(serverMon, clientMon)
		clients = append(clients, dealerClient{ctx: clientCtx, socket: client, mon: clientMon})
	}
	defer func() {
		for _, client := range clients {
			_ = client.mon.Close()
			_ = client.socket.Close()
			_ = client.ctx.Close()
		}
	}()

	recvDone := make(chan struct{})
	go func() {
		defer close(recvDone)
		for time.Now().Before(recvStopAt) {
			received, err := server.Recv(zlink.RecvFlagsDontWait)
			if err != nil {
				if perfcommon.IsTransient(err) {
					continue
				}
				perfcommon.Must(fmt.Errorf("multi dealer/dealer server recv: %w", err))
			}
			if received == nil {
				time.Sleep(50 * time.Microsecond)
				continue
			}
			part, err := received.SinglePartOrError()
			if err == nil {
				now := time.Now()
				if sentAt, ok := perfcommon.SentAtFromMessage(part); ok && now.After(window.ActiveAt) && now.Before(recvStopAt) {
					stats.Add(sentAt)
				}
			}
			perfcommon.Must(received.Close())
		}
	}()

	var wg sync.WaitGroup
	for _, client := range clients {
		wg.Add(1)
		go func(socket *zlink.DealerSocket) {
			defer wg.Done()

			payload := perfcommon.PreparePayload(cfg.msgSize)
			for time.Now().Before(window.StopAt) {
				perfcommon.StampPayload(payload)
				err := socket.Send(zlink.SendFlagsNone, perfcommon.NewMessage(payload))
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
	<-recvDone
	return stats.Snapshot(cfg.duration, cfg.msgSize)
}

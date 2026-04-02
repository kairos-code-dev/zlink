package main

import (
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

	endpoint := perfcommon.UniqueTCPEndpoint("perf-multi-dealer-dealer")
	perfcommon.Must(server.Bind(endpoint))
	startMultiDealerEchoServer(server)

	stats := perfcommon.NewStats()
	stopAt := time.Now().Add(cfg.warmup + cfg.duration)
	activeAt := time.Now().Add(cfg.warmup)

	type dealerClient struct {
		ctx    *zlink.Context
		socket *zlink.DealerSocket
	}
	clients := make([]dealerClient, 0, cfg.clients)
	for i := 0; i < cfg.clients; i++ {
		clientCtx, err := zlink.NewContext()
		perfcommon.Must(err)
		client, err := clientCtx.DealerSocket()
		perfcommon.Must(err)
		perfcommon.Must(client.Connect(endpoint))
		perfcommon.Must(client.SetRecvTimeout(500 * time.Millisecond))
		perfcommon.Must(client.SetSendTimeout(500 * time.Millisecond))
		waitForDealerReady(client)
		clients = append(clients, dealerClient{ctx: clientCtx, socket: client})
	}
	defer func() {
		for _, client := range clients {
			_ = client.socket.Close()
			_ = client.ctx.Close()
		}
	}()

	var wg sync.WaitGroup
	for _, client := range clients {
		wg.Add(1)
		go func(socket *zlink.DealerSocket) {
			defer wg.Done()

			payload := perfcommon.PreparePayload(cfg.msgSize)
			for time.Now().Before(stopAt) {
				perfcommon.StampPayload(payload)
				err := socket.Send(perfcommon.NewMessage(payload))
				if err != nil {
					if perfcommon.IsTransient(err) {
						continue
					}
					perfcommon.Must(err)
				}
				reply, ok, err := socket.TryRecv()
				if err != nil {
					perfcommon.Must(err)
				}
				if !ok || reply == nil {
					continue
				}
				part, err := reply.SinglePartOrError()
				if err == nil {
					if sentAt, ok := perfcommon.SentAtFromMessage(part); ok && time.Now().After(activeAt) {
						stats.Add(sentAt)
					}
				}
				_ = reply.Close()
			}
		}(client.socket)
	}

	wg.Wait()
	return stats.Snapshot(cfg.duration, cfg.msgSize)
}

func startMultiDealerEchoServer(server *zlink.DealerSocket) {
	perfcommon.Must(server.SetRecvTimeout(500 * time.Millisecond))
	go func() {
		for {
			received, ok, err := server.TryRecv()
			if err != nil {
				perfcommon.Must(err)
			}
			if !ok || received == nil {
				continue
			}
			err = server.Send(perfcommon.CloneMessages(received.Parts())...)
			if err != nil && !perfcommon.IsTransient(err) {
				perfcommon.Must(err)
			}
			perfcommon.Must(received.Close())
		}
	}()
}

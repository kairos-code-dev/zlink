package main

import (
	"time"

	"zlink"
	"zlink/perf/internal/perfcommon"
)

func runDealerDealer(cfg benchmarkConfig) perfcommon.Result {
	ctx, err := zlink.NewContext()
	perfcommon.Must(err)
	defer ctx.Close()

	server, err := ctx.DealerSocket()
	perfcommon.Must(err)
	defer server.Close()
	client, err := ctx.DealerSocket()
	perfcommon.Must(err)
	defer client.Close()

	endpoint := perfcommon.UniqueTCPEndpoint("perf-dealer-dealer")
	perfcommon.Must(server.Bind(endpoint))
	perfcommon.Must(client.Connect(endpoint))
	perfcommon.Must(client.SetRecvTimeout(500 * time.Millisecond))
	perfcommon.Must(client.SetSendTimeout(500 * time.Millisecond))
	startDealerEchoServer(server, cfg.recvMode)

	stats := perfcommon.NewStats()
	payload := perfcommon.PreparePayload(cfg.msgSize)
	window := perfcommon.NewBenchmarkWindow(0, cfg.duration)

	for time.Now().Before(window.StopAt) {
		perfcommon.StampPayload(payload)
		err := client.Send(perfcommon.NewMessage(payload))
		if err != nil {
			if perfcommon.IsTransient(err) {
				continue
			}
			perfcommon.Must(err)
		}
		reply, err := client.Recv()
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

func startDealerEchoServer(server *zlink.DealerSocket, recvMode string) {
	if recvMode == "callback" {
		perfcommon.Must(server.OnReceive(func(received *zlink.Received) {
			defer received.Close()
			perfcommon.Must(server.Send(perfcommon.CloneMessages(received.Parts())...))
		}))
		return
	}

	perfcommon.Must(server.SetRecvTimeout(500 * time.Millisecond))
	go func() {
		for {
			received, err := server.Recv()
			if err != nil {
				if perfcommon.IsTransient(err) {
					continue
				}
				return
			}
			err = server.Send(perfcommon.CloneMessages(received.Parts())...)
			if err != nil && !perfcommon.IsTransient(err) {
				perfcommon.Must(err)
			}
			perfcommon.Must(received.Close())
		}
	}()
}

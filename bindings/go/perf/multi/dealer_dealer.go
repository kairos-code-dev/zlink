package main

import (
	"time"

	"zlink"
	"zlink/perf/internal/perfcommon"
)

func runMultiDealerDealer(cfg multiConfig) perfcommon.Result {
	stats := perfcommon.NewStats()
	segmentWarmup := splitDuration(cfg.warmup, cfg.clients)
	segmentDuration := splitDuration(cfg.duration, cfg.clients)

	for i := 0; i < cfg.clients; i++ {
		ctx, err := zlink.NewContext()
		perfcommon.Must(err)

		server, err := ctx.DealerSocket()
		perfcommon.Must(err)
		client, err := ctx.DealerSocket()
		perfcommon.Must(err)

		endpoint := perfcommon.UniqueTCPEndpoint("perf-multi-dealer-dealer")
		perfcommon.Must(server.Bind(endpoint))
		perfcommon.Must(client.Connect(endpoint))
		perfcommon.Must(client.SetRecvTimeout(500 * time.Millisecond))
		perfcommon.Must(client.SetSendTimeout(500 * time.Millisecond))
		startMultiDealerCallbackEchoServer(server)
		waitForDealerReady(client)

		payload := perfcommon.PreparePayload(cfg.msgSize)
		stopAt := time.Now().Add(segmentWarmup + segmentDuration)
		activeAt := time.Now().Add(segmentWarmup)
		for time.Now().Before(stopAt) {
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
			if err == nil {
				if sentAt, ok := perfcommon.SentAtFromMessage(part); ok && time.Now().After(activeAt) {
					stats.Add(sentAt)
				}
			}
			_ = reply.Close()
		}

		_ = client.Close()
		_ = server.Close()
		_ = ctx.Close()
	}

	return stats.Snapshot(cfg.duration, cfg.msgSize)
}

func startMultiDealerCallbackEchoServer(server *zlink.DealerSocket) {
	perfcommon.Must(server.OnReceive(func(received *zlink.Received) {
		defer received.Close()
		perfcommon.Must(server.Send(perfcommon.CloneMessages(received.Parts())...))
	}))
}

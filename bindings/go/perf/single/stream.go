package main

import (
	"io"
	"time"

	"zlink"
	"zlink/perf/internal/perfcommon"
)

func runStream(cfg benchmarkConfig) perfcommon.Result {
	ctx, err := zlink.NewContext()
	perfcommon.Must(err)
	defer ctx.Close()

	server, err := ctx.StreamSocket()
	perfcommon.Must(err)
	defer server.Close()

	endpoint := perfcommon.UniqueTCPEndpoint("perf-stream")
	perfcommon.Must(server.Bind(endpoint))
	startStreamEchoServer(server, cfg.recvMode)

	conn := perfcommon.DialEndpoint(endpoint)
	defer conn.Close()
	perfcommon.Must(conn.SetDeadline(time.Now().Add(cfg.warmup + cfg.duration + 5*time.Second)))

	stats := perfcommon.NewStats()
	payload := perfcommon.PreparePayload(cfg.msgSize)
	reply := make([]byte, cfg.msgSize)
	stopAt := time.Now().Add(cfg.warmup + cfg.duration)
	activeAt := time.Now().Add(cfg.warmup)

	for time.Now().Before(stopAt) {
		perfcommon.StampPayload(payload)
		_, err = conn.Write(payload)
		perfcommon.Must(err)
		_, err = io.ReadFull(conn, reply)
		perfcommon.Must(err)
		if sentAt, ok := perfcommon.SentAtFromBytes(reply); ok && time.Now().After(activeAt) {
			stats.Add(sentAt)
		}
	}

	return stats.Snapshot(cfg.duration, cfg.msgSize)
}

func startStreamEchoServer(server *zlink.StreamSocket, recvMode string) {
	if recvMode == "callback" {
		perfcommon.Must(server.OnReceive(func(received *zlink.Received) {
			defer received.Close()
			perfcommon.Must(server.SendTo(received.RoutingID(),
				perfcommon.CloneMessages(received.Parts())...))
		}))
		return
	}

	perfcommon.Must(server.SetRecvTimeout(500 * time.Millisecond))
	go func() {
		for {
			received, err := server.Recv()
			if err != nil {
				return
			}
			perfcommon.Must(server.SendTo(received.RoutingID(),
				perfcommon.CloneMessages(received.Parts())...))
			perfcommon.Must(received.Close())
		}
	}()
}

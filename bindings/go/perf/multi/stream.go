package main

import (
	"io"
	"sync"
	"time"

	"zlink"
	"zlink/perf/internal/perfcommon"
)

func runMultiStream(cfg multiConfig) perfcommon.Result {
	ctx, err := zlink.NewContext()
	perfcommon.Must(err)
	defer ctx.Close()

	server, err := ctx.StreamSocket()
	perfcommon.Must(err)
	defer server.Close()

	endpoint := perfcommon.UniqueTCPEndpoint("perf-multi-stream")
	perfcommon.Must(server.Bind(endpoint))
	startMultiStreamEchoServer(server, cfg.recvMode)

	stats := perfcommon.NewStats()
	stopAt := time.Now().Add(cfg.warmup + cfg.duration)
	activeAt := time.Now().Add(cfg.warmup)

	var wg sync.WaitGroup
	for i := 0; i < cfg.clients; i++ {
		conn := perfcommon.DialEndpoint(endpoint)
		perfcommon.Must(conn.SetDeadline(time.Now().Add(cfg.warmup + cfg.duration + 5*time.Second)))
		wg.Add(1)
		go func(c io.ReadWriteCloser) {
			defer wg.Done()
			defer c.Close()

			payload := perfcommon.PreparePayload(cfg.msgSize)
			reply := make([]byte, cfg.msgSize)
			for time.Now().Before(stopAt) {
				perfcommon.StampPayload(payload)
				_, err := c.Write(payload)
				if err != nil {
					return
				}
				_, err = io.ReadFull(c, reply)
				if err != nil {
					return
				}
				if sentAt, ok := perfcommon.SentAtFromBytes(reply); ok && time.Now().After(activeAt) {
					stats.Add(sentAt)
				}
			}
		}(conn)
	}

	wg.Wait()
	return stats.Snapshot(cfg.duration, cfg.msgSize)
}

func startMultiStreamEchoServer(server *zlink.StreamSocket, recvMode string) {
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

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

	perfcommon.Must(perfcommon.ConfigureTLSServer(server, cfg.transport))
	endpoint := perfcommon.BindAndResolveEndpoint(server, cfg.transport, "perf-multi-stream")
	startMultiStreamEchoServer(server)

	stats := perfcommon.NewStats()
	window := perfcommon.NewBenchmarkWindow(0, cfg.duration)

	var wg sync.WaitGroup
	for i := 0; i < cfg.clients; i++ {
		conn := perfcommon.DialEndpoint(endpoint)
		perfcommon.Must(conn.SetDeadline(time.Now().Add(cfg.duration + 5*time.Second)))
		wg.Add(1)
		go func(c io.ReadWriteCloser) {
			defer wg.Done()
			defer c.Close()

			payload := perfcommon.PreparePayload(cfg.msgSize)
			reply := make([]byte, cfg.msgSize)
			for time.Now().Before(window.StopAt) {
				perfcommon.StampPayload(payload)
				_, err := c.Write(payload)
				if err != nil {
					return
				}
				_, err = io.ReadFull(c, reply)
				if err != nil {
					return
				}
				perfcommon.RecordBytesLatency(stats, window.ActiveAt, reply)
			}
		}(conn)
	}

	wg.Wait()
	return stats.Snapshot(cfg.duration, cfg.msgSize)
}

func startMultiStreamEchoServer(server *zlink.StreamSocket) {
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

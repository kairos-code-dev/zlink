package main

import (
	"io"
	"sync"
	"time"

	"zlink.systems/zlink"
	"zlink.systems/zlink/perf/internal/perfcommon"
)

func runMultiStream(cfg multiConfig) perfcommon.Result {
	ctx, err := perfcommon.NewMultiContext()
	perfcommon.Must(err)
	defer ctx.Close()

	server, err := ctx.StreamSocket()
	perfcommon.Must(err)
	defer server.Close()

	perfcommon.Must(perfcommon.ConfigureTLSServer(server, cfg.transport))
	perfcommon.ApplyMultiHWM(server, cfg.pattern)
	perfcommon.ApplyMultiBenchmarkSocketOptions(server, cfg.transport)
	endpoint := perfcommon.BindAndResolveEndpoint(server, cfg.transport, "perf-multi-stream")
	startMultiStreamEchoServer(server)

	stats := perfcommon.NewStats()
	conns := make([]perfcommon.PacketConn, 0, cfg.clients)
	for i := 0; i < cfg.clients; i++ {
		conns = append(conns, perfcommon.DialPacketEndpoint(endpoint))
	}
	window := perfcommon.NewBenchmarkWindow(cfg.duration)

	var wg sync.WaitGroup
	for _, conn := range conns {
		perfcommon.Must(conn.SetDeadline(time.Now().Add(cfg.duration + 5*time.Second)))
		wg.Add(1)
		go func(c io.ReadWriteCloser) {
			defer wg.Done()
			defer c.Close()

			payload := perfcommon.PreparePayload(cfg.msgSize)
			reply := make([]byte, cfg.msgSize)
			for time.Now().Before(window.StopAt) {
				perfcommon.StampWindowPayload(payload, window.ActiveAt)
				_, err := c.Write(payload)
				if err != nil {
					return
				}
				_, err = io.ReadFull(c, reply)
				if err != nil {
					return
				}
				perfcommon.RecordBytesRTTLatency(stats, window.ActiveAt, window.StopAt, cfg.msgSize, reply)
			}
		}(conn)
	}

	wg.Wait()
	return stats.Snapshot(cfg.duration, cfg.msgSize)
}

func runMultiStreamServer(cfg multiConfig) {
	ctx, err := perfcommon.NewMultiServerContext()
	perfcommon.Must(err)
	defer ctx.Close()

	server, err := ctx.StreamSocket()
	perfcommon.Must(err)
	defer server.Close()

	perfcommon.Must(perfcommon.ConfigureTLSServer(server, cfg.transport))
	perfcommon.ApplyMultiHWM(server, cfg.pattern)
	perfcommon.ApplyMultiBenchmarkSocketOptions(server, cfg.transport)
	endpoint := perfcommon.BindAndResolveEndpoint(server, cfg.transport, "perf-multi-stream")
	startMultiStreamEchoServer(server)
	flushControlLine("READY,%s", endpoint)
	waitForStopToken()
}

func runMultiStreamClient(cfg multiConfig, endpoint string) perfcommon.Result {
	stats := perfcommon.NewStats()
	conns := make([]perfcommon.PacketConn, 0, cfg.clients)
	for i := 0; i < cfg.clients; i++ {
		conns = append(conns, perfcommon.DialPacketEndpoint(endpoint))
	}
	window := activeDeadline(cfg.duration)

	var wg sync.WaitGroup
	for _, conn := range conns {
		perfcommon.Must(conn.SetDeadline(time.Now().Add(cfg.duration + 5*time.Second)))
		wg.Add(1)
		go func(c io.ReadWriteCloser) {
			defer wg.Done()
			defer c.Close()
			payload := perfcommon.PreparePayload(cfg.msgSize)
			reply := make([]byte, cfg.msgSize)
			for time.Now().Before(window.StopAt) {
				perfcommon.StampWindowPayload(payload, window.ActiveAt)
				if _, err := c.Write(payload); err != nil {
					return
				}
				if _, err := io.ReadFull(c, reply); err != nil {
					return
				}
				perfcommon.RecordBytesRTTLatency(stats, window.ActiveAt, window.StopAt, cfg.msgSize, reply)
			}
		}(conn)
	}
	wg.Wait()
	return stats.Snapshot(cfg.duration, cfg.msgSize)
}

func startMultiStreamEchoServer(server *zlink.StreamSocket) {
	perfcommon.Must(server.OnPacket(func(source zlink.RoutingID, header, body *zlink.Message) {
		packet := perfcommon.FrameStreamPacketMessage(header, body)
		_ = header.Close()
		_ = body.Close()
		_, sendErr := server.SendTo(source).Message(packet).Submit(nil)
		perfcommon.Must(sendErr)
	}))
}

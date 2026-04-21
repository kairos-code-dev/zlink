package main

import (
	"fmt"
	"sync"
	"time"

	"zlink"
	"zlink/perf/internal/perfcommon"
)

type multiSpotReqRepClient struct {
	ctx    *zlink.Context
	socket *zlink.RouterSocket
}

func runMultiSpotReqRep(cfg multiConfig) perfcommon.Result {
	replierCtx, err := zlink.NewContext()
	perfcommon.Must(err)
	defer replierCtx.Close()

	replierNode, err := replierCtx.SpotNode()
	perfcommon.Must(err)
	defer replierNode.Close()
	replier, err := replierNode.Spot()
	perfcommon.Must(err)
	defer replier.Close()

	perfcommon.Must(perfcommon.ConfigureTLSServer(replierNode, cfg.transport))
	perfcommon.ApplyMultiHWM(replier, cfg.pattern)
	perfcommon.ApplyMultiBenchmarkSocketOptions(replier, cfg.transport)
	endpoint := perfcommon.UniqueEndpoint(cfg.transport, "perf-multi-spot-reqrep")
	perfcommon.Must(replierNode.Bind(endpoint))

	replierNodeRID, err := replierNode.RoutingID()
	perfcommon.Must(err)
	replierSpotRID, err := replier.RoutingID()
	perfcommon.Must(err)

	perfcommon.Must(replier.OnDispatchEvent(func(event zlink.SpotDispatchEvent) {
		if event != zlink.SpotDispatchEventRoutedReadable {
			return
		}
		for {
			received, recvErr := replier.RecvRouted(zlink.RecvFlagsDontWait)
			if recvErr != nil {
				if perfcommon.IsTransient(recvErr) {
					return
				}
				perfcommon.Must(fmt.Errorf("multi spot reqrep replier recv: %w", recvErr))
			}
			if received == nil {
				return
			}
			part, partErr := received.SinglePartOrError()
			if partErr == nil {
				perfcommon.Must(received.Reply([]*zlink.Message{part}))
			}
			perfcommon.Must(received.Close())
		}
	}))

	clients := make([]multiSpotReqRepClient, 0, cfg.clients)
	for i := 0; i < cfg.clients; i++ {
		clientCtx, clientErr := zlink.NewContext()
		perfcommon.Must(clientErr)
		requester, requesterErr := clientCtx.RouterSocket()
		perfcommon.Must(requesterErr)
		perfcommon.Must(perfcommon.ConfigureTLSClient(requester, cfg.transport))
		perfcommon.ApplyMultiHWM(requester, cfg.pattern)
		perfcommon.ApplyMultiBenchmarkSocketOptions(requester, cfg.transport)
		perfcommon.Must(requester.Connect(endpoint))
		waitMultiSpotReqRepReady(requester, replierNodeRID, replierSpotRID, cfg.msgSize)
		clients = append(clients, multiSpotReqRepClient{
			ctx:    clientCtx,
			socket: requester,
		})
	}
	defer func() {
		for _, client := range clients {
			_ = client.socket.Close()
			_ = client.ctx.Close()
		}
	}()

	stats := perfcommon.NewStats()
	window := perfcommon.NewBenchmarkWindow(cfg.duration)

	var wg sync.WaitGroup
	for _, client := range clients {
		wg.Add(1)
		go func(socket *zlink.RouterSocket) {
			defer wg.Done()

			payload := perfcommon.PreparePayload(cfg.msgSize)
			for time.Now().Before(window.StopAt) {
				perfcommon.StampPayload(payload)
				replyDone := make(chan error, 1)
				ok, requestErr := socket.TryRequestToSpot(
					replierNodeRID,
					replierSpotRID,
					func(result zlink.RequestResult, parts []*zlink.Message) {
						defer func() {
							for _, part := range parts {
								_ = part.Close()
							}
						}()
						if result != zlink.RequestOK || len(parts) == 0 {
							replyDone <- fmt.Errorf("multi spot reqrep request failed: %v", result)
							return
						}
						if sentAt, ok := perfcommon.SentAtFromMessage(parts[0], cfg.msgSize); ok {
							stats.AddLatencyNs(float64(time.Since(sentAt).Nanoseconds()) / 2.0)
						}
						replyDone <- nil
					},
					perfcommon.MultiRecvTimeout(),
					perfcommon.NewMessage(payload),
				)
				if requestErr != nil {
					if perfcommon.IsTransient(requestErr) {
						continue
					}
					perfcommon.Must(requestErr)
				}
				if !ok {
					continue
				}
				perfcommon.Must(<-replyDone)
			}
		}(client.socket)
	}

	wg.Wait()
	return stats.Snapshot(cfg.duration, cfg.msgSize)
}

func waitMultiSpotReqRepReady(
	requester *zlink.RouterSocket,
	nodeRID zlink.RoutingID,
	spotRID zlink.RoutingID,
	msgSize int,
) {
	payload := perfcommon.PreparePayload(msgSize)
	perfcommon.StampProbePayload(payload)
	ready := make(chan error, 1)
	ok, err := requester.TryRequestToSpot(
		nodeRID,
		spotRID,
		func(result zlink.RequestResult, parts []*zlink.Message) {
			defer func() {
				for _, part := range parts {
					_ = part.Close()
				}
			}()
			if result != zlink.RequestOK || len(parts) == 0 {
				ready <- fmt.Errorf("multi spot reqrep ready probe failed: %v", result)
				return
			}
			if _, ok := perfcommon.SentAtFromMessagePhase(parts[0], msgSize, perfcommon.PhaseWarmup); !ok {
				ready <- fmt.Errorf("multi spot reqrep ready probe returned invalid payload")
				return
			}
			ready <- nil
		},
		perfcommon.MultiRecvTimeout(),
		perfcommon.NewMessage(payload),
	)
	if err != nil {
		perfcommon.Must(err)
	}
	if !ok {
		perfcommon.Must(fmt.Errorf("multi spot reqrep ready probe backpressured"))
	}
	perfcommon.Must(<-ready)
}

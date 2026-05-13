package main

import (
	"fmt"
	"sync"
	"time"

	"zlink.systems/zlink"
	"zlink.systems/zlink/perf/internal/perfcommon"
)

type multiSpotReqRepClient struct {
	ctx    *zlink.Context
	socket *zlink.RouterSocket
}

var (
	multiSpotReqRepNodeRID = zlink.NewRoutingID([]byte("perf-multi-spot-reqrep-node"))
	multiSpotReqRepSpotRID = zlink.NewRoutingID([]byte("perf-multi-spot-reqrep-spot"))
)

func runMultiSpotReqRep(cfg multiConfig) perfcommon.Result {
	replierCtx, err := zlink.NewContext()
	perfcommon.Must(err)
	defer replierCtx.Close()

	replierNode, err := replierCtx.SpotNode()
	perfcommon.Must(err)
	defer replierNode.Close()
	perfcommon.ApplyMultiSpotNodeAdmission(replierNode, cfg.pattern)
	replier, err := replierNode.Spot()
	perfcommon.Must(err)
	defer replier.Close()

	perfcommon.Must(perfcommon.ConfigureTLSServer(replierNode, cfg.transport))
	perfcommon.ApplyMultiBenchmarkSocketOptions(replier, cfg.transport)
	perfcommon.Must(replierNode.SetRoutingID(multiSpotReqRepNodeRID))
	perfcommon.Must(replier.SetRoutingID(multiSpotReqRepSpotRID))
	endpoint := perfcommon.UniqueEndpoint(cfg.transport, "perf-multi-spot-reqrep")
	perfcommon.Must(replierNode.Bind(endpoint))

	perfcommon.Must(replier.OnRoutedReceive(func(received *zlink.Received) {
		defer received.Close()
		parts := received.Parts()
		if len(parts) == 0 {
			return
		}
		reply, err := zlink.NewMessage(parts[0].Data())
		perfcommon.Must(err)
		defer reply.Close()
		sourceRID := received.RoutingID()
		requestSeq := received.RequestSeq()
		spotRID := received.SpotRID()
		if spotRID.Size() == 0 {
			replyErr := replier.ReplyToRouter(sourceRID, requestSeq).Message(reply).Submit(nil)
			perfcommon.Must(replyErr)
			return
		}
		replyErr := replier.ReplyToSpot(sourceRID, spotRID, requestSeq).Message(reply).Submit(nil)
		perfcommon.Must(replyErr)
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
		requesterRID := zlink.NewRoutingID([]byte(fmt.Sprintf("perf-multi-spot-reqrep-requester-%06d", i)))
		perfcommon.Must(requester.SetRoutingID(requesterRID))
		perfcommon.Must(requester.Connect(endpoint))
		waitMultiSpotReqRepReady(requester, multiSpotReqRepNodeRID, multiSpotReqRepSpotRID, cfg.msgSize)
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
				ok, requestErr := socket.RequestToSpot(multiSpotReqRepNodeRID, multiSpotReqRepSpotRID).
					Message(perfcommon.NewMessage(payload)).
					Flags(zlink.SendFlagsDontWait).
					Timeout(perfcommon.MultiRecvTimeout()).
					SubmitCallback(nil, func(result zlink.RequestResult, parts []*zlink.Message) {
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
					})
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
	ok, err := requester.RequestToSpot(nodeRID, spotRID).
		Message(perfcommon.NewMessage(payload)).
		Flags(zlink.SendFlagsDontWait).
		Timeout(perfcommon.MultiRecvTimeout()).
		SubmitCallback(nil, func(result zlink.RequestResult, parts []*zlink.Message) {
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
		})
	if err != nil {
		perfcommon.Must(err)
	}
	if !ok {
		perfcommon.Must(fmt.Errorf("multi spot reqrep ready probe backpressured"))
	}
	perfcommon.Must(<-ready)
}

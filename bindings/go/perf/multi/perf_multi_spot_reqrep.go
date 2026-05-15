package main

import (
	"fmt"
	"strings"
	"sync"
	"time"

	zlink "zlink.systems/zlink/contracts"
	"zlink.systems/zlink/perf/internal/perfcommon"
)

type multiSpotReqRepClient struct {
	ctx    *zlink.Context
	socket *zlink.RouterSocket
}

var (
	multiSpotReqRepNodeRID = zlink.NewRoutingID([]byte("SPOT-REQREP-SERVER-NODE"))
	multiSpotReqRepSpotRID = zlink.NewRoutingID([]byte("SPOT-REQREP-SERVER-SPOT"))
)

func runMultiSpotReqRep(cfg multiConfig) perfcommon.Result {
	replierCtx, err := perfcommon.NewMultiServerContext()
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
		clientCtx, clientErr := perfcommon.NewMultiClientContext()
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
					Submit(nil, func(result zlink.RequestResult, parts []*zlink.Message) {
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

func runMultiSpotReqRepServer(cfg multiConfig) {
	replierCtx, err := perfcommon.NewMultiServerContext()
	perfcommon.Must(err)
	defer replierCtx.Close()

	replierNode, err := replierCtx.SpotNode()
	perfcommon.Must(err)
	defer replierNode.Close()
	perfcommon.ApplyMultiSpotNodeAdmission(replierNode, cfg.pattern)
	perfcommon.Must(perfcommon.ConfigureTLSServer(replierNode, cfg.transport))
	perfcommon.Must(replierNode.SetRoutingID(multiSpotReqRepNodeRID))
	dataEndpoint := perfcommon.UniqueEndpoint(cfg.transport, "perf-multi-spot-reqrep")
	perfcommon.Must(replierNode.Bind(dataEndpoint))

	replier, err := replierNode.Spot()
	perfcommon.Must(err)
	defer replier.Close()
	perfcommon.ApplyMultiBenchmarkSocketOptions(replier, cfg.transport)
	perfcommon.Must(replier.SetRoutingID(multiSpotReqRepSpotRID))
	perfcommon.Must(replier.OnRoutedReceive(func(received *zlink.Received) {
		defer received.Close()
		parts := received.Parts()
		if len(parts) == 0 {
			return
		}
		reply := perfcommon.NewMessage(parts[0].Data())
		defer reply.Close()
		sourceRID := received.RoutingID()
		requestSeq := received.RequestSeq()
		spotRID := received.SpotRID()
		if spotRID.Size() == 0 {
			perfcommon.Must(replier.ReplyToRouter(sourceRID, requestSeq).Message(reply).Submit(nil))
			return
		}
		perfcommon.Must(replier.ReplyToSpot(sourceRID, spotRID, requestSeq).Message(reply).Submit(nil))
	}))

	controlNode, err := replierCtx.SpotNode()
	perfcommon.Must(err)
	defer controlNode.Close()
	perfcommon.ApplyMultiSpotNodeAdmission(controlNode, cfg.pattern)
	perfcommon.Must(perfcommon.ConfigureTLSServer(controlNode, cfg.transport))
	perfcommon.Must(perfcommon.ConfigureTLSClient(controlNode, cfg.transport))
	perfcommon.Must(controlNode.SetRoutingID(zlink.NewRoutingID([]byte("SPOT-REQREP-CONTROL-SERVER-NODE"))))
	controlPub, err := controlNode.Spot()
	perfcommon.Must(err)
	defer controlPub.Close()
	controlSub, err := controlNode.Spot()
	perfcommon.Must(err)
	defer controlSub.Close()
	perfcommon.Must(controlSub.SetSubscription(multiSpotTopic))
	controlEndpoint := perfcommon.UniqueEndpoint(cfg.transport, "perf-multi-spot-reqrep-control-server")
	perfcommon.Must(controlNode.Bind(controlEndpoint))
	controlEndpoint = spotNodeLastEndpoint(controlNode, controlEndpoint)

	var readyCount int
	dataConnected := false
	startRunner := false
	events := make(chan string, 16)
	go scanSpotRoleStdin(cfg, events)

	flushControlLine("READY,%s", dataEndpoint)
	flushControlLine("CONTROL_READY,%s", controlEndpoint)
	deadline := time.Now().Add(perfcommon.MultiReadyTimeout())
	for time.Now().Before(deadline) {
		select {
		case text := <-events:
			switch {
			case strings.HasPrefix(text, "CONNECT_CONTROL,"):
				endpoint := strings.TrimPrefix(text, "CONNECT_CONTROL,")
				perfcommon.Must(controlNode.ConnectPeer(endpoint))
				flushControlLine("CONTROL_CONNECTED,%s", endpoint)
			case text == fmt.Sprintf("START,%d", cfg.msgSize):
				startRunner = true
			case text == "STOP" || text == "QUIT":
				return
			}
		default:
		}
		payload := receiveSpotControlPayload(controlSub)
		switch {
		case strings.HasPrefix(payload, "DATA_ENDPOINT,"):
			endpoint := strings.TrimPrefix(payload, "DATA_ENDPOINT,")
			perfcommon.Must(replierNode.ConnectPeer(endpoint))
			dataConnected = true
		case payload == "CONNECTED":
		case strings.HasPrefix(payload, "READY_COUNT,"):
			fields := strings.Split(payload, ",")
			if len(fields) == 3 && fields[1] == fmt.Sprintf("%d", cfg.msgSize) {
				var count int
				_, _ = fmt.Sscanf(fields[2], "%d", &count)
				readyCount += count
			}
		}
		if dataConnected && readyCount >= cfg.clients {
			break
		}
		time.Sleep(time.Millisecond)
	}
	if !dataConnected || readyCount < cfg.clients {
		perfcommon.Must(fmt.Errorf("spot reqrep server readiness timeout"))
	}
	for !startRunner {
		event := <-events
		if event == fmt.Sprintf("START,%d", cfg.msgSize) {
			startRunner = true
			break
		}
		if event == "STOP" || event == "QUIT" {
			return
		}
	}
	if !publishSpotControlPayload(controlPub, fmt.Sprintf("START,%d", cfg.msgSize), perfcommon.MultiReadyTimeout()) {
		perfcommon.Must(fmt.Errorf("spot reqrep server direct start publish timeout"))
	}
	time.Sleep(cfg.duration + 2*time.Second)
}

func runMultiSpotReqRepClientRole(cfg multiConfig, endpoint string) perfcommon.Result {
	parts := strings.SplitN(endpoint, ",", 2)
	if len(parts) != 2 {
		perfcommon.Must(fmt.Errorf("spot reqrep client expects data_endpoint,control_endpoint"))
	}
	dataEndpoint, controlEndpoint := parts[0], parts[1]

	clientCtx, err := perfcommon.NewMultiClientContext()
	perfcommon.Must(err)
	defer clientCtx.Close()
	node, err := clientCtx.SpotNode()
	perfcommon.Must(err)
	defer node.Close()
	controlNode, err := clientCtx.SpotNode()
	perfcommon.Must(err)
	defer controlNode.Close()
	perfcommon.ApplyMultiSpotNodeAdmission(node, cfg.pattern)
	perfcommon.ApplyMultiSpotNodeAdmission(controlNode, cfg.pattern)
	perfcommon.Must(perfcommon.ConfigureTLSServer(node, cfg.transport))
	perfcommon.Must(perfcommon.ConfigureTLSClient(node, cfg.transport))
	perfcommon.Must(perfcommon.ConfigureTLSServer(controlNode, cfg.transport))
	perfcommon.Must(perfcommon.ConfigureTLSClient(controlNode, cfg.transport))
	perfcommon.Must(node.SetRoutingID(zlink.NewRoutingID([]byte("SPOT-REQREP-CLIENT-NODE"))))
	perfcommon.Must(controlNode.SetRoutingID(zlink.NewRoutingID([]byte("SPOT-REQREP-CONTROL-CLIENT-NODE"))))
	controlPub, err := controlNode.Spot()
	perfcommon.Must(err)
	defer controlPub.Close()
	controlSub, err := controlNode.Spot()
	perfcommon.Must(err)
	defer controlSub.Close()
	perfcommon.Must(controlSub.SetSubscription(multiSpotTopic))
	controlBind := perfcommon.UniqueEndpoint(cfg.transport, "perf-multi-spot-reqrep-control-client")
	perfcommon.Must(controlNode.Bind(controlBind))
	perfcommon.Must(controlNode.ConnectPeer(controlEndpoint))
	clientControlEndpoint := spotNodeLastEndpoint(controlNode, controlBind)
	flushControlLine("CLIENT_CONTROL_ENDPOINT,%s", clientControlEndpoint)

	localEndpoint := perfcommon.UniqueEndpoint(cfg.transport, "perf-multi-spot-reqrep-client")
	perfcommon.Must(node.Bind(localEndpoint))
	perfcommon.Must(node.ConnectPeer(dataEndpoint))

	spots := make([]*zlink.Spot, 0, cfg.clients)
	for i := 0; i < cfg.clients; i++ {
		spot, err := node.Spot()
		perfcommon.Must(err)
		perfcommon.Must(spot.SetRoutingID(zlink.NewRoutingID([]byte(fmt.Sprintf("spot-req-client-spot-%06d", i)))))
		spots = append(spots, spot)
	}
	defer func() {
		for _, spot := range spots {
			_ = spot.Close()
		}
	}()

	events := make(chan string, 16)
	go scanSpotRoleStdin(cfg, events)
	waitForSpotRoleEvent(events, "CONTROL_CONNECTED,")
	time.Sleep(perfcommon.MultiSpotReadySettleDuration())
	time.Sleep(perfcommon.MultiSpotControlSettleDuration())
	if !publishSpotControlPayload(controlPub, fmt.Sprintf("DATA_ENDPOINT,%s", localEndpoint), perfcommon.MultiReadyTimeout()) {
		perfcommon.Must(fmt.Errorf("spot reqrep client data endpoint publish timeout"))
	}
	time.Sleep(perfcommon.MultiSpotControlSettleDuration())
	waitMultiSpotReqRepSpotReady(spots[0], cfg.msgSize)
	if !publishSpotControlPayload(controlPub, "CONNECTED", perfcommon.MultiReadyTimeout()) {
		perfcommon.Must(fmt.Errorf("spot reqrep client connected publish timeout"))
	}
	time.Sleep(perfcommon.MultiSpotControlSettleDuration())
	if !publishSpotControlPayload(controlPub, fmt.Sprintf("READY_COUNT,%d,%d", cfg.msgSize, cfg.clients), perfcommon.MultiReadyTimeout()) {
		perfcommon.Must(fmt.Errorf("spot reqrep client ready publish timeout"))
	}
	flushControlLine("CLIENT_READY,%d", cfg.msgSize)
	waitForSpotRoleEvent(events, fmt.Sprintf("START,%d", cfg.msgSize))
	waitForSpotControlStart(controlSub, cfg.msgSize)

	stats := perfcommon.NewStats()
	window := activeDeadline(cfg.duration)
	payloads := make([][]byte, len(spots))
	for i := range payloads {
		payloads[i] = perfcommon.PreparePayload(cfg.msgSize)
	}
	for time.Now().Before(window.StopAt) {
		for i, spot := range spots {
			perfcommon.StampPayload(payloads[i])
			data := requestSpotReply(spot, payloads[i], 200*time.Millisecond)
			if data == nil {
				continue
			}
			if sentAt, ok := perfcommon.SentAtFromBytes(data, cfg.msgSize); ok && time.Now().Before(window.StopAt) {
				stats.AddLatencyNs(float64(time.Since(sentAt).Nanoseconds()) / 2.0)
			}
		}
	}
	return stats.Snapshot(cfg.duration, cfg.msgSize)
}

func waitMultiSpotReqRepSpotReady(spot *zlink.Spot, msgSize int) {
	payload := perfcommon.PreparePayload(msgSize)
	perfcommon.StampProbePayload(payload)
	if requestSpotReply(spot, payload, perfcommon.MultiReadyTimeout()) == nil {
		perfcommon.Must(fmt.Errorf("multi spot reqrep ready probe failed"))
	}
}

func requestSpotReply(spot *zlink.Spot, payload []byte, timeout time.Duration) []byte {
	done := make(chan []*zlink.Message, 1)
	ok, err := spot.RequestToSpot(multiSpotReqRepNodeRID, multiSpotReqRepSpotRID).
		Message(perfcommon.NewMessage(payload)).
		Flags(zlink.SendFlagsDontWait).
		Timeout(timeout).
		Submit(nil, func(result zlink.RequestResult, parts []*zlink.Message) {
			if result != zlink.RequestOK || len(parts) == 0 {
				done <- nil
				return
			}
			done <- parts
		})
	if err != nil {
		if perfcommon.IsTransient(err) {
			return nil
		}
		perfcommon.Must(err)
	}
	if !ok {
		return nil
	}
	select {
	case parts := <-done:
		defer func() {
			for _, part := range parts {
				_ = part.Close()
			}
		}()
		if len(parts) == 0 || parts[0] == nil {
			return nil
		}
		return append([]byte(nil), parts[0].Data()...)
	case <-time.After(timeout + 100*time.Millisecond):
		return nil
	}
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
		Submit(nil, func(result zlink.RequestResult, parts []*zlink.Message) {
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

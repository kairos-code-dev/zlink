package main

import (
	"bufio"
	"fmt"
	"net"
	"strings"
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

	controlListener, controlEndpoint := listenControlEndpoint()
	defer controlListener.Close()
	var mu sync.Mutex
	var readyCount int
	var readyConn net.Conn
	startRunner := false
	controlConnected := false
	dataConnected := false
	stop := false
	startConnCh := make(chan net.Conn, 1)
	go func() {
		conn, acceptErr := controlListener.Accept()
		if acceptErr == nil {
			startConnCh <- conn
		}
	}()
	go func() {
		stdin := bufio.NewScanner(newStdinReader())
		for stdin.Scan() {
			text := strings.TrimSpace(stdin.Text())
			switch {
			case strings.HasPrefix(text, "CONNECT_CONTROL,"):
				endpoint := strings.TrimPrefix(text, "CONNECT_CONTROL,")
				conn := dialControlEndpoint(endpoint, perfcommon.MultiReadyTimeout())
				mu.Lock()
				readyConn = conn
				controlConnected = true
				mu.Unlock()
				flushControlLine("CONTROL_CONNECTED,%s", endpoint)
				go readSpotServerControl(conn, cfg, replierNode, &mu, &readyCount, &dataConnected, &controlConnected)
			case text == fmt.Sprintf("START,%d", cfg.msgSize):
				mu.Lock()
				startRunner = true
				mu.Unlock()
			case text == "STOP" || text == "QUIT":
				mu.Lock()
				stop = true
				mu.Unlock()
				return
			}
		}
	}()

	flushControlLine("READY,%s", dataEndpoint)
	flushControlLine("CONTROL_READY,%s", controlEndpoint)
	deadline := time.Now().Add(perfcommon.MultiReadyTimeout())
	var startConn net.Conn
	for time.Now().Before(deadline) {
		select {
		case startConn = <-startConnCh:
		default:
		}
		mu.Lock()
		ready := controlConnected && dataConnected && startRunner && readyCount >= cfg.clients && startConn != nil && !stop && readyConn != nil
		stopped := stop
		mu.Unlock()
		if stopped {
			return
		}
		if ready {
			break
		}
		time.Sleep(time.Millisecond)
	}
	if startConn == nil {
		perfcommon.Must(fmt.Errorf("spot reqrep server control accept timeout"))
	}
	writeControlLine(startConn, "START,%d", cfg.msgSize)
	time.Sleep(cfg.duration + 2*time.Second)
}

func runMultiSpotReqRepClientRole(cfg multiConfig, endpoint string) perfcommon.Result {
	parts := strings.SplitN(endpoint, ",", 2)
	if len(parts) != 2 {
		perfcommon.Must(fmt.Errorf("spot reqrep client expects data_endpoint,control_endpoint"))
	}
	dataEndpoint, controlEndpoint := parts[0], parts[1]

	controlListener, clientControlEndpoint := listenControlEndpoint()
	defer controlListener.Close()
	flushControlLine("CLIENT_CONTROL_ENDPOINT,%s", clientControlEndpoint)

	var mu sync.Mutex
	runnerStart := false
	runnerControlConnected := false
	directStart := false
	directStartSize := 0
	readyConnCh := make(chan net.Conn, 1)
	go func() {
		conn, acceptErr := controlListener.Accept()
		if acceptErr == nil {
			readyConnCh <- conn
		}
	}()
	startConn := dialControlEndpoint(controlEndpoint, perfcommon.MultiReadyTimeout())
	defer startConn.Close()
	go readSpotClientStart(startConn, &mu, &directStart, &directStartSize)
	go readSpotClientStdin(cfg, &mu, &runnerStart, &runnerControlConnected)

	clientCtx, err := perfcommon.NewMultiClientContext()
	perfcommon.Must(err)
	defer clientCtx.Close()
	node, err := clientCtx.SpotNode()
	perfcommon.Must(err)
	defer node.Close()
	perfcommon.ApplyMultiSpotNodeAdmission(node, cfg.pattern)
	perfcommon.Must(perfcommon.ConfigureTLSClient(node, cfg.transport))
	perfcommon.Must(node.SetRoutingID(zlink.NewRoutingID([]byte("SPOT-REQREP-CLIENT-NODE"))))
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

	readyConn := waitSpotClientControlReady(readyConnCh, &mu, &runnerControlConnected)
	defer readyConn.Close()
	time.Sleep(perfcommon.MultiSpotReadySettleDuration())
	time.Sleep(perfcommon.MultiSpotControlSettleDuration())
	writeControlLine(readyConn, "DATA_ENDPOINT,%s", localEndpoint)
	time.Sleep(perfcommon.MultiSpotControlSettleDuration())
	writeControlLine(readyConn, "CONNECTED")
	waitMultiSpotReqRepSpotReady(spots[0], cfg.msgSize)
	writeControlLine(readyConn, "READY_COUNT,%d,%d", cfg.msgSize, cfg.clients)
	flushControlLine("CLIENT_READY,%d", cfg.msgSize)
	waitSpotClientStartBarrier(cfg, &mu, &runnerStart, &directStart, &directStartSize)

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

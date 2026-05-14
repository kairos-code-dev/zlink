package main

import (
	"bufio"
	"errors"
	"fmt"
	"net"
	"strconv"
	"strings"
	"sync"
	"time"

	"zlink.systems/zlink"
	"zlink.systems/zlink/perf/internal/perfcommon"
)

type multiSpotSendSendClient struct {
	ctx          *zlink.Context
	node         *zlink.SpotNode
	spot         *zlink.Spot
	payload      []byte
	waitingReply bool
}

var (
	multiSpotSendSendNodeRID = zlink.NewRoutingID([]byte("SPOT-SENDSEND-SERVER-NODE"))
	multiSpotSendSendSpotRID = zlink.NewRoutingID([]byte("SPOT-SENDSEND-SERVER-SPOT"))
)

func runMultiSpotSendSend(cfg multiConfig) perfcommon.Result {
	serverCtx, err := perfcommon.NewMultiServerContext()
	perfcommon.Must(err)
	defer serverCtx.Close()

	serverNode, err := serverCtx.SpotNode()
	perfcommon.Must(err)
	defer serverNode.Close()
	perfcommon.ApplyMultiSpotNodeAdmission(serverNode, cfg.pattern)
	perfcommon.Must(perfcommon.ConfigureTLSServer(serverNode, cfg.transport))
	perfcommon.Must(serverNode.SetRoutingID(multiSpotSendSendNodeRID))

	replier, err := serverNode.Spot()
	perfcommon.Must(err)
	defer replier.Close()
	perfcommon.ApplyMultiBenchmarkSocketOptions(replier, cfg.transport)
	perfcommon.Must(replier.SetRoutingID(multiSpotSendSendSpotRID))
	endpoint := perfcommon.UniqueEndpoint(cfg.transport, "perf-multi-spot-sendsend")
	perfcommon.Must(serverNode.Bind(endpoint))

	stopServer := make(chan struct{})
	defer close(stopServer)
	go func() {
		for {
			select {
			case <-stopServer:
				return
			default:
			}
			drainMultiSpotSendSendServer(replier)
			time.Sleep(time.Millisecond)
		}
	}()

	clients := make([]multiSpotSendSendClient, 0, cfg.clients)
	for i := 0; i < cfg.clients; i++ {
		clientCtx, clientErr := perfcommon.NewMultiClientContext()
		perfcommon.Must(clientErr)
		clientNode, nodeErr := clientCtx.SpotNode()
		perfcommon.Must(nodeErr)
		perfcommon.ApplyMultiSpotNodeAdmission(clientNode, cfg.pattern)
		perfcommon.Must(perfcommon.ConfigureTLSClient(clientNode, cfg.transport))
		perfcommon.Must(clientNode.SetRoutingID(zlink.NewRoutingID([]byte(fmt.Sprintf("SPOT-SENDSEND-CLIENT-NODE-%06d", i)))))
		clientEndpoint := perfcommon.UniqueEndpoint(cfg.transport, fmt.Sprintf("perf-multi-spot-sendsend-client-%06d", i))
		perfcommon.Must(clientNode.Bind(clientEndpoint))
		perfcommon.Must(clientNode.ConnectPeer(endpoint))
		perfcommon.Must(serverNode.ConnectPeer(clientEndpoint))

		spot, spotErr := clientNode.Spot()
		perfcommon.Must(spotErr)
		perfcommon.ApplyMultiBenchmarkSocketOptions(spot, cfg.transport)
		perfcommon.Must(spot.SetRoutingID(zlink.NewRoutingID([]byte(fmt.Sprintf("SPOT-SENDSEND-%06d", i)))))
		client := multiSpotSendSendClient{
			ctx:     clientCtx,
			node:    clientNode,
			spot:    spot,
			payload: perfcommon.PreparePayload(cfg.msgSize),
		}
		waitMultiSpotSendSendReady(client.spot, cfg.msgSize)
		clients = append(clients, client)
	}
	defer func() {
		for _, client := range clients {
			_ = client.spot.Close()
			_ = client.node.Close()
			_ = client.ctx.Close()
		}
	}()

	stats := perfcommon.NewStats()
	window := perfcommon.NewBenchmarkWindow(cfg.duration)
	for time.Now().Before(window.StopAt) {
		progressed := false
		for i := range clients {
			client := &clients[i]
			if client.waitingReply {
				if drainMultiSpotSendSend(client.spot, cfg.msgSize, window.StopAt, stats, true) {
					client.waitingReply = false
					progressed = true
				}
				continue
			}
			perfcommon.StampPayload(client.payload)
			if submitMultiSpotSendSend(client.spot, client.payload) {
				client.waitingReply = true
				progressed = true
				continue
			}
			if drainMultiSpotSendSend(client.spot, cfg.msgSize, window.StopAt, stats, true) {
				client.waitingReply = false
				progressed = true
			}
		}
		if !progressed {
			time.Sleep(time.Millisecond)
		}
	}
	return stats.Snapshot(cfg.duration, cfg.msgSize)
}

func runMultiSpotSendSendServer(cfg multiConfig) {
	serverCtx, err := perfcommon.NewMultiServerContext()
	perfcommon.Must(err)
	defer serverCtx.Close()

	serverNode, err := serverCtx.SpotNode()
	perfcommon.Must(err)
	defer serverNode.Close()
	perfcommon.ApplyMultiSpotNodeAdmission(serverNode, cfg.pattern)
	perfcommon.Must(perfcommon.ConfigureTLSServer(serverNode, cfg.transport))
	perfcommon.Must(serverNode.SetRoutingID(multiSpotSendSendNodeRID))

	replier, err := serverNode.Spot()
	perfcommon.Must(err)
	defer replier.Close()
	perfcommon.ApplyMultiBenchmarkSocketOptions(replier, cfg.transport)
	perfcommon.Must(replier.SetRoutingID(multiSpotSendSendSpotRID))

	dataEndpoint := perfcommon.UniqueEndpoint(cfg.transport, "perf-multi-spot-sendsend")
	perfcommon.Must(serverNode.Bind(dataEndpoint))
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
				go readSpotServerControl(conn, cfg, serverNode, &mu, &readyCount, &dataConnected, &controlConnected)
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
		drainMultiSpotSendSendServer(replier)
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
		perfcommon.Must(fmt.Errorf("spot sendsend server control accept timeout"))
	}
	writeControlLine(startConn, "START,%d", cfg.msgSize)
	idleDeadline := time.Now().Add(cfg.duration + 2*time.Second)
	for time.Now().Before(idleDeadline) {
		mu.Lock()
		stopped := stop
		mu.Unlock()
		if stopped {
			break
		}
		drainMultiSpotSendSendServer(replier)
		time.Sleep(time.Millisecond)
	}
}

func runMultiSpotSendSendClientRole(cfg multiConfig, endpoint string) perfcommon.Result {
	parts := strings.SplitN(endpoint, ",", 2)
	if len(parts) != 2 {
		perfcommon.Must(fmt.Errorf("spot sendsend client expects data_endpoint,control_endpoint"))
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
	go func() {
		scanner := bufio.NewScanner(startConn)
		for scanner.Scan() {
			text := strings.TrimSpace(scanner.Text())
			if strings.HasPrefix(text, "START,") {
				size, parseErr := strconv.Atoi(strings.TrimPrefix(text, "START,"))
				if parseErr == nil {
					mu.Lock()
					directStart = true
					directStartSize = size
					mu.Unlock()
				}
			}
		}
	}()
	go func() {
		stdin := bufio.NewScanner(newStdinReader())
		for stdin.Scan() {
			text := strings.TrimSpace(stdin.Text())
			if text == fmt.Sprintf("START,%d", cfg.msgSize) {
				mu.Lock()
				runnerStart = true
				mu.Unlock()
			} else if strings.HasPrefix(text, "CONTROL_CONNECTED,") {
				mu.Lock()
				runnerControlConnected = true
				mu.Unlock()
			}
		}
	}()

	clientCtx, err := perfcommon.NewMultiClientContext()
	perfcommon.Must(err)
	defer clientCtx.Close()
	clientNode, err := clientCtx.SpotNode()
	perfcommon.Must(err)
	defer clientNode.Close()
	perfcommon.ApplyMultiSpotNodeAdmission(clientNode, cfg.pattern)
	perfcommon.Must(perfcommon.ConfigureTLSClient(clientNode, cfg.transport))
	perfcommon.Must(clientNode.SetRoutingID(zlink.NewRoutingID([]byte("SPOT-SENDSEND-CLIENT-NODE"))))
	localEndpoint := perfcommon.UniqueEndpoint(cfg.transport, "perf-multi-spot-sendsend-client")
	perfcommon.Must(clientNode.Bind(localEndpoint))
	perfcommon.Must(clientNode.ConnectPeer(dataEndpoint))

	clients := make([]multiSpotSendSendClient, 0, cfg.clients)
	for i := 0; i < cfg.clients; i++ {
		spot, spotErr := clientNode.Spot()
		perfcommon.Must(spotErr)
		perfcommon.ApplyMultiBenchmarkSocketOptions(spot, cfg.transport)
		perfcommon.Must(spot.SetRoutingID(zlink.NewRoutingID([]byte(fmt.Sprintf("SPOT-SENDSEND-%06d", i)))))
		clients = append(clients, multiSpotSendSendClient{
			node:    clientNode,
			spot:    spot,
			payload: perfcommon.PreparePayload(cfg.msgSize),
		})
	}
	defer func() {
		for _, client := range clients {
			_ = client.spot.Close()
		}
	}()

	var readyConn net.Conn
	deadline := time.Now().Add(perfcommon.MultiReadyTimeout())
	for time.Now().Before(deadline) {
		select {
		case readyConn = <-readyConnCh:
		default:
		}
		mu.Lock()
		ok := readyConn != nil && runnerControlConnected
		mu.Unlock()
		if ok {
			break
		}
		time.Sleep(time.Millisecond)
	}
	if readyConn == nil {
		perfcommon.Must(fmt.Errorf("spot sendsend client control accept timeout"))
	}
	defer readyConn.Close()

	time.Sleep(perfcommon.MultiSpotReadySettleDuration())
	time.Sleep(perfcommon.MultiSpotControlSettleDuration())
	writeControlLine(readyConn, "DATA_ENDPOINT,%s", localEndpoint)
	time.Sleep(perfcommon.MultiSpotControlSettleDuration())
	writeControlLine(readyConn, "CONNECTED")
	waitMultiSpotSendSendReady(clients[0].spot, cfg.msgSize)
	writeControlLine(readyConn, "READY_COUNT,%d,%d", cfg.msgSize, cfg.clients)
	flushControlLine("CLIENT_READY,%d", cfg.msgSize)

	startDeadline := time.Now().Add(perfcommon.MultiReadyTimeout())
	for time.Now().Before(startDeadline) {
		mu.Lock()
		ok := runnerStart && directStart && directStartSize == cfg.msgSize
		mu.Unlock()
		if ok {
			break
		}
		time.Sleep(time.Millisecond)
	}
	mu.Lock()
	startOK := runnerStart && directStart && directStartSize == cfg.msgSize
	mu.Unlock()
	if !startOK {
		perfcommon.Must(fmt.Errorf("spot sendsend start handshake timeout"))
	}

	stats := perfcommon.NewStats()
	window := activeDeadline(cfg.duration)
	for time.Now().Before(window.StopAt) {
		progressed := false
		for i := range clients {
			client := &clients[i]
			if client.waitingReply {
				if drainMultiSpotSendSend(client.spot, cfg.msgSize, window.StopAt, stats, true) {
					client.waitingReply = false
					progressed = true
				}
				continue
			}
			perfcommon.StampPayload(client.payload)
			if submitMultiSpotSendSend(client.spot, client.payload) {
				client.waitingReply = true
				progressed = true
				continue
			}
			if drainMultiSpotSendSend(client.spot, cfg.msgSize, window.StopAt, stats, true) {
				client.waitingReply = false
				progressed = true
			}
		}
		if !progressed {
			time.Sleep(time.Millisecond)
		}
	}
	return stats.Snapshot(cfg.duration, cfg.msgSize)
}

func readSpotServerControl(
	conn net.Conn,
	cfg multiConfig,
	node *zlink.SpotNode,
	mu *sync.Mutex,
	readyCount *int,
	dataConnected *bool,
	controlConnected *bool,
) {
	scanner := bufio.NewScanner(conn)
	for scanner.Scan() {
		text := strings.TrimSpace(scanner.Text())
		switch {
		case text == "CONNECTED":
			mu.Lock()
			*controlConnected = true
			mu.Unlock()
		case strings.HasPrefix(text, "DATA_ENDPOINT,"):
			endpoint := strings.TrimPrefix(text, "DATA_ENDPOINT,")
			perfcommon.Must(node.ConnectPeer(endpoint))
			mu.Lock()
			*dataConnected = true
			mu.Unlock()
		case strings.HasPrefix(text, "READY_COUNT,"):
			fields := strings.Split(text, ",")
			if len(fields) == 3 {
				size, sizeErr := strconv.Atoi(fields[1])
				count, countErr := strconv.Atoi(fields[2])
				if sizeErr == nil && countErr == nil && size == cfg.msgSize {
					mu.Lock()
					*readyCount += count
					mu.Unlock()
				}
			}
		}
	}
}

func submitMultiSpotSendSend(spot *zlink.Spot, payload []byte) bool {
	message := perfcommon.NewMessage(payload)
	defer message.Close()
	sent, err := spot.SendToSpot(multiSpotSendSendNodeRID, multiSpotSendSendSpotRID).
		Message(message).
		Flags(zlink.SendFlagsDontWait).
		Submit(nil)
	if err != nil {
		if perfcommon.IsTransient(err) {
			return false
		}
		perfcommon.Must(err)
	}
	return sent
}

func drainMultiSpotSendSendServer(replier *zlink.Spot) {
	for {
		received, err := replier.RecvRouted(zlink.RecvFlagsDontWait)
		if err != nil {
			var recvErr *zlink.RecvError
			if errors.As(err, &recvErr) && recvErr.Result == zlink.RecvNoData {
				return
			}
			perfcommon.Must(err)
		}
		if received == nil {
			return
		}
		if received.HasRequestSeq() {
			perfcommon.Must(received.Close())
			continue
		}
		parts := received.Parts()
		if len(parts) > 0 {
			reply := perfcommon.NewMessage(parts[0].Data())
			sent, sendErr := received.Send().Message(reply).Flags(zlink.SendFlagsDontWait).Submit(nil)
			if sendErr != nil && !perfcommon.IsTransient(sendErr) {
				perfcommon.Must(sendErr)
			}
			_ = sent
			perfcommon.Must(reply.Close())
		}
		perfcommon.Must(received.Close())
	}
}

func drainMultiSpotSendSend(
	spot *zlink.Spot,
	msgSize int,
	activeStopAt time.Time,
	stats *perfcommon.Stats,
	record bool,
) bool {
	progressed := false
	for {
		received, err := spot.RecvRouted(zlink.RecvFlagsDontWait)
		if err != nil {
			var recvErr *zlink.RecvError
			if errors.As(err, &recvErr) && recvErr.Result == zlink.RecvNoData {
				return progressed
			}
			perfcommon.Must(err)
		}
		progressed = true
		if received == nil {
			return progressed
		}
		parts := received.Parts()
		if record && time.Now().Before(activeStopAt) && !received.HasRequestSeq() && len(parts) > 0 {
			if sentAt, ok := perfcommon.SentAtFromMessage(parts[0], msgSize); ok {
				stats.AddLatencyNs(float64(time.Since(sentAt).Nanoseconds()) / 2.0)
			}
		}
		perfcommon.Must(received.Close())
	}
}

func waitMultiSpotSendSendReady(spot *zlink.Spot, msgSize int) {
	payload := perfcommon.PreparePayload(msgSize)
	perfcommon.StampProbePayload(payload)
	deadline := time.Now().Add(perfcommon.MultiReadyTimeout())
	waitingReply := false
	for time.Now().Before(deadline) {
		if !waitingReply && submitMultiSpotSendSend(spot, payload) {
			waitingReply = true
		}
		if drainMultiSpotSendSend(spot, msgSize, deadline, perfcommon.NewStats(), false) {
			return
		}
		time.Sleep(time.Millisecond)
	}
	perfcommon.Must(fmt.Errorf("multi spot sendsend ready probe timed out"))
}

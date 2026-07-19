package main

import (
	"bufio"
	"fmt"
	"os"
	"runtime"
	"sort"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	zlink "zlink.systems/zlink"
	"zlink.systems/zlink/perf/internal/perfcommon"
)

const multiSpotTopic = "bench"

func runMultiSpotServer(cfg multiConfig) {
	ctx, err := perfcommon.NewMultiServerContext()
	perfcommon.Must(err)
	defer ctx.Close()
	perfcommon.ApplyMultiAutoHWMMsgUnit(ctx, cfg.msgSize)

	dataNode, err := ctx.SpotNode()
	perfcommon.Must(err)
	defer dataNode.Close()
	controlNode, err := ctx.SpotNode()
	perfcommon.Must(err)
	defer controlNode.Close()
	perfcommon.ApplyMultiSpotNodeAdmission(dataNode, cfg.pattern)
	perfcommon.ApplyMultiSpotNodeAdmission(controlNode, cfg.pattern)
	perfcommon.Must(perfcommon.ConfigureTLSServer(dataNode, cfg.transport))
	perfcommon.Must(perfcommon.ConfigureTLSServer(controlNode, cfg.transport))
	perfcommon.Must(perfcommon.ConfigureTLSClient(controlNode, cfg.transport))
	perfcommon.Must(dataNode.SetRoutingID(zlink.NewRoutingID([]byte("z-go-multi-spot-server"))))
	perfcommon.Must(controlNode.SetRoutingID(zlink.NewRoutingID([]byte("z-go-multi-spot-control-server"))))
	dataSpot, err := dataNode.Spot()
	perfcommon.Must(err)
	defer dataSpot.Close()
	perfcommon.Must(dataSpot.SetSendTimeout(perfcommon.MultiSendTimeout()))
	controlPub, err := controlNode.Spot()
	perfcommon.Must(err)
	defer controlPub.Close()
	controlSub, err := controlNode.Spot()
	perfcommon.Must(err)
	defer controlSub.Close()
	perfcommon.Must(controlSub.SetSubscription(multiSpotTopic))
	controlPoller := perfcommon.NewSocketPoller(controlSub, perfcommon.ZLinkPollIn)
	defer controlPoller.Close()
	controlEvents := make([]zlink.PollEvent, 1)

	dataEndpoint := perfcommon.UniqueEndpoint(cfg.transport, "perf-multi-spot-data")
	controlEndpoint := perfcommon.UniqueEndpoint(cfg.transport, "perf-multi-spot-control-server")
	perfcommon.Must(dataNode.SetPubBind(dataEndpoint))
	perfcommon.Must(controlNode.SetPubBind(controlEndpoint))
	dataEndpoint = spotNodeLastEndpoint(dataNode, dataEndpoint)
	controlEndpoint = spotNodeLastEndpoint(controlNode, controlEndpoint)
	flushControlLine("READY,%s", dataEndpoint)
	flushControlLine("CONTROL_READY,%s", controlEndpoint)

	events := make(chan string, 16)
	go scanSpotRoleStdin(cfg, events)
	readyCount := 0
	readyTimeout := multiSpotEchoServerReadyTimeout()
	deadline := time.Now().Add(readyTimeout)
	for time.Now().Before(deadline) {
		select {
		case event := <-events:
			switch {
			case strings.HasPrefix(event, "CONNECT_CONTROL,"):
				endpoint := strings.TrimPrefix(event, "CONNECT_CONTROL,")
				perfcommon.Must(controlNode.ConnectPeer(endpoint))
				waitMultiSpotReqRepConnectedPeer(controlNode, readyTimeout)
				flushControlLine("CONTROL_CONNECTED,%s", endpoint)
			case event == fmt.Sprintf("START,%d", cfg.msgSize):
			case event == "STOP" || event == "QUIT":
				return
			}
		default:
		}
		payload := receiveSpotControlPayload(controlSub)
		if strings.HasPrefix(payload, "READY_COUNT,") {
			fields := strings.Split(payload, ",")
			if len(fields) == 3 && fields[1] == fmt.Sprintf("%d", cfg.msgSize) {
				var count int
				_, _ = fmt.Sscanf(fields[2], "%d", &count)
				readyCount += count
			}
		}
		if readyCount >= cfg.clients {
			break
		}
		waitSpotControlReadable(controlPoller, controlEvents, deadline)
	}
	if readyCount < cfg.clients {
		perfcommon.Must(fmt.Errorf("spot server readiness timeout"))
	}
	for {
		event := <-events
		if event == fmt.Sprintf("START,%d", cfg.msgSize) {
			break
		}
		if event == "STOP" || event == "QUIT" {
			return
		}
	}
	if !publishSpotControlPayload(controlPub, fmt.Sprintf("START,%d", cfg.msgSize), perfcommon.MultiReadyTimeout()) {
		perfcommon.Must(fmt.Errorf("spot server direct start publish timeout"))
	}
	stopAt := time.Now().Add(cfg.duration)
	payload := perfcommon.PreparePayload(cfg.msgSize)
	for time.Now().Before(stopAt) {
		var err error
		if useMultiSpotBytesPublish(cfg.transport, cfg.msgSize) {
			perfcommon.StampWindowPayload(payload, time.Time{})
			_, err = dataSpot.Publish(multiSpotTopic).Bytes(payload).Flags(zlink.SendFlagsDontWait).Submit(nil)
		} else {
			_, err = perfcommon.SubmitWindowPayload(cfg.msgSize, time.Time{}, func(message *zlink.Message) (bool, error) {
				return dataSpot.Publish(multiSpotTopic).Message(message).Flags(zlink.SendFlagsDontWait).Submit(nil)
			})
		}
		if err != nil && !perfcommon.IsTransient(err) {
			perfcommon.Must(err)
		}
	}
	// perf_multi_spot_server.cpp end-of-active: signal phase end on the
	// data topic (the C server uses a cooldown-phase header; Go uses the
	// shared wire-level stop token) so the client recv-drain exits
	// deterministically instead of relying purely on its own clock.
	sendMultiSpotStopToken(dataSpot)
}

func useMultiSpotBytesPublish(transport string, msgSize int) bool {
	return (transport == "wss" && msgSize <= 1024) || (transport == "tls" && msgSize == 64)
}

// sendMultiSpotStopToken publishes the wire-level stop token on the
// data topic with bounded retry through transient backpressure.
func sendMultiSpotStopToken(spot *zlink.Spot) {
	for attempt := 0; attempt < perfcommon.StopTokenSendAttempts; attempt++ {
		sent, err := perfcommon.SubmitPayload(perfcommon.StopToken, func(message *zlink.Message) (bool, error) {
			return spot.Publish(multiSpotTopic).Message(message).Flags(zlink.SendFlagsDontWait).Submit(nil)
		})
		if err == nil && sent {
			return
		}
		if err != nil && !perfcommon.IsTransient(err) {
			return
		}
		perfcommon.PollIdle(perfcommon.StopTokenSendBackoff)
	}
}

func runMultiSpotClient(cfg multiConfig, endpoint string) perfcommon.Result {
	parts := strings.SplitN(endpoint, ",", 2)
	if len(parts) != 2 {
		perfcommon.Must(fmt.Errorf("spot client expects data_endpoint,control_endpoint"))
	}
	dataEndpoint, controlEndpoint := parts[0], parts[1]
	ctx, err := perfcommon.NewMultiClientContext()
	perfcommon.Must(err)
	defer ctx.Close()
	perfcommon.ApplyMultiAutoHWMMsgUnit(ctx, cfg.msgSize)
	dataNode, err := ctx.SpotNode()
	perfcommon.Must(err)
	defer dataNode.Close()
	controlNode, err := ctx.SpotNode()
	perfcommon.Must(err)
	defer controlNode.Close()
	perfcommon.ApplyMultiSpotNodeAdmission(dataNode, cfg.pattern)
	perfcommon.ApplyMultiSpotNodeAdmission(controlNode, cfg.pattern)
	perfcommon.Must(perfcommon.ConfigureTLSClient(dataNode, cfg.transport))
	perfcommon.Must(perfcommon.ConfigureTLSServer(controlNode, cfg.transport))
	perfcommon.Must(perfcommon.ConfigureTLSClient(controlNode, cfg.transport))
	controlPub, err := controlNode.Spot()
	perfcommon.Must(err)
	defer controlPub.Close()
	controlSub, err := controlNode.Spot()
	perfcommon.Must(err)
	defer controlSub.Close()
	perfcommon.Must(controlSub.SetSubscription(multiSpotTopic))
	controlPoller := perfcommon.NewSocketPoller(controlSub, perfcommon.ZLinkPollIn)
	defer controlPoller.Close()
	controlEvents := make([]zlink.PollEvent, 1)
	controlBind := perfcommon.UniqueEndpoint(cfg.transport, "perf-multi-spot-control-client")
	perfcommon.Must(controlNode.SetPubBind(controlBind))
	perfcommon.Must(controlNode.ConnectPeer(controlEndpoint))
	clientControlEndpoint := spotNodeLastEndpoint(controlNode, controlBind)
	flushControlLine("CLIENT_CONTROL_ENDPOINT,%s", clientControlEndpoint)

	perfcommon.Must(dataNode.ConnectPeer(dataEndpoint))
	spots := make([]*zlink.Spot, 0, cfg.clients)
	for i := 0; i < cfg.clients; i++ {
		spot, err := dataNode.Spot()
		perfcommon.Must(err)
		perfcommon.Must(spot.SetReceiveTimeout(perfcommon.MultiReceiveTimeout()))
		perfcommon.Must(spot.SetRoutingID(zlink.NewRoutingID([]byte(fmt.Sprintf("a-go-multi-spot-client-spot-%06d", i)))))
		perfcommon.Must(spot.SetSubscription(multiSpotTopic))
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
	if !publishSpotControlPayload(controlPub, fmt.Sprintf("READY_COUNT,%d,%d", cfg.msgSize, cfg.clients), perfcommon.MultiReadyTimeout()) {
		perfcommon.Must(fmt.Errorf("spot client ready publish timeout"))
	}
	flushControlLine("CLIENT_READY,%d", cfg.msgSize)
	waitForSpotRoleEvent(events, fmt.Sprintf("START,%d", cfg.msgSize))
	deadline := time.Now().Add(perfcommon.MultiReadyTimeout())
	for time.Now().Before(deadline) {
		if receiveSpotControlPayload(controlSub) == fmt.Sprintf("START,%d", cfg.msgSize) {
			break
		}
		waitSpotControlReadable(controlPoller, controlEvents, deadline)
	}
	stopAt := time.Now().Add(cfg.duration)
	slots := make([]multiSpotClientSlot, len(spots))
	for i, spot := range spots {
		part, err := zlink.NewMessageWithSize(0)
		perfcommon.Must(err)
		slots[i] = multiSpotClientSlot{
			spot:        spot,
			recvPart:    part,
			topicBuffer: make([]byte, 256),
		}
	}
	defer func() {
		for i := range slots {
			_ = slots[i].recvPart.Close()
		}
	}()
	// perf_multi_spot_client.cpp spot_client_recv_worker_loop /
	// drain_spot_client_slot: the SPOT handle has no poll fd (same as
	// single SPOT), so the C reference distributes slots over recv
	// workers. Each worker drains its assigned slots with DONTWAIT and
	// uses a 1ms idle yield when no slot progressed. Throughput counts all
	// active messages, while latency records the same stride sample as C.
	results := runMultiSpotRecvWorkers(slots, cfg.transport, cfg.msgSize, cfg.duration, stopAt)
	return mergeMultiSpotWorkerResults(results, cfg.duration, cfg.msgSize)
}

type multiSpotClientSlot struct {
	spot        *zlink.Spot
	recvPart    *zlink.Message
	topicBuffer []byte
	stop        bool
}

type multiSpotWorkerResult struct {
	count   uint64
	sumNs   float64
	samples []float64
}

func runMultiSpotRecvWorkers(slots []multiSpotClientSlot, transport string, msgSize int, duration time.Duration, stopAt time.Time) []multiSpotWorkerResult {
	workerCount := resolveMultiSpotRecvWorkerCount(len(slots), transport, msgSize)
	results := make([]multiSpotWorkerResult, workerCount)
	if workerCount == 0 {
		return results
	}
	errCh := make(chan error, workerCount)
	var stop atomic.Bool
	var wg sync.WaitGroup
	for workerID := 0; workerID < workerCount; workerID++ {
		workerID := workerID
		wg.Add(1)
		go func() {
			defer wg.Done()
			// Pin each recv worker to its OS thread: the worker's drain loop is
			// a tight cgo round-trip, and unpinned Ms pay Go-scheduler handoff
			// latency on every backpressure wakeup (see runMultiRole).
			runtime.LockOSThread()
			defer runtime.UnlockOSThread()
			result, err := runMultiSpotRecvWorker(slots, workerID, workerCount, msgSize, stopAt, &stop)
			results[workerID] = result
			if err != nil {
				stop.Store(true)
				errCh <- err
			}
		}()
	}
	wg.Wait()
	close(errCh)
	for err := range errCh {
		if err != nil {
			perfcommon.Must(err)
		}
	}
	return results
}

func runMultiSpotRecvWorker(slots []multiSpotClientSlot, workerID, workerCount, msgSize int, stopAt time.Time, stop *atomic.Bool) (multiSpotWorkerResult, error) {
	var result multiSpotWorkerResult
	latencyStride := resolveMultiSpotLatencySampleStride()
	for !stop.Load() && time.Now().Before(stopAt) {
		progressed := false
		activeSlots := 0
		for idx := workerID; idx < len(slots); idx += workerCount {
			slot := &slots[idx]
			if slot.stop {
				continue
			}
			activeSlots++
			for !stop.Load() && time.Now().Before(stopAt) {
				recv, ok, err := slot.spot.SubscribePart(slot.recvPart, slot.topicBuffer, zlink.RecvFlagsDontWait)
				if err != nil {
					if perfcommon.IsTransient(err) {
						break
					}
					return result, err
				}
				if !ok {
					break
				}
				progressed = true
				if recv.More || !multiTopicMatches(slot.topicBuffer, recv.TopicLen, multiSpotTopic) {
					return result, fmt.Errorf("multi spot unexpected part metadata[%d]: topic_len=%d more=%v", idx, recv.TopicLen, recv.More)
				}
				if perfcommon.IsStopTokenMessage(slot.recvPart) {
					slot.stop = true
					activeSlots--
					break
				}
				payload := slot.recvPart.Data()
				if !perfcommon.HasMetricHeaderPhase(payload, msgSize, perfcommon.PhaseActive) {
					continue
				}
				result.count++
				if shouldSampleMultiSpotLatency(result.count, latencyStride) {
					now := time.Now()
					if latencyNs, ok := perfcommon.LatencyNsFromBytesAt(payload, msgSize, perfcommon.PhaseActive, now); ok && now.Before(stopAt) {
						result.sumNs += latencyNs
						result.samples = append(result.samples, latencyNs)
					}
				}
			}
		}
		if activeSlots == 0 {
			break
		}
		if !progressed {
			perfcommon.PollIdle(time.Millisecond)
		}
	}
	return result, nil
}

func resolveMultiSpotRecvWorkerCount(slotCount int, transport string, msgSize int) int {
	if slotCount <= 0 {
		return 0
	}
	if configured := positiveIntEnv("PERF_MULTI_SPOT_RECV_WORKERS", 0); configured > 0 {
		if configured < slotCount {
			return configured
		}
		return slotCount
	}
	if useCompactMultiSpotRecvWorkers(transport, msgSize) {
		if slotCount < 4 {
			return slotCount
		}
		return 4
	}
	scaled := (slotCount + 15) / 16
	if scaled < 4 {
		scaled = 4
	}
	if scaled > 128 {
		scaled = 128
	}
	if scaled > slotCount {
		return slotCount
	}
	return scaled
}

func useCompactMultiSpotRecvWorkers(transport string, msgSize int) bool {
	return (transport == "wss" || transport == "tls") && msgSize <= 1024
}

func resolveMultiSpotLatencySampleStride() uint64 {
	return uint64(positiveIntEnv("PERF_MULTI_SPOT_LATENCY_SAMPLE_STRIDE", 32))
}

func positiveIntEnv(name string, fallback int) int {
	raw := os.Getenv(name)
	if raw == "" {
		return fallback
	}
	value, err := strconv.Atoi(raw)
	if err != nil || value <= 0 {
		return fallback
	}
	return value
}

func shouldSampleMultiSpotLatency(index, stride uint64) bool {
	return stride <= 1 || index == 1 || index%stride == 0
}

func mergeMultiSpotWorkerResults(results []multiSpotWorkerResult, duration time.Duration, msgSize int) perfcommon.Result {
	var count uint64
	var sumNs float64
	sampleCount := 0
	for _, result := range results {
		count += result.count
		sumNs += result.sumNs
		sampleCount += len(result.samples)
	}
	samples := make([]float64, 0, sampleCount)
	for _, result := range results {
		samples = append(samples, result.samples...)
	}
	sort.Float64s(samples)
	latencyMean := 0.0
	if len(samples) > 0 {
		latencyMean = sumNs / float64(len(samples))
	}
	return perfcommon.Result{
		Throughput:   float64(count) / duration.Seconds(),
		Bandwidth:    float64(count*uint64(msgSize)) / duration.Seconds() / 1_000_000.0,
		LatencyNs:    latencyMean,
		LatencyP95Ns: multiSpotPercentile(samples, 95),
		LatencyP99Ns: multiSpotPercentile(samples, 99),
		Valid:        count > 0 && len(samples) > 0,
	}
}

func multiSpotPercentile(sorted []float64, pct float64) float64 {
	if len(sorted) == 0 {
		return 0
	}
	rank := int((pct / 100.0) * float64(len(sorted)-1))
	if rank < 0 {
		rank = 0
	}
	if rank >= len(sorted) {
		rank = len(sorted) - 1
	}
	return sorted[rank]
}

func spotNodeLastEndpoint(node *zlink.SpotNode, fallback string) string {
	status, err := node.Status()
	if err == nil && status != nil && status.LocalEndpoint != "" {
		return status.LocalEndpoint
	}
	return fallback
}

func scanSpotRoleStdin(cfg multiConfig, events chan<- string) {
	scanner := bufio.NewScanner(newStdinReader())
	for scanner.Scan() {
		text := strings.TrimSpace(scanner.Text())
		if text != "" {
			events <- text
		}
	}
}

func waitForSpotRoleEvent(events <-chan string, prefix string) {
	deadline := time.After(perfcommon.MultiReadyTimeout())
	for {
		select {
		case event := <-events:
			if strings.HasPrefix(event, prefix) || event == prefix {
				return
			}
		case <-deadline:
			perfcommon.Must(fmt.Errorf("spot role event timeout: %s", prefix))
		}
	}
}

func waitForSpotControlStart(controlSub *zlink.Spot, msgSize int) {
	expected := fmt.Sprintf("START,%d", msgSize)
	deadline := time.Now().Add(perfcommon.MultiReadyTimeout())
	poller := perfcommon.NewSocketPoller(controlSub, perfcommon.ZLinkPollIn)
	defer poller.Close()
	events := make([]zlink.PollEvent, 1)
	for time.Now().Before(deadline) {
		if receiveSpotControlPayload(controlSub) == expected {
			return
		}
		waitSpotControlReadable(poller, events, deadline)
	}
	perfcommon.Must(fmt.Errorf("spot control start timeout: %s", expected))
}

func publishSpotControlPayload(spot *zlink.Spot, payload string, timeout time.Duration) bool {
	deadline := time.Now().Add(timeout)
	poller := perfcommon.NewSocketPoller(spot, perfcommon.ZLinkPollOut)
	defer poller.Close()
	events := make([]zlink.PollEvent, 1)
	for time.Now().Before(deadline) {
		sent, err := perfcommon.SubmitPayload([]byte(payload), func(message *zlink.Message) (bool, error) {
			return spot.Publish(multiSpotTopic).MoveMessage(message).Flags(zlink.SendFlagsDontWait).Submit(nil)
		})
		if err == nil && sent {
			return true
		}
		if err != nil && !perfcommon.IsTransient(err) {
			perfcommon.Must(err)
		}
		waitSpotWritable(poller, events, deadline)
	}
	return false
}

func waitSpotControlReadable(poller *zlink.Poller, events []zlink.PollEvent, deadline time.Time) {
	waitSpotPoller(poller, events, deadline, 50*time.Millisecond)
}

func waitSpotWritable(poller *zlink.Poller, events []zlink.PollEvent, deadline time.Time) {
	waitSpotPoller(poller, events, deadline, 10*time.Millisecond)
}

func waitSpotPoller(poller *zlink.Poller, events []zlink.PollEvent, deadline time.Time, maxWait time.Duration) {
	remaining := time.Until(deadline)
	if remaining <= 0 {
		return
	}
	if remaining > maxWait {
		remaining = maxWait
	}
	_, err := poller.Wait(events, remaining)
	perfcommon.Must(err)
}

func receiveSpotControlPayload(spot *zlink.Spot) string {
	var message zlink.TopicMessage
	ok, err := spot.Subscribe(&message, zlink.RecvFlagsDontWait)
	if err != nil {
		if perfcommon.IsTransient(err) {
			return ""
		}
		perfcommon.Must(err)
	}
	if !ok {
		return ""
	}
	defer message.Close()
	part, err := message.SinglePartOrError()
	if err != nil || part == nil {
		return ""
	}
	return string(part.Data())
}

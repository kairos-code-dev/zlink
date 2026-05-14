package main

import (
	"bufio"
	"fmt"
	"strings"
	"time"

	"zlink.systems/zlink"
	"zlink.systems/zlink/perf/internal/perfcommon"
)

type multiSpotSubscriber struct {
	spot *zlink.Spot
}

const multiSpotChannelName = "perf-spot-svc"
const multiSpotTopic = "bench"
const multiSpotMaxDrainPerSpot = 1024

func runMultiSpot(cfg multiConfig) perfcommon.Result {
	ctx, err := perfcommon.NewMultiContext()
	perfcommon.Must(err)
	defer ctx.Close()

	registry, err := ctx.Registry()
	perfcommon.Must(err)
	defer registry.Close()
	query, err := ctx.RegistryQueryClient()
	perfcommon.Must(err)
	defer query.Close()
	publisherNode, err := ctx.SpotNode()
	perfcommon.Must(err)
	defer publisherNode.Close()
	publisherDiscovery, err := ctx.Discovery(zlink.AutoConnectSpotMesh, multiSpotChannelName)
	perfcommon.Must(err)
	defer publisherDiscovery.Close()
	perfcommon.ApplyMultiSpotNodeAdmission(publisherNode, cfg.pattern)
	publisher, err := publisherNode.Spot()
	perfcommon.Must(err)
	defer publisher.Close()
	perfcommon.Must(publisher.SetNoDrop(true))
	perfcommon.ApplyMultiBenchmarkSocketOptions(publisher, cfg.transport)

	registryPubEndpoint := perfcommon.UniqueEndpoint(cfg.transport, "perf-multi-spot-registry-pub")
	registryRouterEndpoint := perfcommon.UniqueEndpoint(cfg.transport, "perf-multi-spot-registry-router")
	publisherEndpoint := perfcommon.UniqueEndpoint(cfg.transport, "perf-multi-spot-pub")
	perfcommon.Must(registry.Bind(registryPubEndpoint, registryRouterEndpoint))
	perfcommon.Must(query.Connect(registryRouterEndpoint))
	perfcommon.Must(publisherDiscovery.ConnectRegistry(registryRouterEndpoint))
	perfcommon.Must(publisherNode.AttachDiscovery(publisherDiscovery))
	perfcommon.Must(perfcommon.ConfigureTLSServer(publisherNode, cfg.transport))
	perfcommon.Must(publisherNode.Bind(publisherEndpoint))

	stats := perfcommon.NewStats()
	var window perfcommon.BenchmarkWindow

	subscriberNode, err := ctx.SpotNode()
	perfcommon.Must(err)
	defer subscriberNode.Close()
	perfcommon.ApplyMultiSpotNodeAdmission(subscriberNode, cfg.pattern)
	perfcommon.Must(perfcommon.ConfigureTLSClient(subscriberNode, cfg.transport))
	subscriberDiscovery, err := ctx.Discovery(zlink.AutoConnectSpotMesh, multiSpotChannelName)
	perfcommon.Must(err)
	defer subscriberDiscovery.Close()
	perfcommon.Must(subscriberDiscovery.ConnectRegistry(registryRouterEndpoint))

	subs := make([]multiSpotSubscriber, 0, cfg.clients)
	for i := 0; i < cfg.clients; i++ {
		spot, err := subscriberNode.Spot()
		perfcommon.Must(wrapMultiSpotError("subscriber spot create", i, err))
		subs = append(subs, multiSpotSubscriber{spot: spot})
	}
	// PERF_MULTI_TEST_POLICY § 1.3.1: a single poller covers every
	// subscriber so the drain goroutine waits with -1 (signal-driven)
	// for any inbound readiness.
	combinedPoller, err := zlink.NewPoller()
	perfcommon.Must(err)
	for i, sub := range subs {
		perfcommon.Must(wrapMultiSpotError("subscriber spot options", i,
			applyMultiSpotOptions(sub.spot, cfg.transport)))
		perfcommon.Must(wrapMultiSpotError("subscriber spot subscribe", i,
			sub.spot.SetSubscription(multiSpotTopic)))
		perfcommon.Must(wrapMultiSpotError("subscriber spot poller add", i,
			combinedPoller.AddSocket(sub.spot, perfcommon.ZLinkPollIn, i)))
	}
	subscriberEndpoint := perfcommon.UniqueEndpoint(cfg.transport, "perf-multi-spot-sub")
	perfcommon.Must(subscriberNode.AttachDiscovery(subscriberDiscovery))
	perfcommon.Must(subscriberNode.Bind(subscriberEndpoint))
	waitForMultiSpotRegistryEntries(query, multiSpotChannelName)
	defer func() {
		_ = combinedPoller.Close()
		for _, sub := range subs {
			_ = sub.spot.Close()
		}
	}()

	waitForMultiSpotReady(subs)
	window = perfcommon.NewBenchmarkWindow(cfg.duration)
	recvDone := make(chan struct{})
	go func() {
		defer close(recvDone)
		stopSeen := make([]bool, len(subs))
		stopsRemaining := len(subs)
		for stopsRemaining > 0 {
			event, err := combinedPoller.Wait(-1 * time.Millisecond)
			if err != nil {
				if perfcommon.IsTransient(err) {
					continue
				}
				perfcommon.Must(fmt.Errorf("multi spot poll: %w", err))
			}
			if event == nil {
				continue
			}
			if event.Events&perfcommon.ZLinkPollIn == 0 {
				continue
			}
			drainMultiSpotAvailable(
				subs,
				stats,
				cfg.msgSize,
				window.ActiveAt,
				window.StopAt,
				stopSeen,
				&stopsRemaining,
			)
		}
	}()

	payload := perfcommon.PreparePayload(cfg.msgSize)
	for time.Now().Before(window.StopAt) {
		perfcommon.StampWindowPayload(payload, window.ActiveAt)
		_, err := publisher.Publish(multiSpotTopic).Message(perfcommon.NewMessage(payload)).Flags(zlink.SendFlagsDontWait).Submit(nil)
		if err != nil {
			if perfcommon.IsTransient(err) {
				continue
			}
			perfcommon.Must(err)
		}
	}
	// PERF_MULTI_TEST_POLICY § 1.3.1: wire-level stop token replaces
	// the cooldown-payload terminator. Bounded retry through transient
	// backpressure guarantees every subscriber observes the marker.
	sendMultiSpotStopToken(publisher)
	<-recvDone

	return stats.Snapshot(cfg.duration, cfg.msgSize)
}

func runMultiSpotServer(cfg multiConfig) {
	ctx, err := perfcommon.NewMultiServerContext()
	perfcommon.Must(err)
	defer ctx.Close()

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
	controlPub, err := controlNode.Spot()
	perfcommon.Must(err)
	defer controlPub.Close()
	controlSub, err := controlNode.Spot()
	perfcommon.Must(err)
	defer controlSub.Close()
	perfcommon.Must(controlSub.SetSubscription(multiSpotTopic))

	dataEndpoint := perfcommon.UniqueEndpoint(cfg.transport, "perf-multi-spot-data")
	controlEndpoint := perfcommon.UniqueEndpoint(cfg.transport, "perf-multi-spot-control-server")
	perfcommon.Must(dataNode.Bind(dataEndpoint))
	perfcommon.Must(controlNode.Bind(controlEndpoint))
	dataEndpoint = spotNodeLastEndpoint(dataNode, dataEndpoint)
	controlEndpoint = spotNodeLastEndpoint(controlNode, controlEndpoint)
	flushControlLine("READY,%s", dataEndpoint)
	flushControlLine("CONTROL_READY,%s", controlEndpoint)

	events := make(chan string, 16)
	go scanSpotRoleStdin(cfg, events)
	readyCount := 0
	deadline := time.Now().Add(perfcommon.MultiReadyTimeout())
	for time.Now().Before(deadline) {
		select {
		case event := <-events:
			switch {
			case strings.HasPrefix(event, "CONNECT_CONTROL,"):
				endpoint := strings.TrimPrefix(event, "CONNECT_CONTROL,")
				perfcommon.Must(controlNode.ConnectPeer(endpoint))
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
		time.Sleep(time.Millisecond)
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
	payload := perfcommon.PreparePayload(cfg.msgSize)
	stopAt := time.Now().Add(cfg.duration)
	for time.Now().Before(stopAt) {
		perfcommon.StampPayload(payload)
		_, err := dataSpot.Publish(multiSpotTopic).Message(perfcommon.NewMessage(payload)).Flags(zlink.SendFlagsDontWait).Submit(nil)
		if err != nil && !perfcommon.IsTransient(err) {
			perfcommon.Must(err)
		}
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
	controlBind := perfcommon.UniqueEndpoint(cfg.transport, "perf-multi-spot-control-client")
	perfcommon.Must(controlNode.Bind(controlBind))
	perfcommon.Must(controlNode.ConnectPeer(controlEndpoint))
	clientControlEndpoint := spotNodeLastEndpoint(controlNode, controlBind)
	flushControlLine("CLIENT_CONTROL_ENDPOINT,%s", clientControlEndpoint)

	perfcommon.Must(dataNode.ConnectPeer(dataEndpoint))
	spots := make([]*zlink.Spot, 0, cfg.clients)
	for i := 0; i < cfg.clients; i++ {
		spot, err := dataNode.Spot()
		perfcommon.Must(err)
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
		time.Sleep(time.Millisecond)
	}
	stats := perfcommon.NewStats()
	stopAt := time.Now().Add(cfg.duration)
	for time.Now().Before(stopAt) {
		for _, spot := range spots {
			message, err := spot.Subscribe(zlink.RecvFlagsDontWait)
			if err != nil {
				if perfcommon.IsTransient(err) {
					continue
				}
				perfcommon.Must(err)
			}
			if message == nil {
				continue
			}
			part, err := message.SinglePartOrError()
			if err == nil && part != nil {
				if sentAt, ok := perfcommon.SentAtFromBytes(part.Data(), cfg.msgSize); ok {
					stats.AddLatencyNs(float64(time.Since(sentAt).Nanoseconds()))
				}
			}
			_ = message.Close()
		}
	}
	return stats.Snapshot(cfg.duration, cfg.msgSize)
}

func spotNodeLastEndpoint(node *zlink.SpotNode, fallback string) string {
	status, err := node.StatusSnapshot()
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

func publishSpotControlPayload(spot *zlink.Spot, payload string, timeout time.Duration) bool {
	deadline := time.Now().Add(timeout)
	for time.Now().Before(deadline) {
		sent, err := spot.Publish(multiSpotTopic).Message(perfcommon.NewMessage([]byte(payload))).Flags(zlink.SendFlagsDontWait).Submit(nil)
		if err == nil && sent {
			return true
		}
		if err != nil && !perfcommon.IsTransient(err) {
			perfcommon.Must(err)
		}
		time.Sleep(time.Millisecond)
	}
	return false
}

func receiveSpotControlPayload(spot *zlink.Spot) string {
	message, err := spot.Subscribe(zlink.RecvFlagsDontWait)
	if err != nil {
		if perfcommon.IsTransient(err) {
			return ""
		}
		perfcommon.Must(err)
	}
	if message == nil {
		return ""
	}
	defer message.Close()
	part, err := message.SinglePartOrError()
	if err != nil || part == nil {
		return ""
	}
	return string(part.Data())
}

func applyMultiSpotOptions(spot *zlink.Spot, transport string) error {
	if transport == "pgm" || transport == "epgm" {
		return nil
	}
	if err := spot.SetLinger(0); err != nil {
		return err
	}
	if err := spot.SetSendTimeout(perfcommon.MultiSendTimeout()); err != nil {
		return err
	}
	if err := spot.SetRecvTimeout(perfcommon.MultiRecvTimeout()); err != nil {
		return err
	}
	return nil
}

func wrapMultiSpotError(step string, index int, err error) error {
	if err == nil {
		return nil
	}
	return fmt.Errorf("multi spot setup %s[%d]: %w", step, index, err)
}

func waitForMultiSpotRegistryEntries(query *zlink.RegistryQueryClient, channelName string) {
	deadline := time.Now().Add(perfcommon.MultiReadyTimeout())
	for time.Now().Before(deadline) {
		entries, err := query.Snapshot(nil)
		if err == nil {
			count := 0
			for _, entry := range entries {
				if entry.ChannelName == channelName {
					count++
				}
			}
			if count >= 2 {
				return
			}
		}
		time.Sleep(10 * time.Millisecond)
	}
	perfcommon.Must(fmt.Errorf("multi spot perf registry entries timed out"))
}

func drainMultiSpotAvailable(
	subs []multiSpotSubscriber,
	stats *perfcommon.Stats,
	msgSize int,
	activeAt time.Time,
	stopAt time.Time,
	stopSeen []bool,
	stopsRemaining *int,
) {
	for index, sub := range subs {
		if stopSeen[index] {
			continue
		}
		for drained := 0; drained < multiSpotMaxDrainPerSpot; drained++ {
			message, err := sub.spot.Subscribe(zlink.RecvFlagsDontWait)
			if err != nil {
				if perfcommon.IsTransient(err) {
					break
				}
				perfcommon.Must(err)
			}
			if message == nil {
				break
			}
			part, err := message.SinglePartOrError()
			if err == nil && part != nil {
				if perfcommon.IsStopTokenMessage(part) {
					if !stopSeen[index] {
						stopSeen[index] = true
						*stopsRemaining--
					}
					_ = message.Close()
					break
				}
				data := part.Data()
				header, ok := perfcommon.DecodeMetricHeader(data)
				if ok && header.RunID == perfcommon.MetricRunID && int(header.MsgSize) == msgSize && header.Phase == perfcommon.PhaseActive {
					now := time.Now()
					if now.After(activeAt) && now.Before(stopAt) {
						stats.AddLatencyNs(float64(now.UnixNano() - header.SentTsNs))
					}
				}
			}
			_ = message.Close()
		}
	}
}

func sendMultiSpotStopToken(publisher *zlink.Spot) {
	for retry := 0; retry < perfcommon.StopTokenSendRetries; retry++ {
		sent, err := publisher.Publish(multiSpotTopic).Message(perfcommon.NewMessage(perfcommon.StopToken)).Flags(zlink.SendFlagsNone).Submit(nil)
		if err == nil && sent {
			return
		}
		if err != nil && !perfcommon.IsTransient(err) {
			return
		}
		time.Sleep(perfcommon.StopTokenSendBackoff)
	}
}

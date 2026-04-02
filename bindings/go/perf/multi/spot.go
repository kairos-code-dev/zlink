package main

import (
	"time"

	"zlink"
	"zlink/perf/internal/perfcommon"
)

type multiSpotSubscriber struct {
	node *zlink.SpotNode
	spot *zlink.Spot
}

func runMultiSpot(cfg multiConfig) perfcommon.Result {
	ctx, err := zlink.NewContext()
	perfcommon.Must(err)
	defer ctx.Close()

	publisherNode, err := ctx.SpotNode()
	perfcommon.Must(err)
	defer publisherNode.Close()
	publisher, err := publisherNode.Spot()
	perfcommon.Must(err)
	defer publisher.Close()
	perfcommon.Must(publisher.SetNoDrop(true))

	endpoint := perfcommon.UniqueTCPEndpoint("perf-multi-spot")
	perfcommon.Must(publisherNode.Bind(endpoint))

	stats := perfcommon.NewStats()
	stopAt := time.Now().Add(cfg.warmup + cfg.duration)
	activeAt := time.Now().Add(cfg.warmup)

	subs := make([]multiSpotSubscriber, 0, cfg.clients)
	readyCh := make(chan int, cfg.clients)
	readySeen := make([]bool, cfg.clients)

	for i := 0; i < cfg.clients; i++ {
		node, err := ctx.SpotNode()
		perfcommon.Must(err)
		spot, err := node.Spot()
		perfcommon.Must(err)
		perfcommon.Must(node.ConnectPeer(endpoint))
		perfcommon.Must(spot.SetSubscription("bench."))
		if cfg.recvMode == "callback" {
			index := i
			perfcommon.Must(spot.OnSubscribe(func(message *zlink.TopicMessage) {
				defer message.Close()
				if !readySeen[index] {
					readySeen[index] = true
					select {
					case readyCh <- index:
					default:
					}
				}
				part, err := message.SinglePartOrError()
				if err != nil {
					return
				}
				if sentAt, ok := perfcommon.SentAtFromMessage(part); ok && time.Now().After(activeAt) {
					stats.Add(sentAt)
				}
			}))
		}
		subs = append(subs, multiSpotSubscriber{node: node, spot: spot})
	}
	defer func() {
		for _, sub := range subs {
			_ = sub.spot.Close()
			_ = sub.node.Close()
		}
	}()

	waitForMultiSpotReady(publisher, subs, cfg.recvMode, readyCh, readySeen)

	payload := perfcommon.PreparePayload(cfg.msgSize)
	for time.Now().Before(stopAt) {
		perfcommon.StampPayload(payload)
		result, err := publisher.TryPublish("bench.topic", perfcommon.NewMessage(payload))
		if err != nil {
			if perfcommon.IsTransient(err) {
				continue
			}
			perfcommon.Must(err)
		}
		if result != zlink.SendResultSent {
			time.Sleep(250 * time.Microsecond)
		}
		if cfg.recvMode == "recv" {
			_ = drainMultiSpotOnce(subs, stats, activeAt, nil)
		}
	}
	if cfg.recvMode == "recv" {
		drainUntil := time.Now().Add(500 * time.Millisecond)
		for time.Now().Before(drainUntil) {
			if drainMultiSpotOnce(subs, stats, activeAt, nil) == 0 {
				time.Sleep(250 * time.Microsecond)
			}
		}
	} else {
		time.Sleep(500 * time.Millisecond)
	}

	return stats.Snapshot(cfg.duration, cfg.msgSize)
}

func waitForMultiSpotReady(publisher *zlink.Spot, subs []multiSpotSubscriber, recvMode string, readyCh <-chan int, readySeen []bool) {
	payload := perfcommon.PreparePayload(64)
	readyCount := 0
	deadline := time.Now().Add(5 * time.Second)
	for time.Now().Before(deadline) && readyCount < len(subs) {
		perfcommon.StampPayload(payload)
		result, err := publisher.TryPublish("bench.topic", perfcommon.NewMessage(payload))
		if err != nil {
			if perfcommon.IsTransient(err) {
				continue
			}
			perfcommon.Must(err)
		}
		if result != zlink.SendResultSent {
			time.Sleep(250 * time.Microsecond)
			continue
		}
		if recvMode == "callback" {
			drainLoop := true
			for drainLoop {
				select {
				case index := <-readyCh:
					if !readySeen[index] {
						readySeen[index] = true
					}
				default:
					drainLoop = false
				}
			}
			readyCount = 0
			for _, ready := range readySeen {
				if ready {
					readyCount++
				}
			}
			continue
		}
		readyCount += drainMultiSpotOnce(subs, nil, time.Now().Add(24*time.Hour), readySeen)
		time.Sleep(250 * time.Microsecond)
	}
	if readyCount < len(subs) {
		perfcommon.Must(&multiSpotReadyError{})
	}
}

func drainMultiSpotOnce(subs []multiSpotSubscriber, stats *perfcommon.Stats, activeAt time.Time, readySeen []bool) int {
	processed := 0
	for index, sub := range subs {
		message, ok, err := sub.spot.TrySubscribe()
		if err != nil {
			if perfcommon.IsTransient(err) {
				continue
			}
			perfcommon.Must(err)
		}
		if !ok || message == nil {
			continue
		}
		processed++
		part, err := message.SinglePartOrError()
		if err == nil && stats != nil {
			if sentAt, ok := perfcommon.SentAtFromMessage(part); ok && time.Now().After(activeAt) {
				stats.Add(sentAt)
			}
		}
		if readySeen != nil && index < len(readySeen) && !readySeen[index] {
			readySeen[index] = true
		}
		_ = message.Close()
	}
	return processed
}

type multiSpotReadyError struct{}

func (e *multiSpotReadyError) Error() string {
	return "multi spot perf endpoint did not become ready"
}

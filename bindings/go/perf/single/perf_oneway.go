package main

import (
	"fmt"
	"os"
	"time"

	"zlink.systems/zlink"
	"zlink.systems/zlink/perf/internal/perfcommon"
)

type recvSocket interface {
	zlink.SocketTarget
	Recv(*zlink.Received, zlink.RecvFlags) (bool, error)
}

func runSingleOneWay(
	cfg benchmarkConfig,
	receiver recvSocket,
	send func([]byte) error,
) perfcommon.Result {
	stats := perfcommon.NewStats()
	window := perfcommon.NewBenchmarkWindow(cfg.duration)
	payload := perfcommon.PreparePayload(cfg.msgSize)
	poller := perfcommon.NewSocketPoller(receiver, perfcommon.ZLinkPollIn)
	defer poller.Close()

	go func() {
		for time.Now().Before(window.StopAt) {
			perfcommon.StampWindowPayload(payload, window.ActiveAt)
			err := send(payload)
			if err != nil {
				if perfcommon.IsTransient(err) {
					continue
				}
				if os.Getenv("PERF_DEBUG") != "" {
					fmt.Fprintf(os.Stderr, "single active send error: %v\n", err)
				}
				perfcommon.Must(err)
			}
		}
		// PERF_SINGLE_TEST_POLICY § 1.4: signal phase end via wire-level
		// stop token. Blocking send uses bounded transient-backpressure attempts
		// so the receiver always observes the terminator.
		sendStopTokenSingle(send)
	}()

	// PERF_SINGLE_TEST_POLICY § 1.4: signal-driven wait (-1 ms). The loop
	// exits when the wire-level stop token arrives.
	for {
		event, err := poller.Wait(-1 * time.Millisecond)
		if err != nil {
			if perfcommon.IsTransient(err) {
				continue
			}
			if os.Getenv("PERF_DEBUG") != "" {
				fmt.Fprintf(os.Stderr, "single active poller wait error: %v\n", err)
			}
			perfcommon.Must(err)
		}
		if event == nil {
			continue
		}
		if event.Events&perfcommon.ZLinkPollIn == 0 {
			continue
		}
		stop, drainErr := drainSingleOneWayUntilStop(receiver, stats, cfg.msgSize, window.ActiveAt, window.StopAt)
		if drainErr != nil {
			perfcommon.Must(drainErr)
		}
		if stop {
			break
		}
	}

	return stats.Snapshot(cfg.duration, cfg.msgSize)
}

// drainSingleOneWayUntilStop drains the receiver until either a transient
// EAGAIN-style condition or the wire-level stop token arrives. It returns
// stop=true when the stop token has been observed.
func drainSingleOneWayUntilStop(
	receiver recvSocket,
	stats *perfcommon.Stats,
	msgSize int,
	activeAt time.Time,
	stopAt time.Time,
) (bool, error) {
	var received zlink.Received
	for {
		ok, err := receiver.Recv(&received, zlink.RecvFlagsDontWait)
		if err != nil {
			if perfcommon.IsTransient(err) {
				return false, nil
			}
			return false, err
		}
		if !ok {
			return false, nil
		}
		part, err := perfPayloadPart(&received)
		if err == nil && perfcommon.IsStopTokenMessage(part) {
			_ = received.Close()
			return true, nil
		}
		if err == nil && stats != nil {
			perfcommon.RecordMessageLatency(stats, activeAt, stopAt, msgSize, part)
		}
		_ = received.Close()
	}
}

// drainSingleOneWayProbe drains the receiver of pending messages once
// during ready-probe / phase-end barrier handshakes. It is not used by
// the active measurement loop (which goes through
// drainSingleOneWayUntilStop) but is kept for ready helpers that just
// need to know "did anything arrive."
func drainSingleOneWayProbe(receiver recvSocket) bool {
	processed := false
	var received zlink.Received
	for {
		ok, err := receiver.Recv(&received, zlink.RecvFlagsDontWait)
		if err != nil {
			if perfcommon.IsTransient(err) {
				return processed
			}
			perfcommon.Must(err)
		}
		if !ok {
			return processed
		}
		processed = true
		_ = received.Close()
	}
}

// sendStopTokenSingle attempts to push the wire-level stop token onto the
// connection. The send is bounded: each transient backpressure /
// EAGAIN response yields for `StopTokenSendBackoff`, capped by
// `StopTokenSendAttempts`. A non-transient error is fatal.
func sendStopTokenSingle(send func([]byte) error) {
	for attempt := 0; attempt < perfcommon.StopTokenSendAttempts; attempt++ {
		err := send(perfcommon.StopToken)
		if err == nil {
			return
		}
		if !perfcommon.IsTransient(err) {
			if os.Getenv("PERF_DEBUG") != "" {
				fmt.Fprintf(os.Stderr, "single stop token send error: %v\n", err)
			}
			return
		}
		time.Sleep(perfcommon.StopTokenSendBackoff)
	}
	if os.Getenv("PERF_DEBUG") != "" {
		fmt.Fprintln(os.Stderr, "single stop token send: attempts exhausted")
	}
}

func waitSingleRouteReady(
	name string,
	send func([]byte) error,
	receiver recvSocket,
) {
	payload := perfcommon.PreparePayload(64)
	if os.Getenv("PERF_DEBUG") != "" {
		fmt.Fprintf(os.Stderr, "%s ready probe poller create\n", name)
	}
	poller := perfcommon.NewSocketPoller(receiver, perfcommon.ZLinkPollIn)
	defer poller.Close()
	perfcommon.StampProbePayload(payload)
	if os.Getenv("PERF_DEBUG") != "" {
		fmt.Fprintf(os.Stderr, "%s ready probe send\n", name)
	}
	if err := send(payload); err != nil {
		perfcommon.Must(fmt.Errorf("%s ready probe send: %w", name, err))
	}
	if os.Getenv("PERF_DEBUG") != "" {
		fmt.Fprintf(os.Stderr, "%s ready probe sent\n", name)
	}
	deadline := time.Now().Add(perfcommon.SingleReadyTimeout())
	for time.Now().Before(deadline) {
		timeout := time.Until(deadline)
		if timeout <= 0 {
			break
		}
		event, err := poller.Wait(timeout)
		if err != nil {
			if os.Getenv("PERF_DEBUG") != "" {
				fmt.Fprintf(os.Stderr, "%s ready probe poller wait error: %v\n", name, err)
			}
			perfcommon.Must(err)
		}
		if event == nil || event.Events&perfcommon.ZLinkPollIn == 0 {
			if os.Getenv("PERF_DEBUG") != "" {
				fmt.Fprintf(os.Stderr, "%s ready probe poller wait timeout\n", name)
			}
			continue
		}
		if os.Getenv("PERF_DEBUG") != "" {
			fmt.Fprintf(os.Stderr, "%s ready probe poller readable\n", name)
		}
		if drainSingleOneWayProbe(receiver) {
			if os.Getenv("PERF_DEBUG") != "" {
				fmt.Fprintf(os.Stderr, "%s ready probe drained\n", name)
			}
			return
		}
	}
	perfcommon.Must(fmt.Errorf("%s did not become ready", name))
}

func perfPayloadPart(received *zlink.Received) (*zlink.Message, error) {
	if received == nil {
		return nil, nil
	}
	if received.IsSinglePart() {
		return received.SinglePartOrError()
	}
	parts := received.Parts()
	if received.HasRoutingID() && len(parts) > 0 {
		return parts[len(parts)-1], nil
	}
	return received.SinglePartOrError()
}

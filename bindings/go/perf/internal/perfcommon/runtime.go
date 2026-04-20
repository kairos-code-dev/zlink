package perfcommon

import (
	"os"
	"strconv"
	"strings"
	"time"

	"zlink"
)

const (
	ZLinkPollIn  int16 = 1
	ZLinkPollOut int16 = 2
)

type BenchmarkWindow struct {
	StopAt   time.Time
	ActiveAt time.Time
}

func NewBenchmarkWindow(duration time.Duration) BenchmarkWindow {
	now := time.Now()
	return BenchmarkWindow{
		StopAt:   now.Add(duration),
		ActiveAt: now,
	}
}

func StampWindowPayload(payload []byte, activeAt time.Time) {
	if time.Now().Before(activeAt) {
		StampProbePayload(payload)
		return
	}
	StampPayload(payload)
}

func RecordMessageLatency(stats *Stats, activeAt time.Time, msgSize int, part *zlink.Message) {
	if sentAt, ok := SentAtFromMessage(part, msgSize); ok && time.Now().After(activeAt) {
		stats.Add(sentAt)
	}
}

func RecordMessageRTTLatency(stats *Stats, activeAt time.Time, msgSize int, part *zlink.Message) {
	if sentAt, ok := SentAtFromMessage(part, msgSize); ok && time.Now().After(activeAt) {
		stats.AddLatencyNs(float64(time.Since(sentAt).Nanoseconds()) / 2.0)
	}
}

func RecordBytesLatency(stats *Stats, activeAt time.Time, msgSize int, payload []byte) {
	if sentAt, ok := SentAtFromBytes(payload, msgSize); ok && time.Now().After(activeAt) {
		stats.Add(sentAt)
	}
}

func RecordBytesRTTLatency(stats *Stats, activeAt time.Time, msgSize int, payload []byte) {
	if sentAt, ok := SentAtFromBytes(payload, msgSize); ok && time.Now().After(activeAt) {
		stats.AddLatencyNs(float64(time.Since(sentAt).Nanoseconds()) / 2.0)
	}
}

func PostReadySettle(pattern string) {
	duration := readySettleDuration(pattern)
	if duration <= 0 {
		return
	}
	time.Sleep(duration)
}

func readySettleDuration(pattern string) time.Duration {
	switch pattern {
	case "PUBSUB":
		return durationFromEnv("PERF_SINGLE_PUBSUB_READY_SETTLE_MS", time.Second)
	case "SPOT", "SPOT_REQREP":
		return durationFromEnv("PERF_SINGLE_SPOT_READY_SETTLE_MS", time.Second)
	default:
		return 0
	}
}

func SingleIdleDrainDuration() time.Duration {
	return durationFromEnv("PERF_SINGLE_RCVTIMEO_MS", 200*time.Millisecond)
}

func SinglePubSubRecvTimeout() time.Duration {
	return durationFromEnv("PERF_SINGLE_PUBSUB_RCVTIMEO_MS", SingleIdleDrainDuration())
}

func SingleReadyTimeout() time.Duration {
	return durationFromEnv("PERF_CONNECT_READY_TIMEOUT_MS", 5*time.Second)
}

func MultiReadyTimeout() time.Duration {
	return durationFromEnv("PERF_MULTI_CONNECT_READY_TIMEOUT_MS", 5*time.Second)
}

func durationFromEnv(name string, fallback time.Duration) time.Duration {
	raw := os.Getenv(name)
	if raw == "" {
		return fallback
	}
	value, err := strconv.Atoi(raw)
	if err != nil || value < 0 {
		return fallback
	}
	return time.Duration(value) * time.Millisecond
}

func EnvEnabled(name string, fallback bool) bool {
	raw := strings.TrimSpace(strings.ToLower(os.Getenv(name)))
	if raw == "" {
		return fallback
	}
	switch raw {
	case "1", "true", "yes", "on":
		return true
	case "0", "false", "no", "off":
		return false
	default:
		return fallback
	}
}

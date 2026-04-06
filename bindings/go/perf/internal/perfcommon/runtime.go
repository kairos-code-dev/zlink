package perfcommon

import (
	"time"

	"zlink"
)

type BenchmarkWindow struct {
	StopAt   time.Time
	ActiveAt time.Time
}

func NewBenchmarkWindow(warmup, duration time.Duration) BenchmarkWindow {
	now := time.Now()
	return BenchmarkWindow{
		StopAt:   now.Add(warmup + duration),
		ActiveAt: now.Add(warmup),
	}
}

func RecordMessageLatency(stats *Stats, activeAt time.Time, part *zlink.Message) {
	if sentAt, ok := SentAtFromMessage(part); ok && time.Now().After(activeAt) {
		stats.Add(sentAt)
	}
}

func RecordBytesLatency(stats *Stats, activeAt time.Time, payload []byte) {
	if sentAt, ok := SentAtFromBytes(payload); ok && time.Now().After(activeAt) {
		stats.Add(sentAt)
	}
}

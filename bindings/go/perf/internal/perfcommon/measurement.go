package perfcommon

import (
	"encoding/binary"
	"sort"
	"time"

	"zlink"
)

type LatencyStats struct {
	P50 float64
	P95 float64
	P99 float64
}

func StampPayload(payload []byte) {
	binary.LittleEndian.PutUint64(payload[:8], uint64(time.Now().UnixNano()))
}

func ComputeLatencyStats(values []float64) LatencyStats {
	if len(values) == 0 {
		return LatencyStats{}
	}

	sorted := append([]float64(nil), values...)
	sort.Float64s(sorted)
	return LatencyStats{
		P50: percentile(sorted, 50),
		P95: percentile(sorted, 95),
		P99: percentile(sorted, 99),
	}
}

func SentAtFromBytes(data []byte) (time.Time, bool) {
	if len(data) < 8 {
		return time.Time{}, false
	}
	return time.Unix(0, int64(binary.LittleEndian.Uint64(data[:8]))), true
}

func SentAtFromMessage(part *zlink.Message) (time.Time, bool) {
	if part == nil {
		return time.Time{}, false
	}
	data := part.Data()
	if len(data) < 8 {
		return time.Time{}, false
	}
	header := [8]byte{}
	copy(header[:], data[:8])
	return time.Unix(0, int64(binary.LittleEndian.Uint64(header[:]))), true
}

func percentile(values []float64, pct float64) float64 {
	if len(values) == 0 {
		return 0
	}
	idx := int((pct / 100.0) * float64(len(values)-1))
	return values[idx]
}

package perfcommon

import (
	"encoding/binary"
	"sync/atomic"
	"time"

	"zlink"
)

const (
	MetricMagic      uint32 = 0x5A4C4E4B
	MetricHeaderSize        = 29
	MetricRunID      uint32 = 1
	PhaseWarmup      uint8  = 0
	PhaseActive      uint8  = 1
	PhaseCooldown    uint8  = 2
)

var metricSequence uint64

func StampPayload(payload []byte) {
	StampPayloadPhase(payload, PhaseActive)
}

func StampProbePayload(payload []byte) {
	StampPayloadPhase(payload, PhaseWarmup)
}

func StampCooldownPayload(payload []byte) {
	StampPayloadPhase(payload, PhaseCooldown)
}

func StampPayloadPhase(payload []byte, phase uint8) {
	if len(payload) < MetricHeaderSize {
		Must(&invalidMetricPayloadError{Size: len(payload)})
	}
	binary.LittleEndian.PutUint32(payload[0:4], MetricMagic)
	binary.LittleEndian.PutUint32(payload[4:8], MetricRunID)
	payload[8] = phase
	binary.LittleEndian.PutUint32(payload[9:13], uint32(len(payload)))
	binary.LittleEndian.PutUint64(payload[13:21], atomic.AddUint64(&metricSequence, 1))
	binary.LittleEndian.PutUint64(payload[21:29], uint64(time.Now().UnixNano()))
}

type MetricHeader struct {
	Magic    uint32
	RunID    uint32
	Phase    uint8
	MsgSize  uint32
	Sequence uint64
	SentTsNs int64
}

func DecodeMetricHeader(data []byte) (MetricHeader, bool) {
	if len(data) < MetricHeaderSize {
		return MetricHeader{}, false
	}
	return MetricHeader{
		Magic:    binary.LittleEndian.Uint32(data[0:4]),
		RunID:    binary.LittleEndian.Uint32(data[4:8]),
		Phase:    data[8],
		MsgSize:  binary.LittleEndian.Uint32(data[9:13]),
		Sequence: binary.LittleEndian.Uint64(data[13:21]),
		SentTsNs: int64(binary.LittleEndian.Uint64(data[21:29])),
	}, true
}

func validActiveHeader(header MetricHeader, expectedMsgSize int) bool {
	return header.Magic == MetricMagic &&
		header.RunID == MetricRunID &&
		header.Phase == PhaseActive &&
		int(header.MsgSize) == expectedMsgSize
}

func SentAtFromBytes(data []byte, expectedMsgSize int) (time.Time, bool) {
	header, ok := DecodeMetricHeader(data)
	if !ok || !validActiveHeader(header, expectedMsgSize) {
		return time.Time{}, false
	}
	return time.Unix(0, header.SentTsNs), true
}

func SentAtFromMessage(part *zlink.Message, expectedMsgSize int) (time.Time, bool) {
	if part == nil {
		return time.Time{}, false
	}
	data := part.Data()
	return SentAtFromBytes(data, expectedMsgSize)
}

func percentile(values []float64, pct float64) float64 {
	if len(values) == 0 {
		return 0
	}
	idx := int((pct / 100.0) * float64(len(values)-1))
	return values[idx]
}

type invalidMetricPayloadError struct {
	Size int
}

func (e *invalidMetricPayloadError) Error() string {
	return "perf payload must be >= 29 bytes"
}

package perfcommon

import (
	"strings"
	"time"
)

type SingleConfig struct {
	Pattern   string
	Transport string
	MsgSize   int
	Warmup    time.Duration
	Duration  time.Duration
}

type MultiConfig struct {
	Pattern   string
	Transport string
	MsgSize   int
	Warmup    time.Duration
	Duration  time.Duration
	Clients   int
}

func LoadSingleConfig(pattern, transport string, msgSize, warmup, duration int) SingleConfig {
	cfg := SingleConfig{
		Pattern:   strings.ToUpper(pattern),
		Transport: strings.ToLower(transport),
		MsgSize:   msgSize,
		Warmup:    time.Duration(warmup) * time.Second,
		Duration:  time.Duration(duration) * time.Second,
	}
	ValidateCommon(cfg.Transport, cfg.MsgSize)
	return cfg
}

func LoadMultiConfig(pattern, transport string, msgSize, warmup, duration int, clients int) MultiConfig {
	cfg := MultiConfig{
		Pattern:   strings.ToUpper(pattern),
		Transport: strings.ToLower(transport),
		MsgSize:   msgSize,
		Warmup:    time.Duration(warmup) * time.Second,
		Duration:  time.Duration(duration) * time.Second,
		Clients:   clients,
	}
	ValidateCommon(cfg.Transport, cfg.MsgSize)
	if cfg.Clients <= 0 {
		Must(&invalidClientCountError{Clients: cfg.Clients})
	}
	return cfg
}

type invalidClientCountError struct {
	Clients int
}

func (e *invalidClientCountError) Error() string {
	return "clients must be > 0"
}

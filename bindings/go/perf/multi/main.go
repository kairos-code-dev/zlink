package main

import (
	"flag"
	"strings"
	"time"

	"zlink/perf/internal/perfcommon"
)

type multiConfig struct {
	pattern   string
	transport string
	msgSize   int
	warmup    time.Duration
	duration  time.Duration
	recvMode  string
	clients   int
}

var (
	multiPattern   = flag.String("pattern", "MULTI_PUBSUB", "")
	multiTransport = flag.String("transport", "tcp", "")
	multiSize      = flag.Int("msg-size", 64, "")
	multiWarmup    = flag.Int("warmup", 2, "")
	multiDuration  = flag.Int("duration", 5, "")
	multiRecv      = flag.String("recv", "recv", "")
	multiClients   = flag.Int("clients", 100, "")
)

func main() {
	flag.Parse()

	cfg := multiConfig{
		pattern:   strings.ToUpper(*multiPattern),
		transport: strings.ToLower(*multiTransport),
		msgSize:   *multiSize,
		warmup:    time.Duration(*multiWarmup) * time.Second,
		duration:  time.Duration(*multiDuration) * time.Second,
		recvMode:  strings.ToLower(*multiRecv),
		clients:   *multiClients,
	}
	perfcommon.ValidateCommon(cfg.transport, cfg.msgSize, cfg.recvMode)
	if cfg.clients <= 0 {
		perfcommon.Must(&invalidClientCountError{clients: cfg.clients})
	}

	var result perfcommon.Result
	switch cfg.pattern {
	case "MULTI_PUBSUB":
		result = runMultiPubSub(cfg)
	case "MULTI_DEALER_DEALER":
		result = runMultiDealerDealer(cfg)
	case "MULTI_DEALER_ROUTER":
		result = runMultiDealerRouter(cfg)
	case "MULTI_ROUTER_ROUTER":
		result = runMultiRouterRouter(cfg)
	case "MULTI_SPOT":
		result = runMultiSpot(cfg)
	case "MULTI_STREAM":
		result = runMultiStream(cfg)
	default:
		perfcommon.Must(&unsupportedMultiPatternError{pattern: cfg.pattern})
	}

	perfcommon.PrintResult(cfg.pattern, cfg.transport, cfg.msgSize, result)
}

type unsupportedMultiPatternError struct {
	pattern string
}

func (e *unsupportedMultiPatternError) Error() string {
	return "unsupported multi perf pattern: " + e.pattern
}

type invalidClientCountError struct {
	clients int
}

func (e *invalidClientCountError) Error() string {
	return "clients must be > 0"
}

package main

import (
	"flag"
	"time"

	"zlink/perf/internal/perfcommon"
)

type multiConfig struct {
	pattern   string
	transport string
	msgSize   int
	warmup    time.Duration
	duration  time.Duration
	clients   int
}

var (
	multiPattern   = flag.String("pattern", "MULTI_PUBSUB", "")
	multiTransport = flag.String("transport", "tcp", "")
	multiSize      = flag.Int("msg-size", 64, "")
	multiWarmup    = flag.Int("warmup", 2, "")
	multiDuration  = flag.Int("duration", 5, "")
	multiClients   = flag.Int("clients", 100, "")
)

func main() {
	flag.Parse()

	loaded := perfcommon.LoadMultiConfig(
		*multiPattern,
		*multiTransport,
		*multiSize,
		*multiWarmup,
		*multiDuration,
		*multiClients,
	)
	cfg := multiConfig{
		pattern:   loaded.Pattern,
		transport: loaded.Transport,
		msgSize:   loaded.MsgSize,
		warmup:    loaded.Warmup,
		duration:  loaded.Duration,
		clients:   loaded.Clients,
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

	result = perfcommon.FinalizeResult(cfg.pattern, cfg.msgSize, result)
	perfcommon.PrintResult(cfg.pattern, cfg.transport, cfg.msgSize, result)
}

type unsupportedMultiPatternError struct {
	pattern string
}

func (e *unsupportedMultiPatternError) Error() string {
	return "unsupported multi perf pattern: " + e.pattern
}

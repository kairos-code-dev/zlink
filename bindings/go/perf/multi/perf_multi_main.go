package main

import (
	"flag"
	"fmt"
	"os"
	"time"

	"zlink.systems/zlink/perf/internal/perfcommon"
)

type multiConfig struct {
	pattern   string
	transport string
	msgSize   int
	duration  time.Duration
	clients   int
}

var (
	multiPattern   = flag.String("pattern", "MULTI_PUBSUB", "")
	multiTransport = flag.String("transport", "tcp", "")
	multiSize      = flag.Int("msg-size", 64, "")
	multiDuration  = flag.Int("duration", 5, "")
	multiClients   = flag.Int("clients", 100, "")
	multiRole      = flag.String("role", "", "")
	multiEndpoint  = flag.String("endpoint", "", "")
)

func main() {
	flag.Parse()

	loaded := perfcommon.LoadMultiConfig(
		*multiPattern,
		*multiTransport,
		*multiSize,
		*multiDuration,
		*multiClients,
	)
	cfg := multiConfig{
		pattern:   loaded.Pattern,
		transport: loaded.Transport,
		msgSize:   loaded.MsgSize,
		duration:  loaded.Duration,
		clients:   loaded.Clients,
	}

	if *multiRole != "" {
		runMultiRole(cfg, *multiRole, *multiEndpoint)
		return
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
	case "MULTI_SPOT_REQREP":
		result = runMultiSpotReqRep(cfg)
	case "MULTI_SPOT_SENDSEND":
		result = runMultiSpotSendSend(cfg)
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

func runMultiRole(cfg multiConfig, role, endpoint string) {
	switch role {
	case "server":
		runMultiServerRole(cfg)
	case "client":
		if endpoint == "" {
			perfcommon.Must(fmt.Errorf("--endpoint is required for multi client role"))
		}
		runMultiClientRole(cfg, endpoint)
	default:
		perfcommon.Must(fmt.Errorf("unsupported multi perf role: %s", role))
	}
}

func runMultiServerRole(cfg multiConfig) {
	switch cfg.pattern {
	case "MULTI_PUBSUB":
		runMultiPubSubServer(cfg)
	case "MULTI_DEALER_DEALER":
		runMultiDealerDealerServer(cfg)
	case "MULTI_DEALER_ROUTER":
		runMultiDealerRouterServer(cfg)
	case "MULTI_ROUTER_ROUTER":
		runMultiRouterRouterServer(cfg)
	case "MULTI_STREAM":
		runMultiStreamServer(cfg)
	case "MULTI_SPOT":
		runMultiSpotServer(cfg)
	case "MULTI_SPOT_REQREP":
		runMultiSpotReqRepServer(cfg)
	case "MULTI_SPOT_SENDSEND":
		runMultiSpotSendSendServer(cfg)
	default:
		perfcommon.Must(&unsupportedMultiPatternError{pattern: cfg.pattern})
	}
}

func runMultiClientRole(cfg multiConfig, endpoint string) {
	switch cfg.pattern {
	case "MULTI_PUBSUB":
		result := runMultiPubSubClient(cfg, endpoint)
		printMultiResult(cfg, result)
	case "MULTI_DEALER_DEALER":
		runMultiDealerDealerClient(cfg, endpoint)
	case "MULTI_DEALER_ROUTER":
		result := runMultiDealerRouterClient(cfg, endpoint)
		printMultiResult(cfg, result)
	case "MULTI_ROUTER_ROUTER":
		result := runMultiRouterRouterClientRole(cfg, endpoint)
		printMultiResult(cfg, result)
	case "MULTI_STREAM":
		result := runMultiStreamClient(cfg, endpoint)
		printMultiResult(cfg, result)
	case "MULTI_SPOT":
		result := runMultiSpotClient(cfg, endpoint)
		printMultiResult(cfg, result)
	case "MULTI_SPOT_REQREP":
		result := runMultiSpotReqRepClientRole(cfg, endpoint)
		printMultiResult(cfg, result)
	case "MULTI_SPOT_SENDSEND":
		result := runMultiSpotSendSendClientRole(cfg, endpoint)
		printMultiResult(cfg, result)
	default:
		perfcommon.Must(&unsupportedMultiPatternError{pattern: cfg.pattern})
	}
}

func printMultiResult(cfg multiConfig, result perfcommon.Result) {
	result = perfcommon.FinalizeResult(cfg.pattern, cfg.msgSize, result)
	perfcommon.PrintResult(cfg.pattern, cfg.transport, cfg.msgSize, result)
}

func flushControlLine(format string, args ...interface{}) {
	fmt.Printf(format+"\n", args...)
	_ = os.Stdout.Sync()
}

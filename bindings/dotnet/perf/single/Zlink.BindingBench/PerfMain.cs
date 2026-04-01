using System;

if (args.Length < 3)
    return 1;

var recvMode = (Environment.GetEnvironmentVariable("PERF_RECV_MODE")
    ?? "callback").Trim();
if (!string.Equals(recvMode, "callback", StringComparison.OrdinalIgnoreCase))
    return 1;

var pattern = args[0].ToUpperInvariant();
var transport = args[1];
if (!int.TryParse(args[2], out var size))
    return 1;

return pattern switch
{
    "PAIR" => PerfPair.RunPair(transport, size),
    "PUBSUB" => PerfPubSub.RunPubSub(transport, size),
    "DEALER_DEALER" => PerfDealerDealer.RunDealerDealer(transport, size),
    "DEALER_ROUTER" => PerfDealerRouter.RunDealerRouter(transport, size),
    "ROUTER_ROUTER" => PerfRouterRouter.RunRouterRouter(transport, size),
    "SPOT" => PerfSpot.RunSpot(transport, size),
    _ => 1,
};

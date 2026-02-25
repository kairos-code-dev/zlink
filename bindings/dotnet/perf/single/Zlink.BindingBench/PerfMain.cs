using System;

if (args.Length < 3)
    return 1;

var pattern = args[0].ToUpperInvariant();
var transport = args[1];
if (!int.TryParse(args[2], out var size))
    return 1;

return pattern switch
{
    "PAIR" => PerfRunner.RunPair(transport, size),
    "PUBSUB" => PerfRunner.RunPubSub(transport, size),
    "DEALER_DEALER" => PerfRunner.RunDealerDealer(transport, size),
    "DEALER_ROUTER" => PerfRunner.RunDealerRouter(transport, size),
    "ROUTER_ROUTER" => PerfRunner.RunRouterRouter(transport, size),
    "ROUTER_ROUTER_POLL" => PerfRunner.RunRouterRouterPoll(transport, size),
    "STREAM" => PerfRunner.RunStream(transport, size),
    "STREAM_CALLBACK" => PerfRunner.RunStreamCallback(transport, size),
    "STREAM_LEN32BE" => PerfRunner.RunStreamLen32Be(transport, size),
    "GATEWAY" => PerfRunner.RunGateway(transport, size),
    "SPOT" => PerfRunner.RunSpot(transport, size),
    _ => 2,
};

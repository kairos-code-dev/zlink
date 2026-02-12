using System;

if (args.Length < 3)
    return 1;

var pattern = args[0].ToUpperInvariant();
var transport = args[1];
if (!int.TryParse(args[2], out var size))
    return 1;

return pattern switch
{
    "PAIR" => BenchRunner.RunPair(transport, size),
    "PUBSUB" => BenchRunner.RunPubSub(transport, size),
    "DEALER_DEALER" => BenchRunner.RunDealerDealer(transport, size),
    "DEALER_ROUTER" => BenchRunner.RunDealerRouter(transport, size),
    "ROUTER_ROUTER" => BenchRunner.RunRouterRouter(transport, size),
    "ROUTER_ROUTER_POLL" => BenchRunner.RunRouterRouterPoll(transport, size),
    "STREAM" => BenchRunner.RunStream(transport, size),
    "GATEWAY" => BenchRunner.RunGateway(transport, size),
    "SPOT" => BenchRunner.RunSpot(transport, size),
    _ => 2,
};

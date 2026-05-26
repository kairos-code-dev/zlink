using Systems.Zlink;
using Systems.Zlink.Codecs.Json;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Streams;
using Zlink.Framework.Contracts.Timers;
using Microsoft.Extensions.Hosting;
using TicTacToe.SessionGateway.Session;
using TicTacToe.SessionGateway.Shared.Configuration;

var topology = SampleTopology.Create();
var session = args.Contains("--reconnect", StringComparer.Ordinal)
    ? topology.ReconnectSession
    : topology.PrimarySession;

await SessionServerHostFactory.Build(
        topology,
        session)
    .RunAsync();

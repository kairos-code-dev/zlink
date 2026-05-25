using Systems.Zlink;
using Systems.Zlink.Codecs.Json;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Streams;
using Zlink.Framework.Contracts.Timers;
using Bingo.Server.Session;
using Bingo.Shared.Configuration;
using Microsoft.Extensions.Hosting;

var topology = SampleTopology.Create();

await SessionServerHostFactory.Build(
        topology,
        topology.PrimarySession)
    .RunAsync();

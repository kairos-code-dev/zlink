using Bingo.Server.Session;
using Bingo.Shared.Configuration;
using Microsoft.Extensions.Hosting;

var topology = SampleTopology.Create();

await SessionServerHostFactory.Build(
        topology,
        topology.PrimarySession)
    .RunAsync();

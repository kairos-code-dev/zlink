using Bingo.Server.Infrastructure;
using Bingo.Server.Infrastructure.Configuration;
using Bingo.Server.Session;
using Microsoft.Extensions.Hosting;

var topology = SampleTopology.Create();

await SessionServerHostFactory.Build(
        topology,
        topology.PrimarySession)
    .RunAsync();

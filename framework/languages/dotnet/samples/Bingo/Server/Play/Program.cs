using Bingo.Server.Play;
using Bingo.Shared.Configuration;
using Microsoft.Extensions.Hosting;

var topology = SampleTopology.Create();
using var host = PlayServerHostFactory.Build(topology);

await host.RunAsync();

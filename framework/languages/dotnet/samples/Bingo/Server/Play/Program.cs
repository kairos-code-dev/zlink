using Bingo.Server.Infrastructure;
using Bingo.Server.Infrastructure.Configuration;
using Bingo.Server.Play;
using Microsoft.Extensions.Hosting;

var topology = SampleTopology.Create();
using var host = PlayServerHostFactory.Build(topology);

await host.RunAsync();

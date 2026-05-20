using Microsoft.Extensions.Hosting;
using TicTacToe.SessionGateway.Infrastructure.Configuration;
using TicTacToe.SessionGateway.Play;

var topology = SampleTopology.Create();
using var host = PlayServerHostFactory.Build(topology);

await host.RunAsync();

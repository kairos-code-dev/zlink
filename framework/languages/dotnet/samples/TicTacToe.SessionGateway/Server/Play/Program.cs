using Microsoft.Extensions.Hosting;
using TicTacToe.SessionGateway.Shared.Configuration;
using TicTacToe.SessionGateway.Server.Play;

var topology = SampleTopology.Create();
using var host = PlayServerHostFactory.Build(topology);

await host.RunAsync();

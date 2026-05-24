using Microsoft.Extensions.Hosting;
using TicTacToe.SessionGateway.Server.Api;
using TicTacToe.SessionGateway.Shared.Configuration;

var topology = SampleTopology.Create();
await ApiServerHostFactory.Build(topology).RunAsync();

using Microsoft.Extensions.Hosting;
using TicTacToe.SessionGateway.Api;
using TicTacToe.SessionGateway.Infrastructure.Configuration;

var topology = SampleTopology.Create();
await ApiServerHostFactory.Build(topology).RunAsync();

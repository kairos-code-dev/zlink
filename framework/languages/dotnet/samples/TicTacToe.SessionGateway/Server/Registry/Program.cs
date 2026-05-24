using Microsoft.Extensions.Hosting;
using TicTacToe.SessionGateway.Shared.Configuration;
using TicTacToe.SessionGateway.Server.Registry;

var topology = SampleTopology.Create();
await RegistryHostFactory.Build(topology).RunAsync();

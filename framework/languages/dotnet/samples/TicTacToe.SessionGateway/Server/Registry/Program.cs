using Microsoft.Extensions.Hosting;
using TicTacToe.SessionGateway.Infrastructure.Configuration;
using TicTacToe.SessionGateway.Registry;

var topology = SampleTopology.Create();
await RegistryHostFactory.Build(topology).RunAsync();

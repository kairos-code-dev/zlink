using Microsoft.Extensions.Hosting;
using TicTacToe.SessionGateway.Server.Session;
using TicTacToe.SessionGateway.Shared.Configuration;

var topology = SampleTopology.Create();
var session = args.Contains("--reconnect", StringComparer.Ordinal)
    ? topology.ReconnectSession
    : topology.PrimarySession;

await SessionServerHostFactory.Build(
        topology,
        session)
    .RunAsync();

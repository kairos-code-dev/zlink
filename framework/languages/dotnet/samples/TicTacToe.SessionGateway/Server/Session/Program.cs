using Microsoft.Extensions.Hosting;
using TicTacToe.SessionActorDispatch.Session;
using TicTacToe.SessionGateway.Infrastructure.Configuration;

var topology = SampleTopology.Create();
var session = args.Contains("--reconnect", StringComparer.Ordinal)
    ? topology.ReconnectSession
    : topology.PrimarySession;

await SessionServerHostFactory.Build(
        topology,
        session)
    .RunAsync();

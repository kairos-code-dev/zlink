using Microsoft.Extensions.Hosting;
using TicTacToe.SessionActorDispatch.Session;
using TicTacToe.SessionGateway.Infrastructure;
using TicTacToe.SessionGateway.Infrastructure.Configuration;

var topology = SampleTopology.Create();
var session = args.Contains("--reconnect", StringComparer.Ordinal)
    ? topology.ReconnectSession
    : topology.PrimarySession;
var registryMetadata = SampleRuntime.OpenRegistryMetadata();
var sessionLocations = new RegistryActorSessionLocationStore(registryMetadata, "tictactoe");
var playRoutes = new RegistryPlayRouteStore(registryMetadata, "tictactoe");

await SessionServerHostFactory.Build(
        topology,
        session,
        sessionLocations,
        playRoutes)
    .RunAsync();

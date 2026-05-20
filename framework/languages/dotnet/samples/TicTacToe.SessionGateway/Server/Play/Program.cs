using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using TicTacToe.SessionActorDispatch.Infrastructure;
using TicTacToe.SessionGateway.Infrastructure;
using TicTacToe.SessionGateway.Infrastructure.Configuration;
using TicTacToe.SessionGateway.Play;
using TicTacToe.SessionGateway.Shared.Configuration;

var topology = SampleTopology.Create();
var registryMetadata = SampleRuntime.OpenRegistryMetadata();
var sessionLocations = new RegistryActorSessionLocationStore(registryMetadata, "tictactoe");
var playRoutes = new RegistryPlayRouteStore(registryMetadata, "tictactoe");
using var host = PlayServerHostFactory.Build(topology, sessionLocations, playRoutes);

await host.Services.GetRequiredService<RegistryPlayRoutePublisher>()
    .BindInitialActorPlayRoutesAsync(
        [SampleNames.XActorId, SampleNames.OActorId],
        CancellationToken.None);

await host.RunAsync();

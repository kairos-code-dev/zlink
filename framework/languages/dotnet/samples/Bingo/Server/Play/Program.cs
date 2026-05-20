using Bingo.Server.Infrastructure;
using Bingo.Server.Infrastructure.Configuration;
using Bingo.Server.Play;
using Bingo.Shared.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;

var topology = SampleTopology.Create();
var registryMetadata = SampleRuntime.OpenRegistryMetadata();
var sessionLocations = new RegistryActorSessionLocationStore(registryMetadata, "bingo");
var playRoutes = new RegistryPlayRouteStore(registryMetadata, "bingo");
using var host = PlayServerHostFactory.Build(topology, sessionLocations, playRoutes);

await host.Services.GetRequiredService<RegistryPlayRoutePublisher>()
    .BindInitialActorPlayRoutesAsync(SampleNames.ActorIds, CancellationToken.None);

await host.RunAsync();

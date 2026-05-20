using Bingo.Server.Infrastructure;
using Bingo.Server.Infrastructure.Configuration;
using Bingo.Server.Session;
using Microsoft.Extensions.Hosting;

var topology = SampleTopology.Create();
var registryMetadata = SampleRuntime.OpenRegistryMetadata();
var sessionLocations = new RegistryActorSessionLocationStore(registryMetadata, "bingo");
var playRoutes = new RegistryPlayRouteStore(registryMetadata, "bingo");

await SessionServerHostFactory.Build(
        topology,
        topology.PrimarySession,
        sessionLocations,
        playRoutes)
    .RunAsync();

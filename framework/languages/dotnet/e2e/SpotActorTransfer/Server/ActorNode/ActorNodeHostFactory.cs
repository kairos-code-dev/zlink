using Microsoft.Extensions.Configuration;

using SpotActorTransfer.Shared;
using Systems.Zlink;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Locations.Redis;

namespace SpotActorTransfer.ActorNode;

internal static class ActorNodeHostFactory
{
    public static (WebApplication App, ServerOptions Options) Create(string[] args)
    {
        var options = ServerOptions.Parse(args, "actor-node");
        Directory.CreateDirectory(options.LogDir);
        var builder = WebApplication.CreateBuilder(args);
        builder.Configuration.Sources.Clear();
        builder.Configuration.AddInMemoryCollection();
        builder.Logging.ClearProviders();
        builder.Logging.AddSimpleConsole(console =>
        {
            console.SingleLine = true;
            console.TimestampFormat = "HH:mm:ss.fff ";
        });
        var runtimeEvidence = new RuntimeEvidenceStore();
        builder.Logging.AddProvider(new ActorHandoffEvidenceLoggerProvider(runtimeEvidence));
        builder.WebHost.UseUrls(options.HttpUrl);
        var evidence = new EvidenceStore(options.Rid, options.EvidenceFile);
        var cleanupGates = new ActorCleanupGateStore(evidence);
        builder.Services.AddSingleton(evidence);
        builder.Services.AddSingleton(runtimeEvidence);
        builder.Services.AddSingleton(new DomainStateStore(options.LogDir));
        builder.Services.AddSingleton<JoinedGateStore>();
        builder.Services.AddSingleton<TransferGateStore>();
        builder.Services.AddSingleton(cleanupGates);
        builder.Services.AddSingleton<ActorJoinTargetUseCase>();
        builder.Services.AddZLinkFramework(framework =>
        {
            framework.DefaultRequestTimeout = TimeSpan.FromMilliseconds(options.RequestTimeoutMilliseconds);
            framework.ActorTransferForwardWindow = TimeSpan.FromSeconds(5);
            var redisStore = new ZLinkRedisLocationStore(redis => redis
                .SetConnectionString(options.RedisEndpoint)
                .SetKeyPrefix(options.RedisKeyPrefix));
            framework.AddLocationStore(new CleanupGatedLocationStore(
                redisStore,
                cleanupGates));
            var locations = framework.ConfigureLocations();
            locations.HeartbeatInterval = TimeSpan.FromSeconds(1);
            locations.OwnerLeaseTtl = TimeSpan.FromSeconds(3);
            locations.PollingInterval = TimeSpan.FromMilliseconds(500);
            framework.AddHandlersFromAssemblyOf<TransferEntrySpot>();
            var mesh28 = framework.AddRouteMesh(SpotActorTransferNames.Mesh)
                .Listen(options.RouterEndpoint)
                .SetRoutingId(RoutingId.From(options.Rid))
                .SetEntrySpotRoutingId(RoutingId.From(SpotActorTransferNames.EntrySpotRid))
                .AddEntrySpot<TransferEntrySpot>()
                .AddActorFactory<TransferActorFactory>(SpotActorTransferNames.ActorTypeStateful)
                .AddActorTransferAdapter<TransferActor, TransferActorAdapter>(SpotActorTransferNames.ActorTypeStateful)
                .AddActorFactory<TransferActorFactory>(SpotActorTransferNames.ActorTypeEmptyState)
                .AddActorTransferAdapter<TransferActor, TransferActorAdapter>(SpotActorTransferNames.ActorTypeEmptyState)
                .AddActorFactory<TransferActorFactory>(SpotActorTransferNames.ActorTypeNoAdapter)
                .AddActorFactory<TransferActorFactory>(SpotActorTransferNames.ActorTypeFailLeave)
                .AddActorTransferAdapter<TransferActor, TransferActorAdapter>(SpotActorTransferNames.ActorTypeFailLeave)
                .AddActorFactory<TransferActorFactory>(SpotActorTransferNames.ActorTypeFailTransferOut)
                .AddActorTransferAdapter<TransferActor, TransferActorAdapter>(SpotActorTransferNames.ActorTypeFailTransferOut)
                .AddActorFactory<TransferActorFactory>(SpotActorTransferNames.ActorTypeFailTransferIn)
                .AddActorTransferAdapter<TransferActor, TransferActorAdapter>(SpotActorTransferNames.ActorTypeFailTransferIn)
                .AddSpotFactory<TransferUserSpot>();
            mesh28.ChannelName(SpotActorTransferNames.Mesh);
        });
        return (builder.Build(), options);
    }
}

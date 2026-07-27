using Microsoft.Extensions.Configuration;

using SpotActorTransfer.Shared;
using Systems.Zlink;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Locations.Redis;
using Zlink.Framework.Runtime.Actors;

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
        var relocationBlobs = new RelocationBlobObserver();
        var relocationMessageFlows =
            new RelocationMessageFlowEvidenceStore();
        builder.Services.AddSingleton(evidence);
        builder.Services.AddSingleton(runtimeEvidence);
        builder.Services.AddSingleton(relocationBlobs);
        builder.Services.AddSingleton(relocationMessageFlows);
        builder.Services.AddSingleton<RelocationUnitTerminalStore>();
        builder.Services.AddSingleton(new DomainStateStore(options.LogDir));
        builder.Services.AddSingleton<JoinedGateStore>();
        builder.Services.AddSingleton<TransferGateStore>();
        builder.Services.AddSingleton<TransportDeliveryGate>();
        builder.Services.AddSingleton<IZLinkActorTransportDeliveryGate>(
            services => services.GetRequiredService<TransportDeliveryGate>());
        builder.Services.AddSingleton(cleanupGates);
        builder.Services.AddSingleton<ActorJoinTargetUseCase>();
        builder.Services.AddZLinkFramework(framework =>
        {
            framework.DefaultRequestTimeout = TimeSpan.FromMilliseconds(options.RequestTimeoutMilliseconds);
            framework.ConfigureDispatch()
                .MessageFlow(ZLinkRuntimeMessageFlowMode.KeyTransitions)
                .SetRuntimeMessageFlowObserver(relocationMessageFlows);
            // The common ST-F4/F5 contract permits a short controller duration so
            // the E2E verifies cutoff semantics without coupling the scenario to
            // the independently tested owner-lease TTL.
            var redisStore = new ZLinkRedisLocationStore(redis => redis
                .SetConnectionString(options.RedisEndpoint)
                .SetKeyPrefix(options.RedisKeyPrefix));
            framework.AddLocationStore(new CleanupGatedLocationStore(
                redisStore,
                cleanupGates));
            framework.AddRelocationStore(new ObservedRelocationStore(
                new ZLinkRedisRelocationStore(redis => redis
                    .SetConnectionString(options.RedisEndpoint)
                    .SetKeyPrefix($"{options.RedisKeyPrefix}:relocation")),
                relocationBlobs));
            var locations = framework.ConfigureLocations();
            // A third node keeps a bounded stale owner route so ST-I4 can
            // exercise Message Follow through the public global Actor ID API.
            // The cache remains five seconds shorter than Message Follow.
            locations.RouteCacheMaxAge = TimeSpan.FromSeconds(2);
            locations.MessageFollowDuration = TimeSpan.FromSeconds(7);
            locations.OwnerLeaseRenewInterval = TimeSpan.FromSeconds(1);
            locations.OwnerLeaseTtl = TimeSpan.FromSeconds(10);
            locations.PollingInterval = TimeSpan.FromMilliseconds(500);
            framework.AddHandlersFromAssemblyOf<TransferEntrySpot>();
            var mesh28 = framework.AddRouteMesh(SpotActorTransferNames.Mesh)
                .Listen(options.RouterEndpoint)
                .SetRoutingIdPrefix(options.Rid)
                .SetActorLimit(30_000)
                .SetSpotLimit(5_000);
            mesh28.Objects().Server()
                .AddEntrySpot<TransferEntrySpot>()
                .AddActorFactory<TransferActor, TransferActorFactory>(
                    SpotActorTransferNames.ActorTypeStateful,
                    null,
                    ZLinkRelocationPolicy<TransferActor>
                        .Snapshot<TransferActorRelocationAdapter>())
                .AddActorFactory<TransferActor, TransferActorFactory>(
                    SpotActorTransferNames.ActorTypeEmptyState,
                    null,
                    ZLinkRelocationPolicy<TransferActor>
                        .Snapshot<TransferActorRelocationAdapter>())
                .AddActorFactory<TransferActor, TransferActorFactory>(
                    SpotActorTransferNames.ActorTypeNoAdapter,
                    null,
                    ZLinkRelocationPolicy<TransferActor>.Recreate)
                .AddActorFactory<TransferActor, TransferActorFactory>(
                    SpotActorTransferNames.ActorTypeFailLeave,
                    null,
                    ZLinkRelocationPolicy<TransferActor>
                        .Snapshot<TransferActorRelocationAdapter>())
                .AddActorFactory<TransferActor, TransferActorFactory>(
                    SpotActorTransferNames.ActorTypeFailTransferOut,
                    null,
                    ZLinkRelocationPolicy<TransferActor>
                        .Snapshot<TransferActorRelocationAdapter>())
                .AddActorFactory<TransferActor, TransferActorFactory>(
                    SpotActorTransferNames.ActorTypeFailTransferIn,
                    null,
                    ZLinkRelocationPolicy<TransferActor>
                        .Snapshot<TransferActorRelocationAdapter>())
                .AddSpotFactory<TransferUserSpot>(
                    SpotActorTransferNames.UserSpotType,
                    null,
                    ZLinkRelocationPolicy<TransferUserSpot>.Disabled)
                .AddSpotFactory<RelocationPayloadUserSpot>(
                    SpotActorTransferNames.RelocationPayloadUserSpotType,
                    new ZLinkUserSpotFactoryOptions
                    {
                        ExecutionMode = ZLinkUserSpotExecutionMode.SpotWide
                    },
                    ZLinkRelocationPolicy<RelocationPayloadUserSpot>
                        .Snapshot<RelocationPayloadUserSpotAdapter>())
                .AddInstanceSpotFactory<RelocationPayloadInstanceSpot>(
                    SpotActorTransferNames.RelocationPayloadInstanceSpotType,
                    null,
                    ZLinkRelocationPolicy<RelocationPayloadInstanceSpot>
                        .Snapshot<RelocationPayloadInstanceSpotAdapter>());
        });
        return (builder.Build(), options);
    }
}

using Microsoft.Extensions.Configuration;

using SpotActorTransfer.Shared;
using Systems.Zlink;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Actors;
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
            // The common ST-F4/F5 contract permits a short controller window so
            // the E2E verifies cutoff semantics without coupling the scenario to
            // the independently tested owner-lease TTL.
            var redisStore = new ZLinkRedisLocationStore(redis => redis
                .SetConnectionString(options.RedisEndpoint)
                .SetKeyPrefix(options.RedisKeyPrefix));
            framework.AddLocationStore(new CleanupGatedLocationStore(
                redisStore,
                cleanupGates,
                evidence));
            var locations = framework.ConfigureLocations();
            locations.RouteCacheMaxAge = TimeSpan.Zero;
            locations.RelocationForwardingWindow = TimeSpan.FromSeconds(2);
            locations.OwnerLeaseRenewInterval = TimeSpan.FromSeconds(1);
            locations.OwnerLeaseTtl = TimeSpan.FromSeconds(10);
            locations.PollingInterval = TimeSpan.FromMilliseconds(500);
            framework.AddHandlersFromAssemblyOf<TransferEntrySpot>();
            var mesh28 = framework.AddRouteMesh(SpotActorTransferNames.Mesh)
                .Listen(options.RouterEndpoint)
                .SetRoutingId(RoutingId.From(options.Rid));
            foreach (var peer in options.RoutePeers)
            {
                var separator = peer.IndexOf('=');
                if (separator <= 0 || separator == peer.Length - 1)
                    throw new InvalidOperationException(
                        $"Route peer '{peer}' must use the '<rid>=<endpoint>' format.");
                var peerRid = peer[..separator];
                if (!string.Equals(peerRid, options.Rid, StringComparison.Ordinal))
                    mesh28.PeerConnections.Connect(
                        RoutingId.From(peerRid),
                        peer[(separator + 1)..]);
            }
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
                    SpotActorTransferNames.UserSpotType(options.Rid),
                    null,
                    ZLinkRelocationPolicy<TransferUserSpot>.Disabled);
            mesh28.ChannelName(SpotActorTransferNames.Mesh);
        });
        return (builder.Build(), options);
    }
}

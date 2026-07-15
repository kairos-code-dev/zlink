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
        builder.Logging.ClearProviders();
        builder.Logging.AddSimpleConsole(console =>
        {
            console.SingleLine = true;
            console.TimestampFormat = "HH:mm:ss.fff ";
        });
        builder.WebHost.UseUrls(options.HttpUrl);
        builder.Services.AddSingleton(new EvidenceStore(options.Rid, options.EvidenceFile));
        builder.Services.AddSingleton(new DomainStateStore(options.LogDir));
        builder.Services.AddSingleton<JoinedGateStore>();
        builder.Services.AddSingleton<TransferGateStore>();
        builder.Services.AddSingleton<ActorJoinTargetUseCase>();
        builder.Services.AddZLinkFramework(framework =>
        {
            framework.ActorTransferForwardWindow = TimeSpan.FromSeconds(5);
            framework.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
                .SetConnectionString(options.RedisEndpoint)
                .SetKeyPrefix(options.RedisKeyPrefix)));
            var locations = framework.ConfigureLocations();
            locations.HeartbeatInterval = TimeSpan.FromSeconds(1);
            locations.OwnerLeaseTtl = TimeSpan.FromSeconds(3);
            locations.PollingInterval = TimeSpan.FromMilliseconds(500);
            framework.AddHandlersFromAssemblyOf<TransferEntrySpot>();
            framework.AddSpotMesh(SpotActorTransferNames.Mesh)
                .EnableRouter(options.RouterEndpoint)
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
        });
        return (builder.Build(), options);
    }
}

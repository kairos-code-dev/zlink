using Microsoft.Extensions.Configuration;

using SpotActorTransfer.Shared;
using Systems.Zlink;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Locations.Redis;

namespace SpotActorTransfer.SessionGateway;

internal static class SessionGatewayHostFactory
{
    public static WebApplication Create(string[] args)
    {
        var options = GatewayOptions.Parse(args);
        var builder = WebApplication.CreateBuilder(args);
        builder.Configuration.Sources.Clear();
        builder.Configuration.AddInMemoryCollection();
        builder.WebHost.UseUrls(options.HttpUrl);
        builder.Services.AddSingleton(new GatewayEvidenceStore(options.Rid, options.EvidenceFile));
        builder.Services.AddZLinkFramework(framework =>
        {
            framework.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
                .SetConnectionString(options.RedisEndpoint)
                .SetKeyPrefix(options.RedisKeyPrefix)));
            var mesh27 = framework.AddRouteMesh(SpotActorTransferNames.Mesh)
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
                    mesh27.PeerConnections.Connect(
                        RoutingId.From(peerRid),
                        peer[(separator + 1)..]);
            }
            mesh27.Objects().Client();
            mesh27.ChannelName(SpotActorTransferNames.Mesh);
            framework.AddStreamNode($"{SpotActorTransferNames.Mesh}-stream-{options.Rid}")
                .Bind(options.StreamEndpoint)
                .EnableActorDispatch(SpotActorTransferNames.Mesh)
                .AddSession<TransferSession>();
        });
        var app = builder.Build();
        app.MapGet("/health", () => Results.Ok(new { status = "ok", options.Rid }));
        return app;
    }
}

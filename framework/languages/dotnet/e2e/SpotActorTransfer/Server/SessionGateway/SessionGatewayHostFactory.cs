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
            framework.AddLocationStore(new ZLinkRedisLocationStore(redis => { redis.ConnectionString = options.RedisEndpoint; redis.KeyPrefix = options.RedisKeyPrefix; }));
            var mesh27 = framework.AddRouteMesh(SpotActorTransferNames.Mesh)
                .Listen(options.RouterEndpoint)
                .SetRoutingIdPrefix(options.Rid);
            mesh27.Objects().Client();
            framework.AddStreamNode($"{SpotActorTransferNames.Mesh}-stream-{options.Rid}")
                .Bind(options.StreamEndpoint)
                .EnableActorDispatch()
                .AddSession<TransferSession>();
        });
        var app = builder.Build();
        app.MapGet("/health", () => Results.Ok(new { status = "ok", options.Rid }));
        return app;
    }
}

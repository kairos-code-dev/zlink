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
        builder.WebHost.UseUrls(options.HttpUrl);
        builder.Services.AddSingleton(new GatewayEvidenceStore(options.Rid, options.EvidenceFile));
        builder.Services.AddZLinkFramework(framework =>
        {
            framework.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
                .SetConnectionString(options.RedisEndpoint)
                .SetKeyPrefix(options.RedisKeyPrefix)));
            framework.AddSpotMesh(SpotActorTransferNames.Mesh)
                .EnableRouter(options.RouterEndpoint)
                .SetRoutingId(RoutingId.From(options.Rid));
            framework.AddStreamNode($"{SpotActorTransferNames.Mesh}-stream-{options.Rid}")
                .Bind(options.StreamEndpoint)
                .RegisterSession<TransferSession>();
        });
        var app = builder.Build();
        app.MapGet("/health", () => Results.Ok(new { status = "ok", options.Rid }));
        return app;
    }
}

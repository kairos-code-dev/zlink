using Bingo.Server.Configuration;
using Bingo.Server.Session.Sessions;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Locations.Redis;
using Zlink.Framework.Codecs.Protobuf;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Samples.Logging;

namespace Bingo.Server.Session;

public static class SessionServerHostFactory
{
    public static IHost Build(
        SampleTopology topology,
        SampleSessionNode session)
    {
        var builder = Host.CreateApplicationBuilder();
        SampleLogging.Configure(
            builder.Logging,
            SampleLogging.DirectoryFromEnvironment("BINGO_LOG_DIR"),
            "session");
        builder.Services.AddSingleton(topology);
        builder.Services.AddSingleton(session);
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
                .SetConnectionString(topology.RedisEndpoint)
                .SetKeyPrefix(topology.RedisKeyPrefix)));
            options.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(SampleFlowLog.Path("session"))
                .TraceLabel("session");
            options.AddHandlersFromAssemblyOf(typeof(SessionServerHostFactory));
            options.Codecs.Use(ZLinkProtobufCodec.Default);
            options.AddClientServerChannel(SampleNames.ApiChannel)
                .EnableClient();
            options.AddRouteMesh(SampleNames.PlayChannel)
                .EnableServer(session.PlayRouteEndpoint)
                .EnableClient()
                .SetRoutingId(session.PlayRouteRid);
            options.AddSpotMesh(SampleNames.RoomSpotDiscovery)
                .EnableRouter(session.RouterEndpoint)
                .SetRoutingId(session.RoutingId);
            options.AddStreamNode(SampleNames.StreamNode)
                .Bind(session.StreamEndpoint)
                .RegisterSession<BingoSession>();
        });

        return builder.Build();
    }
}

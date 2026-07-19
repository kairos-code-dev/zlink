using Microsoft.Extensions.Configuration;
using Systems.Zlink;

using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using SupportChat.Server.Configuration;
using SupportChat.Server.Session.Sessions;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Locations.Redis;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Samples.Logging;

namespace SupportChat.Server.Session;

public static class SessionServerHostFactory
{
    public static IHost Build(
        SampleTopology topology,
        SampleSessionNode session,
        string logDirectory)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Configuration.Sources.Clear();
        builder.Configuration.AddInMemoryCollection();
        SampleLogging.Configure(
            builder.Logging,
            logDirectory,
            "session");
        builder.Services.AddSingleton(topology);
        builder.Services.AddZLinkFramework(options =>
        {
            // Channel clients wire through Redis discovery; the session's first
            // authenticate can arrive before the api-channel dealer connects,
            // so the submit window covers the discovery hand-off.
            options.DefaultSocketSendTimeout = TimeSpan.FromSeconds(10);
            options.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
                .SetConnectionString(topology.RedisEndpoint)
                .SetKeyPrefix(topology.RedisKeyPrefix)));
            options.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(SampleFlowLog.Path(logDirectory, "session"))
                .TraceLabel("session");
            options.AddHandlersFromAssemblyOf(typeof(SessionServerHostFactory));
            var apiMesh = options.AddRouteMesh(SampleNames.ApiChannel)
                .Listen("tcp://127.0.0.1:0")
                .SetRoutingId(RoutingId.From("session-api"));
            apiMesh.ChannelName(SampleNames.ApiChannel).SetWeight(0);
            var supportMesh = options.AddRouteMesh(SampleNames.SupportChannel)
                .Listen("tcp://127.0.0.1:0")
                .SetRoutingId(RoutingId.From("session-support"));
            supportMesh.ChannelName(SampleNames.SupportChannel).SetWeight(0);
            var mesh6 = options.AddRouteMesh(SampleNames.SupportSpotDiscovery)
                .Listen(session.RouterEndpoint)
                .SetRoutingId(session.RoutingId);
            mesh6.ChannelName(SampleNames.SupportSpotDiscovery);
            options.AddStreamNode(SampleNames.StreamNode)
                .Bind(session.StreamEndpoint)
                .EnableActorDispatch(SampleNames.SupportSpotDiscovery)
                .AddSession<SupportChatSession>();
        });

        return builder.Build();
    }
}

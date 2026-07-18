using Microsoft.Extensions.Configuration;

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
            options.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
                .SetConnectionString(topology.RedisEndpoint)
                .SetKeyPrefix(topology.RedisKeyPrefix)));
            options.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(SampleFlowLog.Path(logDirectory, "session"))
                .TraceLabel("session");
            options.AddHandlersFromAssemblyOf(typeof(SessionServerHostFactory));
            options.AddClientServerChannel(SampleNames.ApiChannel)
                .EnableClient();
            options.AddClientServerChannel(SampleNames.SupportChannel)
                .EnableClient();
            var mesh6 = options.AddRouteMesh(SampleNames.SupportSpotDiscovery)
                .Listen(session.RouterEndpoint)
                .SetRoutingId(session.RoutingId);
            mesh6.ChannelName(SampleNames.SupportSpotDiscovery);
            options.AddStreamNode(SampleNames.StreamNode)
                .Bind(session.StreamEndpoint)
                .RegisterSession<SupportChatSession>();
        });

        return builder.Build();
    }
}

using Systems.Zlink;
using Microsoft.Extensions.Configuration;

using Microsoft.Extensions.Hosting;
using SupportChat.Server.Configuration;
using SupportChat.Server.Api.Handlers;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Locations.Redis;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Samples.Logging;

namespace SupportChat.Server.Api;

public static class ApiServerHostFactory
{
    public static IHost Build(SampleTopology topology, string logDirectory)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Configuration.Sources.Clear();
        builder.Configuration.AddInMemoryCollection();
        SampleLogging.Configure(
            builder.Logging,
            logDirectory,
            "api");
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
                .SetConnectionString(topology.RedisEndpoint)
                .SetKeyPrefix(topology.RedisKeyPrefix)));
            options.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(SampleFlowLog.Path(logDirectory, "api"))
                .TraceLabel("api");
            options.AddHandlersFromAssemblyOf(typeof(ApiServerHostFactory));
            var apiMesh = options.AddRouteMesh(SampleNames.ApiChannel)
                .Listen(topology.ApiChannelEndpoint)
                // Discovery clients dial this server through its descriptor
                // row, which needs a concrete routing id to be advertised.
                .SetRoutingId(RoutingId.From("1101"));
            apiMesh.ChannelName(SampleNames.ApiChannel)
                .AddRequestHandler<AuthenticateUserHandler>()
                .AddRequestHandler<OpenConversationHandler>();
            var supportMesh = options.AddRouteMesh(SampleNames.SupportChannel)
                .Listen("tcp://127.0.0.1:0")
                .SetRoutingId(RoutingId.From("1102"));
            supportMesh.ChannelName(SampleNames.SupportChannel).SetWeight(0);
        });

        return builder.Build();
    }
}

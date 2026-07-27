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
                .MessageFlow(ZLinkRuntimeMessageFlowMode.KeyTransitions)
                .TraceLogFile(SampleFlowLog.Path(logDirectory, "api"))
                .TraceLabel("api");
            options.AddHandlersFromAssemblyOf(typeof(ApiServerHostFactory));
            var mesh = options.AddRouteMesh(SampleNames.MeshName)
                .Listen(topology.MeshEndpoint)
                .SetRoutingIdPrefix("support-api");
            mesh.ChannelName(SampleNames.ApiChannel)
                .AddHandlerGroup("api");
            mesh.ChannelName(SampleNames.SupportChannel).SetWeight(0);
            mesh.ChannelName(SampleNames.MeshName).SetWeight(0);
        });

        return builder.Build();
    }
}

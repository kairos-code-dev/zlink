using Microsoft.Extensions.Configuration;

using DeliveryDispatch.Server.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Locations.Redis;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Samples.Logging;

namespace DeliveryDispatch.Server.CourierSession;

public static class CourierSessionHostFactory
{
    public static IHost Build(SampleConfiguration configuration)
    {
        var topology = configuration.Topology;
        var builder = Host.CreateApplicationBuilder();
        builder.Configuration.Sources.Clear();
        builder.Configuration.AddInMemoryCollection();
        SampleLogging.Configure(
            builder.Logging,
            configuration.Role.LogDir,
            "courier-session");
        builder.Services.AddSingleton(configuration);
        builder.Services.AddSingleton(topology);
        builder.Services.AddSingleton<CourierSessionBinder>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
                .SetConnectionString(topology.RedisEndpoint)
                .SetKeyPrefix(topology.RedisKeyPrefix)));
            options.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(configuration.FlowLogPath)
                .TraceLabel("courier-session");
            options.AddHandlersFromAssemblyOf(typeof(CourierSessionHostFactory));
            var mesh = options.AddRouteMesh(SampleNames.MeshName)
                .Listen(topology.MeshEndpoint)
                .SetRoutingIdPrefix("courier-session");
            mesh.ChannelName(SampleNames.MeshName).SetWeight(0);
            mesh.ChannelName(SampleNames.DispatchChannel).SetWeight(0);
            mesh.ChannelName(SampleNames.TrackingRouteChannel).SetWeight(0);
            options.AddStreamNode(SampleNames.CourierStreamNode)
                .Bind(topology.CourierStreamEndpoint)
                .EnableActorDispatch()
                .AddSession<CourierSession>();
        });

        return builder.Build();
    }
}

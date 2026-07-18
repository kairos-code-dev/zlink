using Microsoft.Extensions.Configuration;

using Bingo.Server.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Locations.Redis;
using Zlink.Framework.Codecs.Protobuf;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Samples.Logging;

namespace Bingo.Server.Api;

public static class ApiServerHostFactory
{
    public static IHost Build(
        SampleRuntimeConfiguration<SampleApiNode> configuration)
    {
        var node = configuration.Node;
        var nodeName = configuration.NodeName;
        var logDirectory = configuration.LogDirectory;
        var traceLabel = $"api-{nodeName}";
        var builder = Host.CreateApplicationBuilder();
        builder.Configuration.Sources.Clear();
        builder.Configuration.AddInMemoryCollection();
        SampleLogging.Configure(
            builder.Logging,
            logDirectory,
            traceLabel);
        builder.Services.AddSingleton<BingoPlayerRecordStore>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
                .SetConnectionString(configuration.RedisEndpoint)
                .SetKeyPrefix(configuration.RedisKeyPrefix)));
            options.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(SampleFlowLog.Path(logDirectory, traceLabel))
                .TraceLabel(traceLabel);
            options.AddHandlersFromAssemblyOf(typeof(ApiServerHostFactory));
            options.Codecs.Use(ZLinkProtobufCodec.Default);
            options.AddClientServerChannel(SampleNames.ApiChannel)
                .UseAllocatedRoutingId(slotCount: 2, routingIdPrefix: "api")
                .SetRoutingIdAllocationGroup(SampleNames.ApiAllocationGroup)
                .EnableServer(node.ChannelEndpoint)
                .AddHandlerGroup("api");
            var mesh2 = options.AddRouteMesh(SampleNames.RoomSpotDiscovery)
                .UseAllocatedRoutingId(slotCount: 2, routingIdPrefix: "api")
                .SetRoutingIdAllocationGroup(SampleNames.ApiAllocationGroup)
                .Listen(node.SpotRouterEndpoint);
            mesh2.ChannelName(SampleNames.RoomSpotDiscovery);
        });
        builder.Services.AddSingleton(new BingoRoutingIdReport(
            "api",
            SampleNames.ApiAllocationGroup,
            [SampleNames.ApiChannel, SampleNames.RoomSpotDiscovery]));
        builder.Services.AddHostedService<BingoRoutingIdReporter>();

        return builder.Build();
    }
}

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
        SampleTopology topology,
        SampleApiNode node,
        string nodeName,
        string logDirectory)
    {
        var traceLabel = $"api-{nodeName}";
        var builder = Host.CreateApplicationBuilder();
        SampleLogging.Configure(
            builder.Logging,
            logDirectory,
            traceLabel);
        builder.Services.AddSingleton<BingoPlayerRecordStore>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
                .SetConnectionString(topology.RedisEndpoint)
                .SetKeyPrefix(topology.RedisKeyPrefix)));
            options.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(SampleFlowLog.Path(logDirectory, traceLabel))
                .TraceLabel(traceLabel);
            options.AddHandlersFromAssemblyOf(typeof(ApiServerHostFactory));
            options.Codecs.Use(ZLinkProtobufCodec.Default);
            options.AddClientServerChannel(SampleNames.ApiChannel)
                .UseAllocatedRoutingId(slotCount: 2, routingIdPrefix: "api")
                .SetRoutingIdAllocationGroup("bingo.api")
                .EnableServer(node.ChannelEndpoint)
                .AddHandlerGroup("api");
            options.AddClientServerChannel(SampleNames.PlayChannel)
                .EnableClient();
        });
        builder.Services.AddSingleton(new BingoRoutingIdReport(
            "api",
            "bingo.api",
            [SampleNames.ApiChannel]));
        builder.Services.AddHostedService<BingoRoutingIdReporter>();

        return builder.Build();
    }
}

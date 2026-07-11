using Bingo.Server.Configuration;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Locations.Redis;
using Zlink.Framework.Codecs.Protobuf;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Samples.Logging;

namespace Bingo.Server.Api;

public static class ApiServerHostFactory
{
    public static IHost Build(SampleTopology topology, SampleApiNode node)
    {
        var traceLabel = $"api-{node.RouteRid}";
        var builder = Host.CreateApplicationBuilder();
        SampleLogging.Configure(
            builder.Logging,
            SampleLogging.DirectoryFromEnvironment("BINGO_LOG_DIR"),
            traceLabel);
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
                .SetConnectionString(topology.RedisEndpoint)
                .SetKeyPrefix(topology.RedisKeyPrefix)));
            options.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(SampleFlowLog.Path(traceLabel))
                .TraceLabel(traceLabel);
            options.AddHandlersFromAssemblyOf(typeof(ApiServerHostFactory));
            options.Codecs.Use(ZLinkProtobufCodec.Default);
            options.AddClientServerChannel(SampleNames.ApiChannel)
                .EnableServer(node.ChannelEndpoint)
                .SetRoutingId(node.RouteRid)
                .AddHandlerGroup("api");
            options.AddClientServerChannel(SampleNames.PlayChannel)
                .EnableClient(topology.PlayA.PlayChannelEndpoint)
                .EnableClient(topology.PlayB.PlayChannelEndpoint);
        });

        return builder.Build();
    }
}

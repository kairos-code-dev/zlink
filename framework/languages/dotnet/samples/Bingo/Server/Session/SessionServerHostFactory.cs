using Microsoft.Extensions.Configuration;

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
        SampleRuntimeConfiguration<SampleSessionNode> configuration)
    {
        var session = configuration.Node;
        var nodeName = configuration.NodeName;
        var logDirectory = configuration.LogDirectory;
        var traceLabel = $"session-{nodeName}";
        var builder = Host.CreateApplicationBuilder();
        builder.Configuration.Sources.Clear();
        builder.Configuration.AddInMemoryCollection();
        SampleLogging.Configure(
            builder.Logging,
            logDirectory,
            traceLabel);
        builder.Services.AddSingleton(configuration);
        builder.Services.AddSingleton(session);
        builder.Services.AddBingoMetrics();
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
                .SetConnectionString(configuration.RedisEndpoint)
                .SetKeyPrefix(configuration.RedisKeyPrefix)));
            options.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(SampleFlowLog.Path(logDirectory, traceLabel))
                .TraceLabel(traceLabel);
            options.AddHandlersFromAssemblyOf(typeof(SessionServerHostFactory));
            options.Codecs.Use(ZLinkProtobufCodec.Default);
            var mesh = options.AddRouteMesh(SampleNames.MeshName)
                .SetRoutingIdPrefix("session")
                .Listen(session.MeshEndpoint);
            mesh.Channel(SampleNames.ApiChannel).Client();
            options.AddStreamNode(SampleNames.StreamNode)
                .Bind(session.StreamEndpoint)
                .EnableActorDispatch()
                .AddSession<BingoSession>();
        });
        builder.Services.AddSingleton(new BingoRoutingIdReport(
            "session",
            SampleNames.MeshName));
        builder.Services.AddHostedService<BingoRoutingIdReporter>();
        return builder.Build();
    }
}

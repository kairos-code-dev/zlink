using PubSub.Server.Subscriber.Configuration;
using PubSub.Server.Subscriber.Handlers;
using PubSub.Shared;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Locations.Redis;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Eventing;

namespace PubSub.Server.Subscriber;

internal static class SubscriberHostFactory
{
    public static WebApplication Create(string[] args)
    {
        var options = SubscriberOptions.Parse(args);
        var builder = HostFactorySupport.CreateBuilder(args, options.HttpUrl, options.LogDir);
        builder.Services.AddSingleton(options);
        builder.Services.AddSingleton(new EvidenceStore(options.Rid, options.EvidenceFile));
        builder.Services.AddSingleton(new HandlerDelayOptions(options.HandlerDelayMs));
        builder.Services.AddScoped<IZLinkRuntimeEventHandler<ZLinkSocketEvent>, SocketEvidenceRecorder>();
        builder.Services.AddZLinkFramework(framework =>
        {
            framework.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
                .SetConnectionString(options.RedisEndpoint)
                .SetKeyPrefix(options.RedisKeyPrefix)));
            ConfigureFlow(framework.ConfigureDispatch(), options.LogDir, options.Rid);
            // The subscriber dials the publisher rows it discovers in the
            // location store; no endpoint is configured here.
            var fanout = framework.AddFanoutChannel(PubSubNames.Channel);
            var subscriber = string.IsNullOrWhiteSpace(options.PublisherEndpoint)
                ? fanout.EnableSubscriber()
                : fanout.EnableSubscriber(options.PublisherEndpoint);
            subscriber.AddPublishHandler<EventMsgHandler, EventMsg>("EventMsg");
        });
        builder.Services.AddZLinkMonitoring(monitor => monitor.AddSocketEvents(
            PubSubNames.SubscriberSocketSource,
            ZLinkSocketEventKind.Connected,
            ZLinkSocketEventKind.ConnectionReady,
            ZLinkSocketEventKind.HandshakeFailed,
            ZLinkSocketEventKind.Disconnected));
        var app = builder.Build();
        app.MapOperationalEndpoints("subscriber", options.Rid);
        return app;
    }

    private static void ConfigureFlow(IZLinkDispatchOptions dispatch, string logDir, string rid)
    {
        dispatch.SetMessageFlowObserver<EvidenceDispatchErrorObserver>()
            .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .TraceLogFile(Path.Combine(logDir, $"{rid}-flow.log"))
            .TraceLabel(rid);
    }
}

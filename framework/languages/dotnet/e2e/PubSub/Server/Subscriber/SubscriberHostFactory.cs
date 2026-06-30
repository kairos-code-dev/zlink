using PubSub.Server.Subscriber.Configuration;
using PubSub.Server.Subscriber.Handlers;
using PubSub.Shared;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Dispatch;

namespace PubSub.Server.Subscriber;

internal static class SubscriberHostFactory
{
    public static WebApplication Create(string[] args)
    {
        var options = SubscriberOptions.Parse(args);
        var builder = HostFactorySupport.CreateBuilder(args, options.HttpUrl, options.LogDir);
        builder.Services.AddSingleton(options);
        builder.Services.AddSingleton(new EvidenceStore(options.EvidenceFile));
        builder.Services.AddSingleton(new HandlerDelayOptions(options.HandlerDelayMs));

        builder.Services.AddZLinkFramework(framework =>
        {
            framework.UseDiscovery().AddRegistryEndpoint(options.RegistryRouterEndpoint);
            ConfigureFlow(framework.ConfigureDispatch(), options.LogDir, options.Rid);
            framework.AddFanoutChannel(PubSubNames.Channel)
                .EnableSubscriber(options.PublisherEndpoint)
                .AddPublishHandler<EventNotifyHandler, EventNotify>("EventNotify");
        });

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
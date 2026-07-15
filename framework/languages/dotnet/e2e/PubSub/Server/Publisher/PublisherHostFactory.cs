using PubSub.Server.Publisher.Configuration;
using PubSub.Server.Publisher.Endpoints;
using PubSub.Shared;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Locations.Redis;
using Zlink.Framework.Contracts.Dispatch;

namespace PubSub.Server.Publisher;

internal static class PublisherHostFactory
{
    public static WebApplication Create(string[] args)
    {
        var options = PublisherOptions.Parse(args);
        var builder = HostFactorySupport.CreateBuilder(args, options.HttpUrl, options.LogDir);
        builder.Services.AddSingleton(options);
        builder.Services.AddSingleton(new EvidenceStore(options.Rid, options.EvidenceFile));

        builder.Services.AddZLinkFramework(framework =>
        {
            framework.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
                .SetConnectionString(options.RedisEndpoint)
                .SetKeyPrefix(options.RedisKeyPrefix)));
            ConfigureFlow(framework.ConfigureDispatch(), options.LogDir, options.Rid);
            framework.AddFanoutChannel(PubSubNames.Channel)
                .EnablePublisher(options.PublisherEndpoint);
        });

        var app = builder.Build();
        app.MapOperationalEndpoints("publisher", options.Rid);
        app.MapPublisherEndpoints();
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

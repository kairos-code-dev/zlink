using Microsoft.Extensions.DependencyInjection;
using Systems.Zlink;
using Zlink.Framework.AspNetCore;

internal static class TestHostScenarioConfigurator
{
    public static void Configure(IServiceCollection services, TestHostOptions options)
    {
        switch (options.Mode)
        {
            case "idle":
                return;
            case "registry":
                throw new InvalidOperationException("The core registry runtime has been removed.");
            case "channel-server":
                ConfigureChannelServer(services, options);
                return;
            case "channel-client":
                ConfigureChannelClient(services, options);
                return;
            case "channel-subscriber":
                ConfigureChannelSubscriber(services, options);
                return;
            case "channel-publisher":
                ConfigureChannelPublisher(services, options);
                return;
            case "route-server":
                ConfigureRouteServer(services, options);
                return;
            case "route-client":
                ConfigureRouteClient(services, options);
                return;
            case "spot-node":
                ConfigureSpotNode(services, options);
                return;
            case "stream-raw":
                ConfigureStreamRawNode(services, options);
                return;
            case "stream-client":
                ConfigureStreamClient(services, options);
                return;
            default:
                throw new InvalidOperationException($"Unsupported test host mode '{options.Mode}'.");
        }
    }

    private static void ConfigureChannelServer(IServiceCollection services, TestHostOptions options)
    {
        services.AddSingleton(new TestHostEventSink(options.EventFilePath));
        services.AddZLinkFramework(framework =>
        {
            var channel = framework.AddClientServerChannel(options.ChannelName
                                                           ?? throw new InvalidOperationException(
                                                               "Channel server mode requires --channel-name."))
                .EnableServer(options.ServerEndpoint
                              ?? throw new InvalidOperationException(
                                  "Channel server mode requires --server-endpoint."));
            channel
                .AddRequestHandler<TestHostProfileRequestHandler, TestHostProfileRequest, TestHostProfileReply>();
            channel.AddSendHandler<TestHostProfileSendHandler, TestHostProfileSend>();
        });
    }

    private static void ConfigureChannelClient(IServiceCollection services, TestHostOptions options)
    {
        services.AddSingleton(new TestHostEventSink(options.EventFilePath));
        services.AddZLinkFramework(framework =>
        {
            {
                framework.AddClientServerChannel(options.ChannelName
                                                 ?? throw new InvalidOperationException(
                                                     "Channel client mode requires --channel-name."))
                    .EnableClient(options.ServerEndpoint
                                  ?? throw new InvalidOperationException(
                                      "Channel client mode requires --server-endpoint."));
            }
        });
        services.AddHostedService(provider =>
            new ChannelClientStartupRequestHostedService(
                provider.GetRequiredService<IZLinkChannelClient>(),
                provider.GetRequiredService<TestHostEventSink>(),
                options.ChannelName!,
                options.PublishValue ?? "dotnet-to-node"));
    }

    private static void ConfigureChannelSubscriber(IServiceCollection services, TestHostOptions options)
    {
        services.AddSingleton(new TestHostEventSink(options.EventFilePath));
        services.AddZLinkFramework(framework =>
        {
            framework.AddHandlersFromAssemblyOf<Program>();
            var channel = framework.AddFanoutChannel(options.ChannelName
                                                     ?? throw new InvalidOperationException(
                                                         "Channel subscriber mode requires --channel-name."));
            if (!string.IsNullOrWhiteSpace(options.PublisherEndpoint))
                channel.EnableSubscriber(options.PublisherEndpoint);
            else
                channel.EnableSubscriber();
            channel.AddHandlerGroup("testhost-channel-events");
        });
    }

    private static void ConfigureChannelPublisher(IServiceCollection services, TestHostOptions options)
    {
        services.AddZLinkFramework(framework =>
        {
            framework.AddFanoutChannel(options.ChannelName
                                       ?? throw new InvalidOperationException(
                                           "Channel publisher mode requires --channel-name."))
                .EnablePublisher(options.PublisherEndpoint
                                 ?? throw new InvalidOperationException(
                                     "Channel publisher mode requires --publisher-endpoint."));
        });

        if (!string.IsNullOrWhiteSpace(options.PublishTopic))
            services.AddHostedService(provider =>
                new ChannelStartupPublishHostedService(
                    provider.GetRequiredService<IZLinkFanoutClient>(),
                    options.ChannelName!,
                    options.PublishTopic!,
                    options.PublishValue ?? "startup"));
    }

    private static void ConfigureRouteServer(IServiceCollection services, TestHostOptions options)
    {
        services.AddSingleton(new TestHostEventSink(options.EventFilePath));
        services.AddZLinkFramework(framework =>
        {
            framework.AddRouteMeshChannel(options.ChannelName
                                          ?? throw new InvalidOperationException(
                                              "Route server mode requires --channel-name."))
                .EnableServer(options.ServerEndpoint
                              ?? throw new InvalidOperationException(
                                  "Route server mode requires --server-endpoint."))
                .SetRoutingId(RoutingId.From("dotnet-route"))
                .AddRequestHandler<TestHostRouteRequestHandler, TestHostRouteRequest, TestHostRouteReply>();
        });
    }

    private static void ConfigureRouteClient(IServiceCollection services, TestHostOptions options)
    {
        services.AddSingleton(new TestHostEventSink(options.EventFilePath));
        services.AddZLinkFramework(framework =>
        {
            framework.AddRouteMeshChannel(options.ChannelName
                                          ?? throw new InvalidOperationException(
                                              "Route client mode requires --channel-name."))
                .EnableClient(options.ServerEndpoint
                              ?? throw new InvalidOperationException(
                                  "Route client mode requires --server-endpoint."));
        });
        services.AddHostedService(provider =>
            new RouteClientStartupRequestHostedService(
                provider.GetRequiredService<IZLinkRouteClient>(),
                provider.GetRequiredService<TestHostEventSink>(),
                options.ChannelName!,
                options.PublishValue ?? "dotnet-route-to-node"));
    }

    private static void ConfigureSpotNode(IServiceCollection services, TestHostOptions options)
    {
        services.AddSingleton(new TestHostEventSink(options.EventFilePath));
        services.AddScoped<StartupStageSubscriptionHandler>();
        services.AddZLinkFramework(framework =>
        {
            {
                var mesh = framework.AddRouteMesh(options.DiscoveryChannelName
                                                  ?? throw new InvalidOperationException(
                                                      "SPOT node mode requires --discovery-channel."));
                mesh.ChannelName(options.DiscoveryChannelName
                                                  ?? throw new InvalidOperationException(
                                                      "SPOT node mode requires --discovery-channel."));
                _ = options.SpotNodeName
                    ?? throw new InvalidOperationException("SPOT node mode requires --spot-node-name.");
                var spotBindEndpoint = options.SpotBindEndpoint
                                       ?? throw new InvalidOperationException(
                                           "SPOT node mode requires --spot-bind-endpoint.");
                mesh.Listen(spotBindEndpoint);

                if (options.EnableSpotFactory) mesh.AddSpotFactory<StartupStageSpot>();
            }
        });

        if (options.CreateSpot)
            services.AddHostedService(provider =>
                new StartupSpotCreationHostedService(
                    provider.GetRequiredService<IZLinkSpotManager>()));

        if (!string.IsNullOrWhiteSpace(options.AttachSpotPublisherChannel)
            && !string.IsNullOrWhiteSpace(options.PublishTopic))
            services.AddHostedService(provider =>
                new SpotStartupPublishHostedService(
                    provider.GetRequiredService<IZLinkSpotPublisherClient>(),
                    options.AttachSpotPublisherChannel!,
                    options.PublishTopic!,
                    options.PublishValue ?? "startup"));
    }

    private static void ConfigureStreamRawNode(IServiceCollection services, TestHostOptions options)
    {
        services.AddSingleton(new TestHostEventSink(options.EventFilePath));
        services.AddSingleton<TestHostRawStreamRecorder>();
        services.AddZLinkFramework(framework =>
        {
            if (!string.IsNullOrWhiteSpace(options.EventFilePath))
                framework.ConfigureDispatch()
                    .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                    .TraceLogFile(options.EventFilePath + ".flow")
                    .TraceLabel("dotnet-test-host");
            {
                var stream = framework.AddStreamNode("stream.raw");
                stream.Bind(options.StreamEndpoint
                            ?? throw new InvalidOperationException("STREAM raw mode requires --stream-endpoint."));
                stream.RegisterSession<TestHostRawStreamSession>();
            }
        });
    }

    private static void ConfigureStreamClient(IServiceCollection services, TestHostOptions options)
    {
        services.AddSingleton(new TestHostEventSink(options.EventFilePath));
        services.AddHostedService(provider =>
            new StreamClientStartupRequestHostedService(
                provider.GetRequiredService<TestHostEventSink>(),
                options.StreamEndpoint
                ?? throw new InvalidOperationException("STREAM client mode requires --stream-endpoint."),
                options.PublishValue ?? "dotnet-to-node"));
    }
}

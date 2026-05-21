using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework;
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
                ConfigureRegistry(services, options);
                return;
            case "channel-server":
                ConfigureChannelServer(services, options);
                return;
            case "channel-subscriber":
                ConfigureChannelSubscriber(services, options);
                return;
            case "channel-publisher":
                ConfigureChannelPublisher(services, options);
                return;
            case "spot-node":
                ConfigureSpotNode(services, options);
                return;
            case "stream-raw":
                ConfigureStreamRawNode(services, options);
                return;
            default:
                throw new InvalidOperationException($"Unsupported test host mode '{options.Mode}'.");
        }
    }

    private static void ConfigureRegistry(IServiceCollection services, TestHostOptions options)
    {
        services.AddZLinkRegistry(registry =>
        {
            registry.PubEndpoint = options.RegistryPubEndpoint
                ?? throw new InvalidOperationException("Registry mode requires --registry-pub-endpoint.");
            registry.RouterEndpoint = options.RegistryRouterEndpoint
                ?? throw new InvalidOperationException("Registry mode requires --registry-router-endpoint.");

            if (options.RegistryId is uint registryId)
            {
                registry.RegistryId = registryId;
            }
        });
    }

    private static void ConfigureChannelServer(IServiceCollection services, TestHostOptions options)
    {
        services.AddZLinkFramework(framework =>
        {
            if (!string.IsNullOrWhiteSpace(options.DiscoveryEndpoint))
            {
                framework.UseDiscovery(discovery =>
                {
                    discovery.Add(options.DiscoveryEndpoint);
                });
            }

            framework.AddClientServerChannel(
                options.ChannelName
                    ?? throw new InvalidOperationException("Channel server mode requires --channel-name."),
                channel =>
                {
                    channel.EnableServer(server =>
                    {
                        server.Bind(options.ServerEndpoint
                            ?? throw new InvalidOperationException("Channel server mode requires --server-endpoint."));
                    });
                    channel.AddRequestHandler<TestHostProfileRequestHandler, TestHostProfileRequest, TestHostProfileReply>();
                });
        });
    }

    private static void ConfigureChannelSubscriber(IServiceCollection services, TestHostOptions options)
    {
        services.AddSingleton(new TestHostEventSink(options.EventFilePath));
        services.AddZLinkFramework(framework =>
        {
            framework.AddHandlersFromAssemblyOf<Program>();
            framework.UseDiscovery(discovery =>
            {
                discovery.Add(options.DiscoveryEndpoint
                    ?? throw new InvalidOperationException("Channel subscriber mode requires --discovery-endpoint."));
            });

            framework.AddFanoutChannel(
                options.ChannelName
                    ?? throw new InvalidOperationException("Channel subscriber mode requires --channel-name."),
                channel =>
                {
                    channel.EnableSubscriber();
                    channel.AddHandlerGroup("testhost-channel-events");
                });
        });
    }

    private static void ConfigureChannelPublisher(IServiceCollection services, TestHostOptions options)
    {
        services.AddZLinkFramework(framework =>
        {
            if (!string.IsNullOrWhiteSpace(options.DiscoveryEndpoint))
            {
                framework.UseDiscovery(discovery =>
                {
                    discovery.Add(options.DiscoveryEndpoint);
                });
            }

            framework.AddFanoutChannel(
                options.ChannelName
                    ?? throw new InvalidOperationException("Channel publisher mode requires --channel-name."),
                channel =>
                {
                    channel.EnablePublisher(publisher =>
                    {
                        publisher.Bind(options.PublisherEndpoint
                            ?? throw new InvalidOperationException("Channel publisher mode requires --publisher-endpoint."));
                    });
                });
        });

        if (!string.IsNullOrWhiteSpace(options.PublishTopic))
        {
            services.AddHostedService(provider =>
                new ChannelStartupPublishHostedService(
                    provider.GetRequiredService<IZLinkFanoutPublisher>(),
                    options.ChannelName!,
                    options.PublishTopic!,
                    options.PublishValue ?? "startup"));
        }
    }

    private static void ConfigureSpotNode(IServiceCollection services, TestHostOptions options)
    {
        services.AddSingleton(new TestHostEventSink(options.EventFilePath));
        services.AddScoped<StartupStageSubscriptionHandler>();
        services.AddZLinkFramework(framework =>
        {
            framework.AddSpotMesh(
                options.DiscoveryChannelName
                    ?? throw new InvalidOperationException("SPOT node mode requires --discovery-channel."),
                spotMesh =>
                {
                    spotMesh.UseDiscovery(discovery =>
                    {
                        discovery.Add(options.DiscoveryEndpoint
                            ?? throw new InvalidOperationException("SPOT node mode requires --discovery-endpoint."));
                    });

                    spotMesh.AddNode(
                        options.SpotNodeName
                            ?? throw new InvalidOperationException("SPOT node mode requires --spot-node-name."),
                        spot =>
                        {
                            spot.Bind(options.SpotBindEndpoint
                                ?? throw new InvalidOperationException("SPOT node mode requires --spot-bind-endpoint."));

                            if (options.EnablePubSub)
                            {
                                spot.EnablePubSub();
                            }

                            if (!string.IsNullOrWhiteSpace(options.AttachSpotPublisherChannel))
                            {
                                spot.AttachSpotMeshPublisherClient(options.AttachSpotPublisherChannel);
                            }

                            if (!string.IsNullOrWhiteSpace(options.SpotFactoryName))
                            {
                                spot.AddSpotFactory<StartupStageSpot>(options.SpotFactoryName);
                            }
                        });
                });
        });

        if (!string.IsNullOrWhiteSpace(options.CreateSpotName))
        {
            services.AddHostedService(provider =>
                new StartupSpotCreationHostedService(
                    provider.GetRequiredService<IZLinkSpotManager>(),
                    options.CreateSpotName!));
        }

        if (!string.IsNullOrWhiteSpace(options.AttachSpotPublisherChannel)
            && !string.IsNullOrWhiteSpace(options.PublishTopic))
        {
            services.AddHostedService(provider =>
                new SpotStartupPublishHostedService(
                    provider.GetRequiredService<IZLinkSpotMeshPublisherClient>(),
                    options.AttachSpotPublisherChannel!,
                    options.PublishTopic!,
                    options.PublishValue ?? "startup"));
        }
    }

    private static void ConfigureStreamRawNode(IServiceCollection services, TestHostOptions options)
    {
        services.AddSingleton(new TestHostEventSink(options.EventFilePath));
        services.AddSingleton<TestHostRawStreamRecorder>();
        services.AddZLinkFramework(framework =>
        {
            framework.AddStreamNode("stream.raw", stream =>
            {
                stream.Bind(options.StreamEndpoint
                    ?? throw new InvalidOperationException("STREAM raw mode requires --stream-endpoint."));
                stream.AddHeaderSession<TestHostRawStreamSession>();
            });
        });
    }
}

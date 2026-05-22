using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.DependencyInjection.Extensions;
using Zlink.Framework.Runtime.Backend.Contracts;

namespace Zlink.Framework.AspNetCore;

internal static class ZLinkFrameworkServiceRegistrar
{
    public static IServiceCollection AddFrameworkRuntime(
        IServiceCollection services,
        ZLinkFrameworkRegistration registration)
    {
        return services
            .AddScannedHandlers(registration)
            .AddCoreRuntime(registration)
            .AddPublicClients(registration)
            .AddApplicationServices(registration);
    }

    private static IServiceCollection AddScannedHandlers(
        this IServiceCollection services,
        ZLinkFrameworkRegistration registration)
    {
        foreach (var assembly in registration.HandlerAssemblies)
        {
            services.AddZLinkHandlersFromAssembly(assembly);
        }

        foreach (var channel in registration.Channels.Values)
        {
            AddExplicitChannelHandlers(services, channel);
        }

        return services;
    }

    private static void AddExplicitChannelHandlers(
        IServiceCollection services,
        ZLinkChannelRegistration channel)
    {
        foreach (var handler in channel.SendHandlers)
        {
            AddExplicitChannelHandler(
                services,
                channel.ChannelName,
                handler,
                typeof(IZLinkSendHandler<>).MakeGenericType(handler.MessageType),
                ZLinkMessageKind.Command);
        }

        foreach (var handler in channel.RequestHandlers)
        {
            AddExplicitChannelHandler(
                services,
                channel.ChannelName,
                handler,
                typeof(IZLinkRequestHandler<,>).MakeGenericType(handler.MessageType, handler.ReplyType!),
                ZLinkMessageKind.Request);
        }

        foreach (var handler in channel.PublishHandlers)
        {
            AddExplicitChannelHandler(
                services,
                channel.ChannelName,
                handler,
                typeof(IZLinkPublishHandler<>).MakeGenericType(handler.MessageType),
                ZLinkMessageKind.Publish);
        }
    }

    private static void AddExplicitChannelHandler(
        IServiceCollection services,
        string channelName,
        ZLinkChannelHandlerRegistration handler,
        Type handlerInterface,
        ZLinkMessageKind kind)
    {
        services.TryAddTransient(handler.HandlerType);
        services.AddSingleton(ZLinkHandlerScanner.CreateExplicitInterfaceDescriptor(
            handler.HandlerType,
            handlerInterface,
            kind,
            channelName,
            handler.PacketName));
    }

    private static IServiceCollection AddCoreRuntime(
        this IServiceCollection services,
        ZLinkFrameworkRegistration registration)
    {
        services.AddSingleton(registration);
        services.TryAddSingleton<IZLinkBackendAdapterFactory, ZLinkDotNetBackendAdapterFactory>();
        services.TryAddSingleton(static provider =>
            new ZLinkHandlerRegistry(
                provider.GetServices<ZLinkHandlerEndpointDescriptor>()));
        services.TryAddSingleton<ZLinkHandlerDispatcher>();
        services.TryAddSingleton<ZLinkRuntimeEventDispatcher>();
        services.AddSingleton(static provider =>
            new ZLinkFrameworkRuntime(
                provider,
                provider.GetRequiredService<IZLinkBackendAdapterFactory>(),
                provider.GetRequiredService<ZLinkFrameworkRegistration>(),
                provider.GetRequiredService<ZLinkHandlerRegistry>(),
                provider.GetRequiredService<ZLinkHandlerDispatcher>(),
                provider.GetService<ZLinkRegistryRuntime>()));
        services.AddSingleton<IZLinkChannelConnectionManager, ZLinkChannelConnectionManager>();
        services.AddSingleton<IZLinkMessageMetadataPolicy, ZLinkMessageMetadataPolicy>();
        services.AddSingleton<Microsoft.Extensions.Hosting.IHostedService, ZLinkFrameworkHostedService>();

        return services;
    }

    private static IServiceCollection AddPublicClients(
        this IServiceCollection services,
        ZLinkFrameworkRegistration registration)
    {
        services.AddSingleton<ZLinkClient>();
        services.AddSingleton<IZLinkClientServerClient>(static provider => provider.GetRequiredService<ZLinkClient>());
        services.AddSingleton<IZLinkClient>(static provider => provider.GetRequiredService<ZLinkClient>());
        services.AddSingleton<ZLinkRouteClient>();
        services.AddSingleton<IZLinkRouteClient>(static provider => provider.GetRequiredService<ZLinkRouteClient>());
        services.AddSingleton<IZLinkMultipartRouteClient>(static provider => provider.GetRequiredService<ZLinkRouteClient>());
        services.AddSingleton<ZLinkSessionProxyService>();
        services.AddSingleton<IZLinkSessionProxyFactory>(
            provider => provider.GetRequiredService<ZLinkSessionProxyService>());
        services.AddSingleton<ZLinkEventPublisher>();
        services.AddSingleton<IZLinkFanoutPublisher>(static provider => provider.GetRequiredService<ZLinkEventPublisher>());
        services.AddSingleton<IZLinkEventPublisher>(static provider => provider.GetRequiredService<ZLinkEventPublisher>());

        if (HasSpotNode(registration))
        {
            services.AddSingleton<IZLinkSpotManager, ZLinkSpotManagerService>();
            services.AddSingleton<IZLinkSpotClient, ZLinkSpotClientService>();
            services.AddSingleton<IZLinkSpotConnectionManager, ZLinkSpotConnectionManagerService>();
        }

        if (HasRoutedSpotEgress(registration))
        {
            services.AddSingleton<IZLinkRoutedSpotClient, ZLinkRoutedSpotClientService>();
        }

        if (HasSpotPublisherClient(registration))
        {
            services.AddSingleton<ZLinkSpotPublisherClientService>();
            services.AddSingleton<IZLinkSpotMeshPublisherClient>(static provider => provider.GetRequiredService<ZLinkSpotPublisherClientService>());
            services.AddSingleton<IZLinkSpotPublisherClient>(static provider => provider.GetRequiredService<ZLinkSpotPublisherClientService>());
        }

        if (HasSpotNode(registration) && registration.ActorFactories.Count > 0)
        {
            services.AddSingleton<IZLinkActorManager, ZLinkActorManagerService>();
        }

        if (registration.ActorRemoteAddressResolverType is not null)
        {
            services.TryAddSingleton(registration.ActorRemoteAddressResolverType);
            services.AddSingleton(
                typeof(IZLinkActorRemoteAddressResolver),
                provider => provider.GetRequiredService(registration.ActorRemoteAddressResolverType));
        }
        else if (registration.RegistryActorRemoteAddresses is not null)
        {
            services.AddSingleton<ZLinkRegistryActorRemoteAddressResolver>();
            services.AddSingleton<IZLinkActorRemoteAddressResolver>(
                static provider => provider.GetRequiredService<ZLinkRegistryActorRemoteAddressResolver>());
        }

        if (registration.SpotRemoteAddressResolverType is not null)
        {
            services.TryAddSingleton(registration.SpotRemoteAddressResolverType);
            services.AddSingleton(
                typeof(IZLinkSpotRemoteAddressResolver),
                provider => provider.GetRequiredService(registration.SpotRemoteAddressResolverType));
        }
        else if (registration.RegistrySpotRemoteAddresses is not null)
        {
            services.AddSingleton<ZLinkRegistrySpotRemoteAddressResolver>();
            services.AddSingleton<IZLinkSpotRemoteAddressResolver>(
                static provider => provider.GetRequiredService<ZLinkRegistrySpotRemoteAddressResolver>());
        }

        return services;
    }

    private static IServiceCollection AddApplicationServices(
        this IServiceCollection services,
        ZLinkFrameworkRegistration registration)
    {
        foreach (var filterType in registration.Filters)
        {
            services.AddTransient(filterType);
        }

        foreach (var actorFactoryType in registration.ActorFactories.Values)
        {
            services.AddScoped(actorFactoryType);
        }

        foreach (var routed in registration.RouteChannels.Values)
        {
            foreach (var handler in routed.SendHandlers)
            {
                services.AddScoped(handler.HandlerType);
            }

            foreach (var handler in routed.RequestHandlers)
            {
                services.AddScoped(handler.HandlerType);
            }
        }

        return services;
    }

    private static bool HasSpotNode(ZLinkFrameworkRegistration registration)
    {
        return registration.SpotNodes.Count > 0;
    }

    private static bool HasSpotPublisherClient(ZLinkFrameworkRegistration registration)
    {
        return registration.SpotNodes.Values.Any(static spotNode =>
            spotNode.AttachedSpotPublisherClients.Count > 0);
    }

    private static bool HasRoutedSpotEgress(ZLinkFrameworkRegistration registration)
    {
        return registration.Channels.Values.Any(static channel => channel.SpotRouteEgress is not null)
            || registration.RouteChannels.Values.Any(static channel => channel.SpotRouteEgress is not null);
    }
}

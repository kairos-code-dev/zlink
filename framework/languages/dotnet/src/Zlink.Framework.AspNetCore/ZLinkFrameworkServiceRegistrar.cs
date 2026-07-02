using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.DependencyInjection.Extensions;
using Microsoft.Extensions.Hosting;

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
            .AddApplicationServices(registration)
            .AddLocationRuntime(registration);
    }

    private static IServiceCollection AddScannedHandlers(
        this IServiceCollection services,
        ZLinkFrameworkRegistration registration)
    {
        foreach (var assembly in registration.HandlerAssemblies) services.AddZLinkHandlersFromAssembly(assembly);

        foreach (var channel in registration.Channels.Values) AddExplicitChannelHandlers(services, channel);

        return services;
    }

    private static void AddExplicitChannelHandlers(
        IServiceCollection services,
        ZLinkChannelRegistration channel)
    {
        foreach (var handler in channel.SendHandlers)
            AddExplicitChannelHandler(
                services,
                channel.ChannelName,
                handler,
                typeof(IZLinkSendHandler<>).MakeGenericType(handler.MessageType),
                ZLinkMessageKind.Command);

        foreach (var handler in channel.RequestHandlers)
            AddExplicitChannelHandler(
                services,
                channel.ChannelName,
                handler,
                typeof(IZLinkRequestHandler<,>).MakeGenericType(handler.MessageType, handler.ReplyType!),
                ZLinkMessageKind.Request);

        foreach (var handler in channel.PublishHandlers)
            AddExplicitChannelHandler(
                services,
                channel.ChannelName,
                handler,
                typeof(IZLinkPublishHandler<>).MakeGenericType(handler.MessageType),
                ZLinkMessageKind.Publish);
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

        // Install the shared, runtime-mutable message-flow mode cell (seeded from the
        // configured mode) so SetMessageFlowMode can flip tracing on/off live and
        // every surface that reads EffectiveMessageFlow observes it.
        registration.DispatchOptions.Diagnostics.LiveMode ??=
            new ZLinkMessageFlowModeCell(registration.DispatchOptions.Diagnostics.MessageFlow);

        // Public runtime toggle (resolve IZLinkMessageFlowControl to flip tracing live).
        services.AddSingleton<IZLinkMessageFlowControl>(
            new ZLinkMessageFlowControl(registration.DispatchOptions.Diagnostics));

        if (registration.DispatchOptions.MessageFlowObserverType is { } flowObserverType)
            services.TryAddTransient(flowObserverType);

        services.TryAddSingleton(static provider =>
            new ZLinkHandlerRegistry(
                provider.GetServices<ZLinkHandlerEndpointDescriptor>()));
        services.TryAddSingleton<ZLinkHandlerDispatcher>();
        services.TryAddScoped(typeof(IZLinkSessionPacketDispatcher<>), typeof(ZLinkSessionPacketDispatcher<>));
        services.TryAddSingleton<ZLinkRuntimeEventDispatcher>();
        services.AddSingleton(static provider =>
            new ZLinkFrameworkRuntime(
                provider,
                provider.GetRequiredService<IZLinkBackendAdapterFactory>(),
                provider.GetRequiredService<ZLinkFrameworkRegistration>(),
                provider.GetRequiredService<ZLinkHandlerRegistry>(),
                provider.GetRequiredService<ZLinkHandlerDispatcher>(),
                provider.GetService<ZLinkRegistryRuntime>()));
        services.AddSingleton<IZLinkMessageMetadataPolicy, ZLinkMessageMetadataPolicy>();
        services.AddSingleton<IHostedService, ZLinkFrameworkHostedService>();

        return services;
    }

    private static IServiceCollection AddPublicClients(
        this IServiceCollection services,
        ZLinkFrameworkRegistration registration)
    {
        services.AddSingleton<ZLinkChannelClient>();
        services.AddSingleton<IZLinkChannelClient>(static provider =>
            provider.GetRequiredService<ZLinkChannelClient>());
        services.AddSingleton<IZLinkChannelRuntimeOptions>(static provider => new ZLinkChannelRuntimeOptions(
            provider.GetRequiredService<ZLinkFrameworkRuntime>()));
        services.AddSingleton<ZLinkRouteClient>();
        services.AddSingleton<IZLinkRouteClient>(static provider => provider.GetRequiredService<ZLinkRouteClient>());
        services.AddSingleton<IZLinkMultipartRouteClient>(static provider =>
            provider.GetRequiredService<ZLinkRouteClient>());
        services.AddSingleton<ZLinkBoundSessionService>();
        services.AddSingleton<IZLinkBoundSessionFactory>(provider =>
            provider.GetRequiredService<ZLinkBoundSessionService>());
        services.AddSingleton<ZLinkFanoutClient>();
        services.AddSingleton<IZLinkFanoutClient>(static provider => provider.GetRequiredService<ZLinkFanoutClient>());

        if (HasSpotNode(registration))
        {
            services.AddSingleton<IZLinkSpotManager, ZLinkSpotManagerService>();
            services.AddSingleton<IZLinkSpotOutbound, ZLinkSpotOutboundService>();
        }

        if (HasSpotPublisherClient(registration))
        {
            services.AddSingleton<ZLinkSpotPublisherClientService>();
            services.AddSingleton<IZLinkSpotPublisherClient>(static provider =>
                provider.GetRequiredService<ZLinkSpotPublisherClientService>());
        }

        if (HasActorCapableSpotNode(registration))
        {
            services.AddSingleton<ZLinkActorManagerService>();
            services.AddSingleton<IZLinkActorManager>(static provider =>
                provider.GetRequiredService<ZLinkActorManagerService>());
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
            services.AddSingleton<IZLinkSpotRemoteAddressResolver>(static provider =>
                provider.GetRequiredService<ZLinkRegistrySpotRemoteAddressResolver>());
        }

        return services;
    }

    private static IServiceCollection AddApplicationServices(
        this IServiceCollection services,
        ZLinkFrameworkRegistration registration)
    {
        foreach (var filterType in registration.Filters) services.AddTransient(filterType);

        foreach (var actorFactoryType in registration.SpotNodes.Values
                     .SelectMany(static spotNode => spotNode.ActorFactories.Values))
            services.TryAddScoped(actorFactoryType);

        foreach (var spotType in registration.SpotNodes.Values
                     .SelectMany(static spotNode => spotNode.SpotFactories))
            services.TryAddScoped(spotType);

        foreach (var entrySpotType in registration.SpotNodes.Values
                     .Select(static spotNode => spotNode.EntrySpotType)
                     .OfType<Type>())
            services.TryAddScoped(entrySpotType);

        foreach (var routed in registration.RouteChannels.Values)
        {
            foreach (var handler in routed.SendHandlers) services.TryAddScoped(handler.HandlerType);

            foreach (var handler in routed.RequestHandlers) services.TryAddScoped(handler.HandlerType);
        }

        ZLinkApplicationDependencyRegistrar.AddConstructorDiscoveredDependencies(services, registration);

        return services;
    }

    private static bool HasSpotNode(ZLinkFrameworkRegistration registration)
    {
        return registration.SpotNodes.Count > 0;
    }

    private static bool HasSpotPublisherClient(ZLinkFrameworkRegistration registration)
    {
        return registration.SpotNodes.Count > 0;
    }

    private static bool HasActorCapableSpotNode(ZLinkFrameworkRegistration registration)
    {
        return registration.SpotNodes.Values.Any(static spotNode => spotNode.ActorFactories.Count > 0);
    }
    private static IServiceCollection AddLocationRuntime(
        this IServiceCollection services,
        ZLinkFrameworkRegistration registration)
    {
        var locations = registration.Locations;
        if (!locations.Enabled) return services;

        services.AddSingleton(locations.Options);
        if (locations.UseInMemoryStores)
        {
            services.AddSingleton<ZLinkInMemoryLocationStore>();
            services.AddSingleton<IZLinkPeerLocationStore>(
                static provider => provider.GetRequiredService<ZLinkInMemoryLocationStore>());
            services.AddSingleton<IZLinkSpotLocationStore>(
                static provider => provider.GetRequiredService<ZLinkInMemoryLocationStore>());
            services.AddSingleton<IZLinkActorLocationStore>(
                static provider => provider.GetRequiredService<ZLinkInMemoryLocationStore>());
            services.AddSingleton<IZLinkRouteLocationStore>(
                static provider => provider.GetRequiredService<ZLinkInMemoryLocationStore>());
            services.AddSingleton<IZLinkOwnerLeaseStore>(
                static provider => provider.GetRequiredService<ZLinkInMemoryLocationStore>());
            services.AddSingleton<IZLinkLocationChangeStampStore>(
                static provider => provider.GetRequiredService<ZLinkInMemoryLocationStore>());
        }
        else
        {
            services.AddSingleton(typeof(IZLinkPeerLocationStore), locations.PeerStoreType!);
            services.AddSingleton(typeof(IZLinkSpotLocationStore), locations.SpotStoreType!);
            services.AddSingleton(typeof(IZLinkActorLocationStore), locations.ActorStoreType!);
            services.AddSingleton(typeof(IZLinkRouteLocationStore), locations.RouteStoreType!);
            services.AddSingleton(typeof(IZLinkOwnerLeaseStore), locations.OwnerLeaseStoreType!);
        }

        services.AddSingleton(static provider => new ZLinkOwnerLeaseTracker(
            provider.GetRequiredService<IZLinkOwnerLeaseStore>(),
            provider.GetRequiredService<ZLinkLocationOptions>()));
        services.AddSingleton(static provider => new ZLinkStoreLocationResolvers(
            provider.GetRequiredService<ZLinkLocationOptions>(),
            provider.GetRequiredService<IZLinkPeerLocationStore>(),
            provider.GetRequiredService<IZLinkSpotLocationStore>(),
            provider.GetRequiredService<IZLinkActorLocationStore>(),
            provider.GetRequiredService<IZLinkRouteLocationStore>(),
            provider.GetRequiredService<ZLinkOwnerLeaseTracker>()));
        services.AddSingleton<IZLinkPeerLocationResolver>(
            static provider => provider.GetRequiredService<ZLinkStoreLocationResolvers>());
        services.AddSingleton<IZLinkSpotLocationResolver>(
            static provider => provider.GetRequiredService<ZLinkStoreLocationResolvers>());
        services.AddSingleton<IZLinkActorLocationResolver>(
            static provider => provider.GetRequiredService<ZLinkStoreLocationResolvers>());
        services.AddSingleton<IZLinkRouteLocationResolver>(
            static provider => provider.GetRequiredService<ZLinkStoreLocationResolvers>());
        services.AddSingleton(static provider => new ZLinkLocationRuntime(
            provider.GetRequiredService<ZLinkLocationOptions>(),
            provider.GetRequiredService<IZLinkPeerLocationStore>(),
            provider.GetRequiredService<IZLinkSpotLocationStore>(),
            provider.GetRequiredService<IZLinkActorLocationStore>(),
            provider.GetRequiredService<IZLinkRouteLocationStore>(),
            provider.GetRequiredService<IZLinkOwnerLeaseStore>()));
        services.AddSingleton<IZLinkLocationRuntimeQuery>(
            static provider => new ZLinkLocationRuntimeQueryService(
                provider.GetRequiredService<ZLinkLocationOptions>(),
                provider.GetRequiredService<IZLinkPeerLocationStore>(),
                provider.GetRequiredService<IZLinkSpotLocationStore>(),
                provider.GetRequiredService<IZLinkActorLocationStore>(),
                provider.GetRequiredService<IZLinkRouteLocationStore>(),
                provider.GetRequiredService<ZLinkOwnerLeaseTracker>(),
                provider.GetRequiredService<ZLinkLocationRuntime>(),
                provider.GetRequiredService<ZLinkStoreLocationResolvers>()));
        services.AddSingleton(static provider => new ZLinkLocationAutoConnectHost(
            provider.GetRequiredService<ZLinkLocationRuntime>(),
            provider.GetRequiredService<IZLinkPeerLocationResolver>(),
            provider.GetRequiredService<ZLinkLocationOptions>(),
            provider.GetService<IZLinkLocationChangeStampStore>(),
            provider.GetService<IZLinkLocationWatchStore>()));
        services.AddSingleton<IHostedService, ZLinkLocationHostedService>();
        return services;
    }

}
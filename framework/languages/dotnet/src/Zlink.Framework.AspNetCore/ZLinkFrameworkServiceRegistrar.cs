using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.DependencyInjection.Extensions;
using System.Reflection;
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
        services.AddSingleton<Microsoft.Extensions.Hosting.IHostedService, ZLinkFrameworkHostedService>();

        return services;
    }

    private static IServiceCollection AddPublicClients(
        this IServiceCollection services,
        ZLinkFrameworkRegistration registration)
    {
        services.AddSingleton<ZLinkChannelClient>();
        services.AddSingleton<IZLinkChannelClient>(static provider => provider.GetRequiredService<ZLinkChannelClient>());
        services.AddSingleton<ZLinkRouteClient>();
        services.AddSingleton<IZLinkRouteClient>(static provider => provider.GetRequiredService<ZLinkRouteClient>());
        services.AddSingleton<IZLinkMultipartRouteClient>(static provider => provider.GetRequiredService<ZLinkRouteClient>());
        services.AddSingleton<ZLinkBoundSessionService>();
        services.AddSingleton<IZLinkBoundSessionFactory>(
            provider => provider.GetRequiredService<ZLinkBoundSessionService>());
        services.AddSingleton<ZLinkFanoutClient>();
        services.AddSingleton<IZLinkFanoutClient>(static provider => provider.GetRequiredService<ZLinkFanoutClient>());

        if (HasSpotNode(registration))
        {
            services.AddSingleton<IZLinkSpotManager, ZLinkSpotManagerService>();
            services.AddSingleton<IZLinkSpotClient, ZLinkSpotClientService>();
        }

        if (HasRoutedSpotEgress(registration))
        {
            services.AddSingleton<IZLinkRoutedSpotClient, ZLinkRoutedSpotClientService>();
        }

        if (HasSpotPublisherClient(registration))
        {
            services.AddSingleton<ZLinkSpotPublisherClientService>();
            services.AddSingleton<IZLinkSpotPublisherClient>(static provider => provider.GetRequiredService<ZLinkSpotPublisherClientService>());
        }

        if (HasSpotNode(registration) && registration.ActorFactories.Count > 0)
        {
            services.AddSingleton<IZLinkActorManager, ZLinkActorManagerService>();
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
            services.TryAddScoped(actorFactoryType);
        }

        foreach (var sessionType in registration.StreamNodes.Values
                     .Select(static stream => stream.HeaderSessionType)
                     .OfType<Type>())
        {
            services.TryAddScoped(sessionType);
        }

        foreach (var spotType in registration.SpotNodes.Values
                     .SelectMany(static spotNode => spotNode.SpotFactories))
        {
            services.TryAddScoped(spotType);
        }

        foreach (var entrySpotType in registration.SpotNodes.Values
                     .Select(static spotNode => spotNode.EntrySpotType)
                     .OfType<Type>())
        {
            services.TryAddScoped(entrySpotType);
        }

        foreach (var routed in registration.RouteChannels.Values)
        {
            foreach (var handler in routed.SendHandlers)
            {
                services.TryAddScoped(handler.HandlerType);
            }

            foreach (var handler in routed.RequestHandlers)
            {
                services.TryAddScoped(handler.HandlerType);
            }
        }

        AddEnumerableConstructorDependencies(services, EnumerateRegisteredApplicationTypes(registration));

        return services;
    }

    private static IEnumerable<Type> EnumerateRegisteredApplicationTypes(
        ZLinkFrameworkRegistration registration)
    {
        foreach (var filterType in registration.Filters)
        {
            yield return filterType;
        }

        foreach (var actorFactoryType in registration.ActorFactories.Values)
        {
            yield return actorFactoryType;
        }

        foreach (var stream in registration.StreamNodes.Values)
        {
            if (stream.HeaderSessionType is not null)
            {
                yield return stream.HeaderSessionType;
            }
        }

        foreach (var spotNode in registration.SpotNodes.Values)
        {
            if (spotNode.EntrySpotType is not null)
            {
                yield return spotNode.EntrySpotType;
            }

            foreach (var spotType in spotNode.SpotFactories)
            {
                yield return spotType;
            }
        }

        foreach (var channel in registration.Channels.Values)
        {
            foreach (var handler in channel.SendHandlers)
            {
                yield return handler.HandlerType;
            }

            foreach (var handler in channel.RequestHandlers)
            {
                yield return handler.HandlerType;
            }

            foreach (var handler in channel.PublishHandlers)
            {
                yield return handler.HandlerType;
            }
        }

        foreach (var routed in registration.RouteChannels.Values)
        {
            foreach (var handler in routed.SendHandlers)
            {
                yield return handler.HandlerType;
            }

            foreach (var handler in routed.RequestHandlers)
            {
                yield return handler.HandlerType;
            }
        }

        foreach (var assembly in registration.HandlerAssemblies)
        {
            foreach (var endpoint in ZLinkHandlerScanner.Scan(assembly))
            {
                yield return endpoint.DeclaringType;
            }

            foreach (var endpoint in ZLinkHandlerScanner.ScanRoute(assembly))
            {
                yield return endpoint.DeclaringType;
            }
        }
    }

    private static void AddEnumerableConstructorDependencies(
        IServiceCollection services,
        IEnumerable<Type> registeredTypes)
    {
        foreach (var registeredType in registeredTypes.Distinct())
        {
            foreach (var serviceType in FindEnumerableConstructorServices(registeredType))
            {
                AddAssemblyImplementations(services, registeredType.Assembly, serviceType);
                AddAssemblyImplementations(services, serviceType.Assembly, serviceType);
            }

            foreach (var serviceType in FindSessionPacketHandlerConstructorServices(registeredType))
            {
                AddAssemblyImplementations(services, registeredType.Assembly, serviceType);
                AddAssemblyImplementations(services, serviceType.Assembly, serviceType);
            }
        }
    }

    private static IEnumerable<Type> FindEnumerableConstructorServices(Type type)
    {
        foreach (var constructor in type.GetConstructors())
        {
            foreach (var parameter in constructor.GetParameters())
            {
                var parameterType = parameter.ParameterType;
                if (!parameterType.IsGenericType
                    || parameterType.GetGenericTypeDefinition() != typeof(IEnumerable<>))
                {
                    continue;
                }

                var serviceType = parameterType.GetGenericArguments()[0];
                if (serviceType.IsInterface)
                {
                    yield return serviceType;
                }
            }
        }
    }

    private static IEnumerable<Type> FindSessionPacketHandlerConstructorServices(Type type)
    {
        foreach (var constructor in type.GetConstructors())
        {
            foreach (var parameter in constructor.GetParameters())
            {
                var parameterType = parameter.ParameterType;
                if (!parameterType.IsGenericType
                    || parameterType.GetGenericTypeDefinition() != typeof(IZLinkSessionPacketDispatcher<>))
                {
                    continue;
                }

                var contextType = parameterType.GetGenericArguments()[0];
                yield return typeof(IZLinkSessionPacketHandler<>).MakeGenericType(contextType);
            }
        }
    }

    private static void AddAssemblyImplementations(
        IServiceCollection services,
        Assembly assembly,
        Type serviceType)
    {
        foreach (var implementationType in assembly.GetTypes()
                     .Where(type => type is { IsClass: true, IsAbstract: false }
                         && type != serviceType
                         && serviceType.IsAssignableFrom(type)))
        {
            services.TryAddEnumerable(ServiceDescriptor.Scoped(serviceType, implementationType));
            services.TryAddScoped(implementationType);
        }
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

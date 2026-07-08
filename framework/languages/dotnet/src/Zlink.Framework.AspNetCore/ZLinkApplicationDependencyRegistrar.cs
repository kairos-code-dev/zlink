using System.Reflection;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.DependencyInjection.Extensions;

namespace Zlink.Framework.AspNetCore;

internal static class ZLinkApplicationDependencyRegistrar
{
    public static void AddConstructorDiscoveredDependencies(
        IServiceCollection services,
        ZLinkFrameworkRegistration registration)
    {
        foreach (var registeredType in EnumerateRegisteredApplicationTypes(registration).Distinct())
        {
            foreach (var serviceType in FindEnumerableConstructorServices(registeredType))
            {
                AddAssemblyImplementations(services, registeredType.Assembly, serviceType);
                AddAssemblyImplementations(services, serviceType.Assembly, serviceType);
            }
        }
    }

    private static IEnumerable<Type> EnumerateRegisteredApplicationTypes(
        ZLinkFrameworkRegistration registration)
    {
        foreach (var filterType in registration.Filters) yield return filterType;

        foreach (var stream in registration.StreamNodes.Values)
            if (stream.HeaderSessionType is not null)
                yield return stream.HeaderSessionType;

        foreach (var spotNode in registration.SpotNodes.Values)
        {
            if (spotNode.EntrySpotType is not null) yield return spotNode.EntrySpotType;

            foreach (var spotType in spotNode.SpotFactories) yield return spotType;

            foreach (var actorFactoryType in spotNode.ActorFactories.Values) yield return actorFactoryType;
        }

        foreach (var channel in registration.Channels.Values)
        {
            foreach (var handler in channel.SendHandlers) yield return handler.HandlerType;

            foreach (var handler in channel.RequestHandlers) yield return handler.HandlerType;

            foreach (var handler in channel.PublishHandlers) yield return handler.HandlerType;
        }

        foreach (var routed in registration.RouteChannels.Values)
        {
            foreach (var handler in routed.SendHandlers) yield return handler.HandlerType;

            foreach (var handler in routed.RequestHandlers) yield return handler.HandlerType;
        }

        foreach (var assembly in registration.EnumerateHandlerScanAssemblies())
        {
            foreach (var endpoint in ZLinkHandlerScanner.Scan(assembly)) yield return endpoint.DeclaringType;

            foreach (var endpoint in ZLinkHandlerScanner.ScanRoute(assembly)) yield return endpoint.DeclaringType;
        }
    }

    private static IEnumerable<Type> FindEnumerableConstructorServices(Type type)
    {
        foreach (var constructor in type.GetConstructors())
        foreach (var parameter in constructor.GetParameters())
        {
            var parameterType = parameter.ParameterType;
            if (!parameterType.IsGenericType
                || parameterType.GetGenericTypeDefinition() != typeof(IEnumerable<>))
                continue;

            var serviceType = parameterType.GetGenericArguments()[0];
            if (serviceType.IsInterface) yield return serviceType;
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
}

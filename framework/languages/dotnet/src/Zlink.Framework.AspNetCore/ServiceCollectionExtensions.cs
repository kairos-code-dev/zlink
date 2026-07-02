using System.Reflection;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.DependencyInjection.Extensions;
using Microsoft.Extensions.Hosting;

namespace Zlink.Framework.AspNetCore;

public static class ServiceCollectionExtensions
{
    public static IServiceCollection AddZLinkFramework(
        this IServiceCollection services,
        Action<IZLinkFrameworkOptions> configure)
    {
        ArgumentNullException.ThrowIfNull(services);
        ArgumentNullException.ThrowIfNull(configure);

        var registration = new ZLinkFrameworkRegistration();
        var builder = new ZLinkFrameworkOptionsBuilder(registration);

        configure(builder);
        ZLinkFrameworkRegistrationValidator.Validate(registration);

        ZLinkFrameworkServiceRegistrar.AddFrameworkRuntime(services, registration);

        return services;
    }

    [Obsolete("Use AddZLinkFramework(options => options.AddHandlersFromAssemblyOf<TMarker>()) instead.")]
    public static IServiceCollection AddZLinkHandlersFromAssemblyContaining<TMarker>(
        this IServiceCollection services)
    {
        return services.AddZLinkHandlersFromAssembly(typeof(TMarker).Assembly);
    }

    public static IServiceCollection AddZLinkHandlersFromAssembly(
        this IServiceCollection services,
        Assembly assembly)
    {
        ArgumentNullException.ThrowIfNull(services);
        ArgumentNullException.ThrowIfNull(assembly);

        foreach (var endpoint in ZLinkHandlerScanner.Scan(assembly))
        {
            services.TryAddTransient(endpoint.DeclaringType);
            services.AddSingleton(endpoint);
        }

        foreach (var endpoint in ZLinkHandlerScanner.ScanRoute(assembly))
            services.TryAddTransient(endpoint.DeclaringType);

        return services;
    }

    public static IServiceCollection AddZLinkMonitoring(
        this IServiceCollection services,
        Action<IZLinkMonitoringOptions> configure)
    {
        ArgumentNullException.ThrowIfNull(services);
        ArgumentNullException.ThrowIfNull(configure);

        if (services.Any(static descriptor => descriptor.ServiceType == typeof(ZLinkMonitoringRegistration)))
            throw new ZLinkConfigurationException("Monitoring is already configured.");

        var registration = new ZLinkMonitoringRegistration();
        var builder = new ZLinkMonitoringOptionsModel(registration);

        configure(builder);

        ZLinkMonitoringServiceRegistrar.AddMonitoringRuntime(services, registration);

        return services;
    }
}
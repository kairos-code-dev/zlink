using Microsoft.Extensions.DependencyInjection;
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

        if (services.Any(static descriptor => descriptor.ServiceType == typeof(ZLinkFrameworkRegistration)))
            throw new ZLinkConfigurationException("ZLink framework is already configured.");

        var registration = new ZLinkFrameworkRegistration();
        var builder = new ZLinkFrameworkOptionsBuilder(registration);

        configure(builder);
        ZLinkFrameworkRegistrationValidator.Validate(registration);

        ZLinkFrameworkServiceRegistrar.AddFrameworkRuntime(services, registration);

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

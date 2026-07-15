using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.DependencyInjection.Extensions;
using Microsoft.Extensions.Hosting;

namespace Zlink.Framework.AspNetCore;

internal static class ZLinkMonitoringServiceRegistrar
{
    public static IServiceCollection AddMonitoringRuntime(
        IServiceCollection services,
        ZLinkMonitoringRegistration registration)
    {
        services.AddSingleton(registration);
        services.TryAddSingleton<IZLinkBackendAdapterFactory, ZLinkDotNetBackendAdapterFactory>();
        services.TryAddSingleton<ZLinkRuntimeEventDispatcher>();
        services.TryAddSingleton<IZLinkRuntimeEventPublisher>(static provider =>
            provider.GetRequiredService<ZLinkRuntimeEventDispatcher>());
        services.AddSingleton<IHostedService>(static provider =>
            new ZLinkMonitoringHostedService(
                provider.GetRequiredService<IZLinkBackendAdapterFactory>(),
                provider.GetRequiredService<ZLinkMonitoringRegistration>(),
                provider.GetRequiredService<IZLinkRuntimeEventPublisher>(),
                provider.GetService<ZLinkFrameworkRuntime>(),
                provider.GetService<IZLinkLocationRuntimeQuery>(),
                provider.GetService<ZLinkAutoConnectLifecycleCoordinator>()));

        return services;
    }
}

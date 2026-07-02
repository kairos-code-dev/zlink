using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.DependencyInjection.Extensions;

namespace Zlink.Framework.Locations.Redis;

public static class ZLinkRedisLocationServiceCollectionExtensions
{
    /// <summary>
    /// Registers the official Redis location store: the configured options,
    /// one <see cref="ZLinkRedisLocationStore"/> owning one shared Redis
    /// connection, and all six store contracts (peer, spot, actor, route,
    /// owner lease, change stamp) mapped onto that single instance. The
    /// contracts must share one physical store so that NewClaim can judge
    /// owner lease expiry atomically (draft 6.6).
    /// </summary>
    public static IServiceCollection AddZLinkRedisLocationStores(
        this IServiceCollection services,
        Action<ZLinkRedisLocationOptions> configure)
    {
        ArgumentNullException.ThrowIfNull(services);
        ArgumentNullException.ThrowIfNull(configure);

        var options = new ZLinkRedisLocationOptions();
        configure(options);
        options.Validate();

        services.TryAddSingleton(options);
        services.TryAddSingleton<ZLinkRedisLocationStore>();
        services.TryAddSingleton<IZLinkPeerLocationStore>(
            provider => provider.GetRequiredService<ZLinkRedisLocationStore>());
        services.TryAddSingleton<IZLinkSpotLocationStore>(
            provider => provider.GetRequiredService<ZLinkRedisLocationStore>());
        services.TryAddSingleton<IZLinkActorLocationStore>(
            provider => provider.GetRequiredService<ZLinkRedisLocationStore>());
        services.TryAddSingleton<IZLinkRouteLocationStore>(
            provider => provider.GetRequiredService<ZLinkRedisLocationStore>());
        services.TryAddSingleton<IZLinkOwnerLeaseStore>(
            provider => provider.GetRequiredService<ZLinkRedisLocationStore>());
        services.TryAddSingleton<IZLinkLocationChangeStampStore>(
            provider => provider.GetRequiredService<ZLinkRedisLocationStore>());
        return services;
    }
}

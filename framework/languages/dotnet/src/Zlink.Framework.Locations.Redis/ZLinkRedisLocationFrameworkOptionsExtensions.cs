using Zlink.Framework.Contracts.Configuration;

namespace Zlink.Framework.Locations.Redis;

public static class ZLinkRedisLocationFrameworkOptionsExtensions
{
    /// <summary>
    /// Registers the official Redis location store for every location store
    /// role in one call (draft 20.2): peer, spot, actor, route, owner lease,
    /// and change stamp all share one Redis-backed store instance.
    /// </summary>
    public static void AddRedisLocationStore(
        this IZLinkFrameworkOptions options,
        Action<ZLinkRedisLocationOptions> configure)
    {
        ArgumentNullException.ThrowIfNull(options);
        ArgumentNullException.ThrowIfNull(configure);

        options.AddLocationStores(services => services.AddZLinkRedisLocationStores(configure));
    }
}

using Zlink.Framework.Locations.Redis.Internal;

namespace Zlink.Framework.Locations.Redis;

/// <summary>
/// Maps Redis location row identities to the shared canonical representation.
/// Redis key prefixes and kind namespaces remain owned by
/// <see cref="ZLinkRedisLocationKeys"/>.
/// </summary>
internal static class ZLinkRedisLocationKeyCodec
{
    internal static string EncodePeerKey(ZLinkPeerLocationKey key) =>
        ZLinkCanonicalLocationKeyFormatter.EncodePeerKey(key);

    internal static string EncodeSpotKey(ZLinkSpotLocationKey key) =>
        ZLinkCanonicalLocationKeyFormatter.EncodeSpotKey(key);

    internal static string EncodeActorKey(ZLinkActorLocationKey key) =>
        ZLinkCanonicalLocationKeyFormatter.EncodeActorKey(key);

    internal static string EncodeRouteKey(ZLinkRouteLocationKey key) =>
        ZLinkCanonicalLocationKeyFormatter.EncodeRouteKey(key);
}

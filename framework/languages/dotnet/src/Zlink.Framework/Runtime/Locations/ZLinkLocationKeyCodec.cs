using Zlink.Framework.Internal.Locations;

namespace Zlink.Framework.Runtime.Locations;

/// <summary>
/// Maps framework-owned location store and bookkeeping keys to the shared
/// canonical identity representation.
/// </summary>
internal static class ZLinkLocationKeyCodec
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

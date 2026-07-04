using System.Globalization;
using System.Text;

namespace Zlink.Framework.Runtime.Locations;

/// <summary>
/// Serializes location keys into the canonical strings shared by every
/// language implementation. Each segment is length-prefixed so that plain
/// concatenation can never make two different keys collide.
/// Routing id segments use the lossless lower-case hex encoding of the
/// routing id bytes; human-readable representations may collide or differ
/// across languages.
///
/// This codec is intentionally separate from
/// Zlink.Framework.Locations.Redis.ZLinkRedisLocationKeyCodec. The P0
/// decision in the plan document's "빠지는 것" section keeps this type for
/// framework-internal bookkeeping and leaves backend-owned keys to each store
/// extension. The cross-language source of truth for Redis store keys is
/// framework/doc/framework/common/spec/location-store-redis.ko.md.
/// </summary>
internal static class ZLinkLocationKeyCodec
{
    internal static string EncodePeerKey(ZLinkPeerLocationKey key)
    {
        // Node rid identifies the peer when present; endpoint is the
        // fallback identity for roles that have no routing id.
        var identity = key.NodeRid is { } rid
            ? rid.ToHex()
            : key.Endpoint ?? string.Empty;
        return Encode(
            key.AutoConnectType.ToCanonicalString(),
            key.MeshName,
            key.Role.ToCanonicalString(),
            identity);
    }

    internal static string EncodeSpotKey(ZLinkSpotLocationKey key) =>
        Encode(key.MeshName, key.SpotRid.ToHex());

    internal static string EncodeActorKey(ZLinkActorLocationKey key) =>
        Encode(key.ActorId);

    internal static string EncodeRouteKey(ZLinkRouteLocationKey key) =>
        Encode(((int)key.RouteKind).ToString(CultureInfo.InvariantCulture), key.RouteKey);

    private static string Encode(params string[] segments)
    {
        var builder = new StringBuilder();
        foreach (var segment in segments)
        {
            builder
                .Append(segment.Length.ToString(CultureInfo.InvariantCulture))
                .Append(':')
                .Append(segment);
        }

        return builder.ToString();
    }
}

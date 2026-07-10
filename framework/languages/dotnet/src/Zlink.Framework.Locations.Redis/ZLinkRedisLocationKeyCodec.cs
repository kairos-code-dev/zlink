using System.Globalization;
using System.Text;

namespace Zlink.Framework.Locations.Redis;

/// <summary>
/// Serializes location keys into the canonical strings shared by every
/// language implementation. Each segment is length-prefixed so that plain
/// concatenation can never make two different keys collide.
/// Routing id segments use the lossless lower-case hex encoding of the
/// routing id bytes; human-readable representations may collide or differ
/// across languages.
///
/// This codec is intentionally separate from
/// Zlink.Framework.Runtime.Locations.ZLinkLocationKeyCodec. The P0 decision
/// in the plan document's "빠지는 것" section keeps the framework-internal
/// codec for runtime bookkeeping and leaves Redis keys owned by this backend
/// extension. The cross-language source of truth for Redis store keys is
/// framework/doc/framework/common/spec/location-store-redis.ko.md, so changes
/// must be checked against that document instead of treating either .NET codec
/// as a duplicate of the other.
/// </summary>
internal static class ZLinkRedisLocationKeyCodec
{
    internal static string EncodePeerKey(ZLinkPeerLocationKey key)
    {
        // Node rid identifies the peer when present; endpoint is the
        // fallback identity for roles that have no routing id.
        var identity = key.NodeRid is { } rid
            ? rid.ToHex()
            : key.Endpoint ?? string.Empty;
        return Encode(
            ToCanonicalString(key.AutoConnectType),
            key.MeshName,
            ToCanonicalString(key.Role),
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

    private static string ToCanonicalString(ZLinkLocationAutoConnectType type) => type switch
    {
        ZLinkLocationAutoConnectType.RouteMesh => "route-mesh",
        ZLinkLocationAutoConnectType.ClientServer => "client-server",
        ZLinkLocationAutoConnectType.DealerMesh => "dealer-mesh",
        ZLinkLocationAutoConnectType.Fanout => "fanout",
        ZLinkLocationAutoConnectType.SpotMesh => "spot-mesh",
        _ => throw new ArgumentOutOfRangeException(nameof(type), type, "Unknown auto-connect type.")
    };

    private static string ToCanonicalString(ZLinkLocationRole role) => role switch
    {
        ZLinkLocationRole.Spot => "spot",
        ZLinkLocationRole.Router => "router",
        ZLinkLocationRole.Dealer => "dealer",
        ZLinkLocationRole.Pub => "pub",
        ZLinkLocationRole.Sub => "sub",
        _ => throw new ArgumentOutOfRangeException(nameof(role), role, "Unknown location role.")
    };

}

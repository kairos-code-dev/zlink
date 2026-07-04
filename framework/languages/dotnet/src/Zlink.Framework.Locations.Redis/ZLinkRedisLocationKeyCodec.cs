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

    internal static ZLinkLocationKey DecodeKey(ZLinkLocationKind kind, string encoded) =>
        kind switch
        {
            ZLinkLocationKind.Peer => new ZLinkLocationKey.Peer(DecodePeerKey(encoded)),
            ZLinkLocationKind.Spot => new ZLinkLocationKey.Spot(DecodeSpotKey(encoded)),
            ZLinkLocationKind.Actor => new ZLinkLocationKey.Actor(DecodeActorKey(encoded)),
            ZLinkLocationKind.Route => new ZLinkLocationKey.Route(DecodeRouteKey(encoded)),
            _ => throw new ArgumentOutOfRangeException(nameof(kind), kind, "Unknown location kind.")
        };

    private static ZLinkPeerLocationKey DecodePeerKey(string encoded)
    {
        var segments = Decode(encoded, expectedCount: 4);
        var identity = segments[3];
        RoutingId? nodeRid = string.IsNullOrEmpty(identity) ? null : RoutingId.FromHex(identity);
        return new ZLinkPeerLocationKey(
            ParseAutoConnectType(segments[0]),
            segments[1],
            ParseRole(segments[2]),
            nodeRid,
            nodeRid is null ? identity : null);
    }

    private static ZLinkSpotLocationKey DecodeSpotKey(string encoded)
    {
        var segments = Decode(encoded, expectedCount: 2);
        return new ZLinkSpotLocationKey(segments[0], RoutingId.FromHex(segments[1]));
    }

    private static ZLinkActorLocationKey DecodeActorKey(string encoded)
    {
        var segments = Decode(encoded, expectedCount: 1);
        return new ZLinkActorLocationKey(segments[0]);
    }

    private static ZLinkRouteLocationKey DecodeRouteKey(string encoded)
    {
        var segments = Decode(encoded, expectedCount: 2);
        return new ZLinkRouteLocationKey(
            (ZLinkRouteKind)int.Parse(segments[0], CultureInfo.InvariantCulture),
            segments[1]);
    }

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

    private static string[] Decode(string encoded, int expectedCount)
    {
        var segments = new string[expectedCount];
        var offset = 0;
        for (var i = 0; i < expectedCount; i++)
        {
            var colon = encoded.IndexOf(':', offset);
            if (colon < 0)
            {
                throw new FormatException("Encoded location key segment length is missing.");
            }

            var length = int.Parse(encoded[offset..colon], CultureInfo.InvariantCulture);
            var start = colon + 1;
            var end = start + length;
            if (length < 0 || end > encoded.Length)
            {
                throw new FormatException("Encoded location key segment length is invalid.");
            }

            segments[i] = encoded[start..end];
            offset = end;
        }

        if (offset != encoded.Length)
        {
            throw new FormatException("Encoded location key has trailing data.");
        }

        return segments;
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

    private static ZLinkLocationAutoConnectType ParseAutoConnectType(string name) => name switch
    {
        "route-mesh" => ZLinkLocationAutoConnectType.RouteMesh,
        "client-server" => ZLinkLocationAutoConnectType.ClientServer,
        "dealer-mesh" => ZLinkLocationAutoConnectType.DealerMesh,
        "fanout" => ZLinkLocationAutoConnectType.Fanout,
        "spot-mesh" => ZLinkLocationAutoConnectType.SpotMesh,
        _ => throw new FormatException($"Unknown auto-connect type '{name}'.")
    };

    private static ZLinkLocationRole ParseRole(string name) => name switch
    {
        "spot" => ZLinkLocationRole.Spot,
        "router" => ZLinkLocationRole.Router,
        "dealer" => ZLinkLocationRole.Dealer,
        "pub" => ZLinkLocationRole.Pub,
        "sub" => ZLinkLocationRole.Sub,
        _ => throw new FormatException($"Unknown location role '{name}'.")
    };
}

using System.Globalization;
using System.Text;

namespace Zlink.Framework.Runtime.Locations;

/// <summary>
/// Serializes location keys into the canonical strings shared by every
/// language implementation. Each segment is length-prefixed so that plain
/// concatenation can never make two different keys collide.
/// </summary>
internal static class ZLinkLocationKeyCodec
{
    internal static string EncodePeerKey(ZLinkPeerLocationKey key)
    {
        // Node rid identifies the peer when present; endpoint is the
        // fallback identity for roles that have no routing id.
        var identity = key.NodeRid is { } rid
            ? rid.ToString()
            : key.Endpoint ?? string.Empty;
        return Encode(
            key.AutoConnectType.ToCanonicalString(),
            key.MeshName,
            key.Role.ToCanonicalString(),
            identity);
    }

    internal static string EncodeSpotKey(ZLinkSpotLocationKey key) =>
        Encode(key.MeshName, key.SpotRid.ToString());

    internal static string EncodeActorKey(ZLinkActorLocationKey key) =>
        Encode(NormalizeActorType(key.ActorType), key.ActorId);

    internal static string EncodeRouteKey(ZLinkRouteLocationKey key) =>
        Encode(((int)key.RouteKind).ToString(CultureInfo.InvariantCulture), key.RouteKey);

    /// <summary>An absent actor type and an empty actor type are the same
    /// key by contract.</summary>
    internal static string NormalizeActorType(string? actorType) => actorType ?? string.Empty;

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

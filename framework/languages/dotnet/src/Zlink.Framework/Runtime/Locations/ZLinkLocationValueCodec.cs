namespace Zlink.Framework.Runtime.Locations;

internal static class ZLinkLocationValueCodec
{
    private static readonly (ZLinkLocationAutoConnectType Type, string Name)[] AutoConnectTypes =
    [
        (ZLinkLocationAutoConnectType.RouteMesh, "route-mesh"),
        (ZLinkLocationAutoConnectType.ClientServer, "client-server"),
        (ZLinkLocationAutoConnectType.DealerMesh, "dealer-mesh"),
        (ZLinkLocationAutoConnectType.Fanout, "fanout"),
        (ZLinkLocationAutoConnectType.SpotMesh, "spot-mesh")
    ];

    private static readonly (ZLinkLocationRole Role, string Name)[] Roles =
    [
        (ZLinkLocationRole.Spot, "spot"),
        (ZLinkLocationRole.Router, "router"),
        (ZLinkLocationRole.Dealer, "dealer"),
        (ZLinkLocationRole.Pub, "pub"),
        (ZLinkLocationRole.Sub, "sub")
    ];

    internal static string ToCanonicalString(this ZLinkLocationAutoConnectType type)
    {
        foreach (var entry in AutoConnectTypes)
        {
            if (entry.Type == type)
            {
                return entry.Name;
            }
        }

        throw new ArgumentOutOfRangeException(nameof(type), type, "Unknown auto-connect type.");
    }

    internal static string ToCanonicalString(this ZLinkLocationRole role)
    {
        foreach (var entry in Roles)
        {
            if (entry.Role == role)
            {
                return entry.Name;
            }
        }

        throw new ArgumentOutOfRangeException(nameof(role), role, "Unknown location role.");
    }

    internal static bool TryParseAutoConnectType(string value, out ZLinkLocationAutoConnectType type)
    {
        foreach (var entry in AutoConnectTypes)
        {
            if (entry.Name == value)
            {
                type = entry.Type;
                return true;
            }
        }

        type = ZLinkLocationAutoConnectType.Invalid;
        return false;
    }

    internal static bool TryParseRole(string value, out ZLinkLocationRole role)
    {
        foreach (var entry in Roles)
        {
            if (entry.Name == value)
            {
                role = entry.Role;
                return true;
            }
        }

        role = ZLinkLocationRole.Invalid;
        return false;
    }

    internal static bool IsKnown(ZLinkLocationAutoConnectType type)
    {
        foreach (var entry in AutoConnectTypes)
        {
            if (entry.Type == type)
            {
                return true;
            }
        }

        return false;
    }

    internal static bool IsKnown(ZLinkLocationRole role)
    {
        foreach (var entry in Roles)
        {
            if (entry.Role == role)
            {
                return true;
            }
        }

        return false;
    }
}

namespace Zlink.Framework.Runtime.Configuration;

internal sealed class ZLinkHandlerExposureCatalog
{
    private readonly IReadOnlyDictionary<string, HashSet<ZLinkHandlerGroupCatalogEntry>> _groups;
    private readonly IReadOnlyList<ZLinkHandlerExposure> _scannedChannels;
    private readonly IReadOnlyList<ZLinkHandlerExposure> _scannedRoutes;

    private ZLinkHandlerExposureCatalog(
        IReadOnlyDictionary<string, HashSet<ZLinkHandlerGroupCatalogEntry>> groups,
        IReadOnlyList<ZLinkHandlerExposure> scannedChannels,
        IReadOnlyList<ZLinkHandlerExposure> scannedRoutes)
    {
        _groups = groups;
        _scannedChannels = scannedChannels;
        _scannedRoutes = scannedRoutes;
    }

    public static ZLinkHandlerExposureCatalog Build(
        IReadOnlyList<ZLinkHandlerEndpointDescriptor> channelEndpoints,
        IReadOnlyList<ZLinkRouteHandlerEndpointDescriptor> routeEndpoints)
    {
        var groups = new Dictionary<string, HashSet<ZLinkHandlerGroupCatalogEntry>>(StringComparer.Ordinal);
        var channels = channelEndpoints
            .Select(static endpoint => new ZLinkHandlerExposure(
                endpoint.Kind,
                endpoint.MessageName,
                endpoint.Groups))
            .ToArray();
        var routes = routeEndpoints
            .Select(static endpoint => new ZLinkHandlerExposure(
                endpoint.Kind,
                endpoint.MessageName,
                endpoint.Groups))
            .ToArray();

        AddGroups(groups, ZLinkHandlerEndpointSurface.Channel, channels);
        AddGroups(groups, ZLinkHandlerEndpointSurface.Route, routes);
        return new ZLinkHandlerExposureCatalog(groups, channels, routes);
    }

    public IReadOnlySet<ZLinkMessageKind> ValidateChannel(ZLinkChannelRegistration channel)
    {
        var allowedKinds = channel.AutoConnectType == ZLinkAutoConnectType.Fanout
            ? FanoutKinds
            : ClientServerKinds;
        ValidateChannelMappedGroups(channel, allowedKinds);

        ValidateExplicitChannelDuplicates(channel);
        var exposed = SelectMapped(_scannedChannels, channel.HandlerGroups);
        AddExplicitChannelHandlers(exposed, channel);
        ValidateConflicts(
            exposed,
            (kind, packetName) =>
                $"channel '{channel.ChannelName}' maps duplicate {kind} handler packet '{packetName}'.");
        return exposed.Select(static entry => entry.Kind).ToHashSet();
    }

    public void ValidateRoute(ZLinkRouteChannelRegistration route)
    {
        ValidateRouteMappedGroups(route);

        ValidateExplicitRouteDuplicates(route);
        var exposed = SelectMapped(_scannedRoutes, route.HandlerGroups);
        AddExplicitRouteHandlers(exposed, route);
        ValidateConflicts(
            exposed,
            (kind, packetName) =>
                $"Route channel '{route.RouterChannelId}' maps duplicate {kind} handler packet '{packetName}'.");
    }

    private void ValidateChannelMappedGroups(
        ZLinkChannelRegistration channel,
        IReadOnlySet<ZLinkMessageKind> allowedKinds)
    {
        foreach (var group in channel.HandlerGroups)
        {
            if (!_groups.TryGetValue(group, out var entries))
                throw new ZLinkConfigurationException(
                    $"channel '{channel.ChannelName}' maps unknown handler group '{group}'.");

            foreach (var entry in entries)
                if (entry.Surface != ZLinkHandlerEndpointSurface.Channel || !allowedKinds.Contains(entry.Kind))
                    throw new ZLinkConfigurationException(
                        $"channel '{channel.ChannelName}' maps handler group '{group}' with incompatible handler kind '{entry.Kind}'.");
        }
    }

    private void ValidateRouteMappedGroups(ZLinkRouteChannelRegistration route)
    {
        foreach (var group in route.HandlerGroups)
        {
            if (!_groups.TryGetValue(group, out var entries))
                throw new ZLinkConfigurationException(
                    $"Route channel '{route.RouterChannelId}' maps unknown handler group '{group}'.");

            foreach (var entry in entries)
                if (entry.Surface != ZLinkHandlerEndpointSurface.Route
                    || !ClientServerKinds.Contains(entry.Kind))
                    throw new ZLinkConfigurationException(
                        $"Route channel '{route.RouterChannelId}' maps handler group '{group}' with incompatible handler kind '{entry.Kind}'.");
        }
    }

    private static void ValidateExplicitChannelDuplicates(ZLinkChannelRegistration channel)
    {
        ValidateExplicitDuplicates(
            channel.SendHandlers.Select(static handler => Explicit(handler, ZLinkMessageKind.Command)),
            packetName => $"Duplicate send handler '{channel.ChannelName}:{packetName}'.");
        ValidateExplicitDuplicates(
            channel.RequestHandlers.Select(static handler => Explicit(handler, ZLinkMessageKind.Request)),
            packetName => $"Duplicate request handler '{channel.ChannelName}:{packetName}'.");
        ValidateExplicitDuplicates(
            channel.PublishHandlers.Select(static handler => Explicit(handler, ZLinkMessageKind.Publish)),
            packetName => $"Duplicate publish handler '{channel.ChannelName}:{packetName}'.");
    }

    private static void ValidateExplicitRouteDuplicates(ZLinkRouteChannelRegistration route)
    {
        ValidateExplicitDuplicates(
            route.SendHandlers.Select(static handler => Explicit(handler, ZLinkMessageKind.Command)),
            packetName => $"Duplicate routed send handler '{route.RouterChannelId}:{packetName}'.");
        ValidateExplicitDuplicates(
            route.RequestHandlers.Select(static handler => Explicit(handler, ZLinkMessageKind.Request)),
            packetName => $"Duplicate routed request handler '{route.RouterChannelId}:{packetName}'.");
    }

    private static void ValidateExplicitDuplicates(
        IEnumerable<ZLinkHandlerExposure> handlers,
        Func<string, string> duplicateMessage)
    {
        var packets = new HashSet<string>(StringComparer.Ordinal);
        foreach (var handler in handlers)
            if (!packets.Add(handler.PacketName))
                throw new ZLinkConfigurationException(duplicateMessage(handler.PacketName));
    }

    private static List<ZLinkHandlerExposure> SelectMapped(
        IReadOnlyList<ZLinkHandlerExposure> scanned,
        IReadOnlySet<string> mappedGroups) => scanned
        .Where(endpoint => endpoint.Groups.Count > 0 && endpoint.Groups.Any(mappedGroups.Contains))
        .ToList();

    private static void AddExplicitChannelHandlers(
        ICollection<ZLinkHandlerExposure> exposed,
        ZLinkChannelRegistration channel)
    {
        foreach (var handler in channel.SendHandlers)
            exposed.Add(Explicit(handler, ZLinkMessageKind.Command));
        foreach (var handler in channel.RequestHandlers)
            exposed.Add(Explicit(handler, ZLinkMessageKind.Request));
        foreach (var handler in channel.PublishHandlers)
            exposed.Add(Explicit(handler, ZLinkMessageKind.Publish));
    }

    private static void AddExplicitRouteHandlers(
        ICollection<ZLinkHandlerExposure> exposed,
        ZLinkRouteChannelRegistration route)
    {
        foreach (var handler in route.SendHandlers)
            exposed.Add(Explicit(handler, ZLinkMessageKind.Command));
        foreach (var handler in route.RequestHandlers)
            exposed.Add(Explicit(handler, ZLinkMessageKind.Request));
    }

    private static ZLinkHandlerExposure Explicit(
        ZLinkChannelHandlerRegistration handler,
        ZLinkMessageKind kind) => new(
        kind,
        ResolvePacketName(handler.MessageType, handler.PacketName),
        EmptyGroups);

    private static ZLinkHandlerExposure Explicit(
        ZLinkRouteHandlerRegistration handler,
        ZLinkMessageKind kind) => new(
        kind,
        ResolvePacketName(handler.MessageType, handler.PacketName),
        EmptyGroups);

    private static string ResolvePacketName(Type messageType, string? packetName) =>
        packetName ?? ZLinkMessageNameResolver.ResolveFromType(messageType);

    private static void ValidateConflicts(
        IEnumerable<ZLinkHandlerExposure> exposed,
        Func<ZLinkMessageKind, string, string> conflictMessage)
    {
        var handlers = new HashSet<(ZLinkMessageKind Kind, string PacketName)>();
        foreach (var handler in exposed)
        {
            var key = (handler.Kind, handler.PacketName);
            if (!handlers.Add(key))
                throw new ZLinkConfigurationException(conflictMessage(handler.Kind, handler.PacketName));
        }
    }

    private static void AddGroups(
        IDictionary<string, HashSet<ZLinkHandlerGroupCatalogEntry>> groups,
        ZLinkHandlerEndpointSurface surface,
        IEnumerable<ZLinkHandlerExposure> handlers)
    {
        foreach (var handler in handlers)
        foreach (var group in handler.Groups)
        {
            if (!groups.TryGetValue(group, out var entries))
            {
                entries = [];
                groups.Add(group, entries);
            }
            entries.Add(new ZLinkHandlerGroupCatalogEntry(surface, handler.Kind));
        }
    }

    private static readonly IReadOnlySet<string> EmptyGroups = new HashSet<string>(StringComparer.Ordinal);
    private static readonly IReadOnlySet<ZLinkMessageKind> ClientServerKinds =
        new HashSet<ZLinkMessageKind> { ZLinkMessageKind.Command, ZLinkMessageKind.Request };
    private static readonly IReadOnlySet<ZLinkMessageKind> FanoutKinds =
        new HashSet<ZLinkMessageKind> { ZLinkMessageKind.Publish };

    private sealed record ZLinkHandlerExposure(
        ZLinkMessageKind Kind,
        string PacketName,
        IReadOnlySet<string> Groups);
}

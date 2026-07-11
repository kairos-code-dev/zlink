namespace Zlink.Framework.Runtime.Configuration;

internal static partial class ZLinkFrameworkRegistrationValidator
{
    private static void ValidateRouteChannel(
        ZLinkRouteChannelRegistration routed,
        bool autoConnectConfigured,
        IReadOnlyDictionary<string, HashSet<ZLinkHandlerGroupCatalogEntry>> handlerGroups,
        IReadOnlyList<ZLinkRouteHandlerEndpointDescriptor> scannedEndpoints)
    {
        var serverEnabled = !string.IsNullOrWhiteSpace(routed.BindEndpoint);
        if (!serverEnabled && !routed.ClientEnabled)
            throw new ZLinkConfigurationException(
                $"Route channel '{routed.RouterChannelId}' must enable server or client capability.");

        if (routed.ClientEnabled)
        {
            ZLinkPeerAcquisitionPolicy.RequirePeerSource(
                $"Route channel '{routed.RouterChannelId}'",
                autoConnectConfigured,
                routed.ManualConnections);
            routed.AcquisitionMode = ZLinkPeerAcquisitionPolicy.Resolve(
                autoConnectConfigured,
                routed.ManualConnections);
            routed.ManualConnections.Freeze(routed.AcquisitionMode);
        }

        ValidateRouteMappedGroups(routed, handlerGroups);
        ValidateUniqueRouteHandlers(
            routed.RouterChannelId,
            ZLinkMessageKind.Command,
            "send",
            routed.SendHandlers);
        ValidateUniqueRouteHandlers(
            routed.RouterChannelId,
            ZLinkMessageKind.Request,
            "request",
            routed.RequestHandlers);
        ValidateRouteHandlerConflicts(routed, scannedEndpoints);
    }

    private static void ValidateRouteMappedGroups(
        ZLinkRouteChannelRegistration routed,
        IReadOnlyDictionary<string, HashSet<ZLinkHandlerGroupCatalogEntry>> handlerGroups)
    {
        foreach (var group in routed.HandlerGroups)
        {
            if (!handlerGroups.TryGetValue(group, out var entries))
                throw new ZLinkConfigurationException(
                    $"Route channel '{routed.RouterChannelId}' maps unknown handler group '{group}'.");

            foreach (var entry in entries)
                if (entry.Surface != ZLinkHandlerEndpointSurface.Route
                    || entry.Kind is not (ZLinkMessageKind.Command or ZLinkMessageKind.Request))
                    throw new ZLinkConfigurationException(
                        $"Route channel '{routed.RouterChannelId}' maps handler group '{group}' with incompatible handler kind '{entry.Kind}'.");
        }
    }

    private static void ValidateUniqueRouteHandlers(
        string routerChannelId,
        ZLinkMessageKind kind,
        string label,
        IReadOnlyList<ZLinkRouteHandlerRegistration> handlers)
    {
        var keys = new HashSet<(ZLinkMessageKind Kind, string PacketName)>();
        foreach (var handler in handlers)
        {
            var packetName = handler.PacketName
                             ?? ZLinkMessageNameResolver.ResolveFromType(handler.MessageType);
            if (!keys.Add((kind, packetName)))
                throw new ZLinkConfigurationException(
                    $"Duplicate routed {label} handler '{routerChannelId}:{packetName}'.");
        }
    }

    private static void ValidateRouteHandlerConflicts(
        ZLinkRouteChannelRegistration routed,
        IReadOnlyList<ZLinkRouteHandlerEndpointDescriptor> scannedEndpoints)
    {
        var exposed = new Dictionary<(ZLinkMessageKind Kind, string PacketName), Type>();

        foreach (var endpoint in scannedEndpoints)
        {
            if (endpoint.Groups.Count == 0
                || !endpoint.Groups.Any(routed.HandlerGroups.Contains))
                continue;

            AddExposedRouteHandler(
                routed.RouterChannelId,
                exposed,
                endpoint.Kind,
                endpoint.MessageName,
                endpoint.DeclaringType);
        }

        foreach (var handler in routed.SendHandlers)
            AddExposedRouteHandler(
                routed.RouterChannelId,
                exposed,
                ZLinkMessageKind.Command,
                handler.PacketName ?? ZLinkMessageNameResolver.ResolveFromType(handler.MessageType),
                handler.HandlerType);

        foreach (var handler in routed.RequestHandlers)
            AddExposedRouteHandler(
                routed.RouterChannelId,
                exposed,
                ZLinkMessageKind.Request,
                handler.PacketName ?? ZLinkMessageNameResolver.ResolveFromType(handler.MessageType),
                handler.HandlerType);
    }

    private static void AddExposedRouteHandler(
        string routerChannelId,
        IDictionary<(ZLinkMessageKind Kind, string PacketName), Type> exposed,
        ZLinkMessageKind kind,
        string packetName,
        Type handlerType)
    {
        var key = (kind, packetName);
        if (exposed.TryGetValue(key, out var existing)
            && existing != handlerType)
            throw new ZLinkConfigurationException(
                $"Route channel '{routerChannelId}' maps duplicate {kind} handler packet '{packetName}'.");

        exposed[key] = handlerType;
    }
}

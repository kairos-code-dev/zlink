namespace Zlink.Framework.Runtime.Configuration;

internal static partial class ZLinkFrameworkRegistrationValidator
{
    private static void ValidateChannel(
        ZLinkChannelRegistration channel,
        bool autoConnectConfigured,
        IReadOnlyDictionary<string, HashSet<ZLinkHandlerGroupCatalogEntry>> handlerGroups,
        IReadOnlyList<ZLinkHandlerEndpointDescriptor> scannedEndpoints)
    {
        ValidateChannelShape(channel);

        if (channel.Server is not null && string.IsNullOrWhiteSpace(channel.Server.BindEndpoint))
            throw new ZLinkConfigurationException(
                $"channel '{channel.ChannelName}' server must define a bind endpoint.");

        if (channel.Client is not null)
        {
            if (!string.IsNullOrWhiteSpace(channel.Client.BindEndpoint))
                throw new ZLinkConfigurationException(
                    $"channel '{channel.ChannelName}' client cannot define a bind endpoint.");

            ZLinkPeerAcquisitionPolicy.RequirePeerSource(
                $"channel '{channel.ChannelName}' client",
                autoConnectConfigured,
                channel.Client.ManualConnections);
        }

        if (channel.Publisher is not null && string.IsNullOrWhiteSpace(channel.Publisher.BindEndpoint))
            throw new ZLinkConfigurationException(
                $"channel '{channel.ChannelName}' publisher must define a bind endpoint.");

        if (channel.Subscriber is not null)
            ZLinkPeerAcquisitionPolicy.RequirePeerSource(
                $"channel '{channel.ChannelName}' subscriber",
                autoConnectConfigured,
                channel.Subscriber.ManualConnections);

        ValidateChannelHandlerExposure(channel, handlerGroups, scannedEndpoints);
    }

    private static void ValidateChannelHandlerExposure(
        ZLinkChannelRegistration channel,
        IReadOnlyDictionary<string, HashSet<ZLinkHandlerGroupCatalogEntry>> handlerGroups,
        IReadOnlyList<ZLinkHandlerEndpointDescriptor> scannedEndpoints)
    {
        switch (channel.AutoConnectType)
        {
            case ZLinkAutoConnectType.ClientServer:
                ValidateClientServerHandlerExposure(channel, handlerGroups);
                break;
            case ZLinkAutoConnectType.Fanout:
                ValidateFanoutHandlerExposure(channel, handlerGroups);
                break;
        }

        ValidateUniqueChannelHandlers(
            channel.ChannelName,
            ZLinkMessageKind.Command,
            "send",
            channel.SendHandlers);
        ValidateUniqueChannelHandlers(
            channel.ChannelName,
            ZLinkMessageKind.Request,
            "request",
            channel.RequestHandlers);
        ValidateUniqueChannelHandlers(
            channel.ChannelName,
            ZLinkMessageKind.Publish,
            "publish",
            channel.PublishHandlers);
        ValidateChannelHandlerConflicts(channel, scannedEndpoints);
    }

    private static void ValidateClientServerHandlerExposure(
        ZLinkChannelRegistration channel,
        IReadOnlyDictionary<string, HashSet<ZLinkHandlerGroupCatalogEntry>> handlerGroups)
    {
        ValidateMappedGroups(
            channel,
            handlerGroups,
            new HashSet<ZLinkMessageKind>
            {
                ZLinkMessageKind.Command,
                ZLinkMessageKind.Request
            });

        var hasHandlerExposure = channel.SendHandlers.Count > 0
                                 || channel.RequestHandlers.Count > 0
                                 || ChannelGroupsExposeAny(
                                     channel,
                                     handlerGroups,
                                     ZLinkMessageKind.Command,
                                     ZLinkMessageKind.Request);

        if (hasHandlerExposure && channel.Server is null)
            throw new ZLinkConfigurationException(
                $"client/server channel '{channel.ChannelName}' exposes handlers but does not enable server capability.");

        if (channel.Server is not null
            && !hasHandlerExposure)
            throw new ZLinkConfigurationException(
                $"client/server channel '{channel.ChannelName}' server must map a handler group, register a typed handler, or be accepted by a SPOT route channel.");

        if (channel.PublishHandlers.Count > 0)
            throw new ZLinkConfigurationException(
                $"client/server channel '{channel.ChannelName}' cannot register publish handlers.");
    }

    private static void ValidateFanoutHandlerExposure(
        ZLinkChannelRegistration channel,
        IReadOnlyDictionary<string, HashSet<ZLinkHandlerGroupCatalogEntry>> handlerGroups)
    {
        ValidateMappedGroups(
            channel,
            handlerGroups,
            new HashSet<ZLinkMessageKind> { ZLinkMessageKind.Publish });

        var hasHandlerExposure = channel.PublishHandlers.Count > 0
                                 || ChannelGroupsExposeAny(channel, handlerGroups, ZLinkMessageKind.Publish);

        if (hasHandlerExposure && channel.Subscriber is null)
            throw new ZLinkConfigurationException(
                $"fanout channel '{channel.ChannelName}' exposes publish handlers but does not enable subscriber capability.");

        if (channel.Subscriber is not null && !hasHandlerExposure)
            throw new ZLinkConfigurationException(
                $"fanout channel '{channel.ChannelName}' subscriber must map a publish handler group or register a typed publish handler.");

        if (channel.SendHandlers.Count > 0 || channel.RequestHandlers.Count > 0)
            throw new ZLinkConfigurationException(
                $"fanout channel '{channel.ChannelName}' cannot register send or request handlers.");
    }

    private static void ValidateMappedGroups(
        ZLinkChannelRegistration channel,
        IReadOnlyDictionary<string, HashSet<ZLinkHandlerGroupCatalogEntry>> handlerGroups,
        IReadOnlySet<ZLinkMessageKind> allowedKinds)
    {
        foreach (var group in channel.HandlerGroups)
        {
            if (!handlerGroups.TryGetValue(group, out var entries))
                throw new ZLinkConfigurationException(
                    $"channel '{channel.ChannelName}' maps unknown handler group '{group}'.");

            foreach (var entry in entries)
                if (entry.Surface != ZLinkHandlerEndpointSurface.Channel
                    || !allowedKinds.Contains(entry.Kind))
                    throw new ZLinkConfigurationException(
                        $"channel '{channel.ChannelName}' maps handler group '{group}' with incompatible handler kind '{entry.Kind}'.");
        }
    }

    private static bool ChannelGroupsExposeAny(
        ZLinkChannelRegistration channel,
        IReadOnlyDictionary<string, HashSet<ZLinkHandlerGroupCatalogEntry>> handlerGroups,
        params ZLinkMessageKind[] kinds)
    {
        return channel.HandlerGroups.Any(group =>
            handlerGroups.TryGetValue(group, out var groupEntries)
            && groupEntries.Any(entry =>
                entry.Surface == ZLinkHandlerEndpointSurface.Channel
                && kinds.Contains(entry.Kind)));
    }

    private static void ValidateUniqueChannelHandlers(
        string channelName,
        ZLinkMessageKind kind,
        string label,
        IReadOnlyList<ZLinkChannelHandlerRegistration> handlers)
    {
        var keys = new HashSet<(ZLinkMessageKind Kind, string PacketName)>();
        foreach (var handler in handlers)
        {
            var packetName = handler.PacketName
                             ?? ZLinkMessageNameResolver.ResolveFromType(handler.MessageType);
            if (!keys.Add((kind, packetName)))
                throw new ZLinkConfigurationException(
                    $"Duplicate {label} handler '{channelName}:{packetName}'.");
        }
    }

    private static void ValidateChannelHandlerConflicts(
        ZLinkChannelRegistration channel,
        IReadOnlyList<ZLinkHandlerEndpointDescriptor> scannedEndpoints)
    {
        var exposed = new Dictionary<(ZLinkMessageKind Kind, string PacketName), Type>();

        foreach (var endpoint in scannedEndpoints)
        {
            if (endpoint.Groups.Count == 0
                || !endpoint.Groups.Any(channel.HandlerGroups.Contains))
                continue;

            AddExposedChannelHandler(
                channel.ChannelName,
                exposed,
                endpoint.Kind,
                endpoint.MessageName,
                endpoint.DeclaringType);
        }

        foreach (var handler in channel.SendHandlers)
            AddExposedChannelHandler(
                channel.ChannelName,
                exposed,
                ZLinkMessageKind.Command,
                handler.PacketName ?? ZLinkMessageNameResolver.ResolveFromType(handler.MessageType),
                handler.HandlerType);

        foreach (var handler in channel.RequestHandlers)
            AddExposedChannelHandler(
                channel.ChannelName,
                exposed,
                ZLinkMessageKind.Request,
                handler.PacketName ?? ZLinkMessageNameResolver.ResolveFromType(handler.MessageType),
                handler.HandlerType);

        foreach (var handler in channel.PublishHandlers)
            AddExposedChannelHandler(
                channel.ChannelName,
                exposed,
                ZLinkMessageKind.Publish,
                handler.PacketName ?? ZLinkMessageNameResolver.ResolveFromType(handler.MessageType),
                handler.HandlerType);
    }

    private static void AddExposedChannelHandler(
        string channelName,
        IDictionary<(ZLinkMessageKind Kind, string PacketName), Type> exposed,
        ZLinkMessageKind kind,
        string packetName,
        Type handlerType)
    {
        var key = (kind, packetName);
        if (exposed.TryGetValue(key, out var existing)
            && existing != handlerType)
            throw new ZLinkConfigurationException(
                $"channel '{channelName}' maps duplicate {kind} handler packet '{packetName}'.");

        exposed[key] = handlerType;
    }

    private static void ValidateChannelShape(ZLinkChannelRegistration channel)
    {
        switch (channel.AutoConnectType)
        {
            case ZLinkAutoConnectType.ClientServer:
                RequireClientServerShape(channel);
                break;
            case ZLinkAutoConnectType.Fanout:
                RequireFanoutShape(channel);
                break;
            case ZLinkAutoConnectType.Invalid:
                throw new ZLinkConfigurationException(
                    $"channel '{channel.ChannelName}' must declare a concrete auto connect type.");
            default:
                throw new ZLinkConfigurationException(
                    $"channel '{channel.ChannelName}' has unsupported auto connect type '{channel.AutoConnectType}'.");
        }
    }

    private static void RequireClientServerShape(ZLinkChannelRegistration channel)
    {
        if (channel.Server is null && channel.Client is null)
            throw new ZLinkConfigurationException(
                $"client/server channel '{channel.ChannelName}' must enable server or client capabilities.");

        if (channel.Publisher is not null || channel.Subscriber is not null)
            throw new ZLinkConfigurationException(
                $"client/server channel '{channel.ChannelName}' cannot enable publisher or subscriber capabilities.");
    }

    private static void RequireFanoutShape(ZLinkChannelRegistration channel)
    {
        if (channel.Publisher is null && channel.Subscriber is null)
            throw new ZLinkConfigurationException(
                $"fanout channel '{channel.ChannelName}' must enable publisher or subscriber capabilities.");

        if (channel.Server is not null || channel.Client is not null)
            throw new ZLinkConfigurationException(
                $"fanout channel '{channel.ChannelName}' cannot enable server or client capabilities.");
    }
}

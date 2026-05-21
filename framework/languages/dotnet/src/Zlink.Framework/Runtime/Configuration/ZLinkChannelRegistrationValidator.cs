namespace Zlink.Framework.Runtime.Configuration;

internal static partial class ZLinkFrameworkRegistrationValidator
{
    private static void ValidateChannel(
        ZLinkChannelRegistration channel,
        bool discoveryConfigured,
        bool acceptedBySpotRouteChannel,
        IReadOnlyDictionary<string, HashSet<ZLinkMessageKind>> handlerGroups)
    {
        ValidateChannelShape(channel);

        if (channel.Server is not null && string.IsNullOrWhiteSpace(channel.Server.BindEndpoint))
        {
            throw new ZLinkConfigurationException(
                $"channel '{channel.ChannelName}' server must define a bind endpoint.");
        }

        if (channel.Client is not null)
        {
            if (channel.AutoConnectType != ZLinkAutoConnectType.DealerMesh
                && !string.IsNullOrWhiteSpace(channel.Client.BindEndpoint))
            {
                throw new ZLinkConfigurationException(
                    $"channel '{channel.ChannelName}' client bind endpoint is only valid for dealer mesh channels.");
            }

            ZLinkPeerAcquisitionPolicy.RequirePeerSource(
                $"channel '{channel.ChannelName}' client",
                discoveryConfigured,
                channel.Client.ManualConnections);
        }

        if (channel.SpotRouteEgress is not null)
        {
            if (channel.AutoConnectType != ZLinkAutoConnectType.ClientServer)
            {
                throw new ZLinkConfigurationException(
                    $"channel '{channel.ChannelName}' cannot enable routed SPOT egress.");
            }

            if (channel.Client is null)
            {
                throw new ZLinkConfigurationException(
                    $"client/server channel '{channel.ChannelName}' routed SPOT egress requires client capability.");
            }
        }

        if (channel.Publisher is not null && string.IsNullOrWhiteSpace(channel.Publisher.BindEndpoint))
        {
            throw new ZLinkConfigurationException(
                $"channel '{channel.ChannelName}' publisher must define a bind endpoint.");
        }

        if (channel.Subscriber is not null)
        {
            ZLinkPeerAcquisitionPolicy.RequirePeerSource(
                $"channel '{channel.ChannelName}' subscriber",
                discoveryConfigured,
                channel.Subscriber.ManualConnections);
        }

        ValidateChannelHandlerExposure(channel, acceptedBySpotRouteChannel, handlerGroups);
    }

    private static void ValidateChannelHandlerExposure(
        ZLinkChannelRegistration channel,
        bool acceptedBySpotRouteChannel,
        IReadOnlyDictionary<string, HashSet<ZLinkMessageKind>> handlerGroups)
    {
        switch (channel.AutoConnectType)
        {
            case ZLinkAutoConnectType.ClientServer:
                ValidateClientServerHandlerExposure(channel, acceptedBySpotRouteChannel, handlerGroups);
                break;
            case ZLinkAutoConnectType.Fanout:
                ValidateFanoutHandlerExposure(channel, handlerGroups);
                break;
            case ZLinkAutoConnectType.DealerMesh:
                if (channel.HandlerGroups.Count > 0
                    || channel.SendHandlers.Count > 0
                    || channel.RequestHandlers.Count > 0
                    || channel.PublishHandlers.Count > 0)
                {
                    throw new ZLinkConfigurationException(
                        $"dealer mesh channel '{channel.ChannelName}' cannot expose application handlers.");
                }
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
    }

    private static void ValidateClientServerHandlerExposure(
        ZLinkChannelRegistration channel,
        bool acceptedBySpotRouteChannel,
        IReadOnlyDictionary<string, HashSet<ZLinkMessageKind>> handlerGroups)
    {
        ValidateMappedGroups(
            channel,
            handlerGroups,
            new HashSet<ZLinkMessageKind>
            {
                ZLinkMessageKind.Command,
                ZLinkMessageKind.Request,
            });

        var hasHandlerExposure = channel.SendHandlers.Count > 0
            || channel.RequestHandlers.Count > 0
            || ChannelGroupsExposeAny(
                channel,
                handlerGroups,
                ZLinkMessageKind.Command,
                ZLinkMessageKind.Request);

        if (hasHandlerExposure && channel.Server is null)
        {
            throw new ZLinkConfigurationException(
                $"client/server channel '{channel.ChannelName}' exposes handlers but does not enable server capability.");
        }

        if (channel.Server is not null
            && !hasHandlerExposure
            && !acceptedBySpotRouteChannel)
        {
            throw new ZLinkConfigurationException(
                $"client/server channel '{channel.ChannelName}' server must map a handler group, register a typed handler, or be accepted by a SPOT route channel.");
        }

        if (channel.PublishHandlers.Count > 0)
        {
            throw new ZLinkConfigurationException(
                $"client/server channel '{channel.ChannelName}' cannot register publish handlers.");
        }
    }

    private static void ValidateFanoutHandlerExposure(
        ZLinkChannelRegistration channel,
        IReadOnlyDictionary<string, HashSet<ZLinkMessageKind>> handlerGroups)
    {
        ValidateMappedGroups(
            channel,
            handlerGroups,
            new HashSet<ZLinkMessageKind> { ZLinkMessageKind.Publish });

        var hasHandlerExposure = channel.PublishHandlers.Count > 0
            || ChannelGroupsExposeAny(channel, handlerGroups, ZLinkMessageKind.Publish);

        if (hasHandlerExposure && channel.Subscriber is null)
        {
            throw new ZLinkConfigurationException(
                $"fanout channel '{channel.ChannelName}' exposes publish handlers but does not enable subscriber capability.");
        }

        if (channel.Subscriber is not null && !hasHandlerExposure)
        {
            throw new ZLinkConfigurationException(
                $"fanout channel '{channel.ChannelName}' subscriber must map a publish handler group or register a typed publish handler.");
        }

        if (channel.SendHandlers.Count > 0 || channel.RequestHandlers.Count > 0)
        {
            throw new ZLinkConfigurationException(
                $"fanout channel '{channel.ChannelName}' cannot register send or request handlers.");
        }
    }

    private static void ValidateMappedGroups(
        ZLinkChannelRegistration channel,
        IReadOnlyDictionary<string, HashSet<ZLinkMessageKind>> handlerGroups,
        IReadOnlySet<ZLinkMessageKind> allowedKinds)
    {
        foreach (var group in channel.HandlerGroups)
        {
            if (!handlerGroups.TryGetValue(group, out var kinds))
            {
                throw new ZLinkConfigurationException(
                    $"channel '{channel.ChannelName}' maps unknown handler group '{group}'.");
            }

            foreach (var kind in kinds)
            {
                if (!allowedKinds.Contains(kind))
                {
                    throw new ZLinkConfigurationException(
                        $"channel '{channel.ChannelName}' maps handler group '{group}' with incompatible handler kind '{kind}'.");
                }
            }
        }
    }

    private static bool ChannelGroupsExposeAny(
        ZLinkChannelRegistration channel,
        IReadOnlyDictionary<string, HashSet<ZLinkMessageKind>> handlerGroups,
        params ZLinkMessageKind[] kinds)
    {
        return channel.HandlerGroups.Any(group =>
            handlerGroups.TryGetValue(group, out var groupKinds)
            && kinds.Any(groupKinds.Contains));
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
            {
                throw new ZLinkConfigurationException(
                    $"Duplicate {label} handler '{channelName}:{packetName}'.");
            }
        }
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
            case ZLinkAutoConnectType.DealerMesh:
                RequireDealerMeshShape(channel);
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
        {
            throw new ZLinkConfigurationException(
                $"client/server channel '{channel.ChannelName}' must enable server or client capabilities.");
        }

        if (channel.Publisher is not null || channel.Subscriber is not null)
        {
            throw new ZLinkConfigurationException(
                $"client/server channel '{channel.ChannelName}' cannot enable publisher or subscriber capabilities.");
        }
    }

    private static void RequireFanoutShape(ZLinkChannelRegistration channel)
    {
        if (channel.Publisher is null && channel.Subscriber is null)
        {
            throw new ZLinkConfigurationException(
                $"fanout channel '{channel.ChannelName}' must enable publisher or subscriber capabilities.");
        }

        if (channel.Server is not null || channel.Client is not null)
        {
            throw new ZLinkConfigurationException(
                $"fanout channel '{channel.ChannelName}' cannot enable server or client capabilities.");
        }
    }

    private static void RequireDealerMeshShape(ZLinkChannelRegistration channel)
    {
        if (channel.Client is null)
        {
            throw new ZLinkConfigurationException(
                $"dealer mesh channel '{channel.ChannelName}' must enable client capabilities.");
        }

        if (channel.Server is not null || channel.Publisher is not null || channel.Subscriber is not null)
        {
            throw new ZLinkConfigurationException(
                $"dealer mesh channel '{channel.ChannelName}' can only enable client capabilities.");
        }
    }
}

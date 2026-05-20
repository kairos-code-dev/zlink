namespace Zlink.Framework.Runtime.Configuration;

internal static partial class ZLinkFrameworkRegistrationValidator
{
    private static void ValidateRouteChannel(
        ZLinkRouteChannelRegistration routed,
        bool discoveryConfigured,
        bool acceptedBySpotRouteChannel)
    {
        if (string.IsNullOrWhiteSpace(routed.BindEndpoint))
        {
            throw new ZLinkConfigurationException(
                $"Route channel '{routed.RouterChannelId}' must define a bind endpoint.");
        }

        if (!acceptedBySpotRouteChannel
            || discoveryConfigured
            || routed.ManualConnections.Count > 0)
        {
            ZLinkPeerAcquisitionPolicy.RequireSinglePeerSource(
                $"Route channel '{routed.RouterChannelId}'",
                discoveryConfigured,
                routed.ManualConnections);
        }

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
            {
                throw new ZLinkConfigurationException(
                    $"Duplicate routed {label} handler '{routerChannelId}:{packetName}'.");
            }
        }
    }
}

namespace Zlink.Framework.Configuration;

internal static partial class ZLinkFrameworkRegistrationValidator
{
    private static void ValidateRouteChannel(
        ZLinkRouteChannelRegistration routed,
        bool discoveryConfigured)
    {
        if (string.IsNullOrWhiteSpace(routed.BindEndpoint))
        {
            throw new ZLinkConfigurationException(
                $"Route channel '{routed.RouterChannelId}' must define a bind endpoint.");
        }

        ZLinkPeerAcquisitionPolicy.RequireSinglePeerSource(
            $"Route channel '{routed.RouterChannelId}'",
            discoveryConfigured,
            routed.ManualConnections);

        var keys = new HashSet<(ZLinkMessageKind Kind, string PacketName)>();
        foreach (var handler in routed.SendHandlers)
        {
            var packetName = handler.PacketName ?? ZLinkMessageNameResolver.ResolveFromType(handler.MessageType);
            if (!keys.Add((ZLinkMessageKind.Command, packetName)))
            {
                throw new ZLinkConfigurationException(
                    $"Duplicate routed send handler '{routed.RouterChannelId}:{packetName}'.");
            }
        }

        foreach (var handler in routed.RequestHandlers)
        {
            var packetName = handler.PacketName ?? ZLinkMessageNameResolver.ResolveFromType(handler.MessageType);
            if (!keys.Add((ZLinkMessageKind.Request, packetName)))
            {
                throw new ZLinkConfigurationException(
                    $"Duplicate routed request handler '{routed.RouterChannelId}:{packetName}'.");
            }
        }
    }
}

namespace Zlink.Framework.Runtime.Configuration;

internal static partial class ZLinkFrameworkRegistrationValidator
{
    private static void ValidateRouteChannel(
        ZLinkRouteChannelRegistration routed,
        bool autoConnectConfigured,
        ZLinkHandlerExposureCatalog handlerExposure)
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

        handlerExposure.ValidateRoute(routed);
    }
}

namespace Zlink.Framework.Runtime.Configuration;

internal static partial class ZLinkFrameworkRegistrationValidator
{
    public static void Validate(ZLinkFrameworkRegistration registration)
    {
        registration.FreezeScannedHandlerCatalog();
        var globalSpotFactories = new HashSet<Type>();
        var globalEntrySpots = new HashSet<Type>();
        var channelHandlerEndpoints = registration.ScannedHandlerCatalog.ChannelEndpoints;
        var routeHandlerEndpoints = registration.ScannedHandlerCatalog.RouteEndpoints;
        var handlerExposure = ZLinkHandlerExposureCatalog.Build(channelHandlerEndpoints, routeHandlerEndpoints);

        ValidateDispatchOptions(registration.DispatchOptions);

        ValidateLocations(registration);
        ValidateActorTransferOptions(registration);

        foreach (var channel in registration.Channels.Values)
            ValidateChannel(
                channel,
                registration.Locations.Enabled,
                handlerExposure);

        foreach (var streamNode in registration.StreamNodes.Values) ValidateStreamNode(streamNode, registration);

        foreach (var routed in registration.RouteChannels.Values)
            ValidateRouteChannel(
                routed,
                registration.Locations.Enabled,
                handlerExposure);

        foreach (var spotNode in registration.SpotNodes.Values)
            ValidateSpotNode(
                spotNode,
                registration,
                globalSpotFactories,
                globalEntrySpots);

        var actorCapableNodes = registration.SpotNodes.Values
            .Where(static spotNode => spotNode.ActorFactories.Count > 0)
            .ToArray();
        if (actorCapableNodes.Length > 1)
            throw new ZLinkConfigurationException(
                "Actor factory registration is ambiguous because more than one SpotNode owns actor factories.");

        registration.ActorCatalog.Build(registration.SpotNodes.Values);
    }

    private static void ValidateDispatchOptions(ZLinkDispatchOptionsModel options)
    {
        if (options.Unhandled.Send == ZLinkUnhandledDispatchAction.ReplyError)
            throw new ZLinkConfigurationException(
                "Unhandled send dispatch cannot use ReplyError because send has no reply path.");

        if (options.Unhandled.Publish == ZLinkUnhandledDispatchAction.ReplyError)
            throw new ZLinkConfigurationException(
                "Unhandled publish dispatch cannot use ReplyError because publish has no reply path.");

        if (double.IsNaN(options.Diagnostics.SampleRate)
            || options.Diagnostics.SampleRate < 0.0d
            || options.Diagnostics.SampleRate > 1.0d)
            throw new ZLinkConfigurationException(
                "Diagnostics SampleRate must be between 0.0 and 1.0.");
    }

    private static void ValidateActorTransferOptions(ZLinkFrameworkRegistration registration)
    {
        var hasTransferAdapter = registration.SpotNodes.Values
            .Any(static node => node.ActorTransfers.Count > 0);
        if (!hasTransferAdapter)
            return;

        if (registration.ActorTransferTimeout is null
            || registration.ActorTransferForwardWindow is null)
            throw new ZLinkConfigurationException(
                "ActorTransferTimeout and ActorTransferForwardWindow must both be set when an actor transfer adapter is registered.");
    }

    private static void ValidateStreamNode(
        ZLinkStreamNodeRegistration streamNode,
        ZLinkFrameworkRegistration registration)
    {
        if (string.IsNullOrWhiteSpace(streamNode.BindEndpoint))
            throw new ZLinkConfigurationException(
                $"STREAM node '{streamNode.StreamNodeName}' must define a bind endpoint.");

        if (streamNode.HeaderSessionType is null)
            throw new ZLinkConfigurationException(
                $"STREAM node '{streamNode.StreamNodeName}' must register a header stream session.");
    }
}

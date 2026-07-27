namespace Zlink.Framework.Runtime.Configuration;

internal static partial class ZLinkFrameworkRegistrationValidator
{
    public static void Validate(ZLinkFrameworkRegistration registration)
    {
        registration.FreezeScannedHandlerCatalog();
        var globalSpotFactories = new HashSet<Type>();
        var globalEntrySpots = new HashSet<Type>();
        var channelHandlerEndpoints = registration.ScannedHandlerCatalog.ChannelEndpoints;
        var handlerExposure = ZLinkHandlerExposureCatalog.Build(channelHandlerEndpoints);

        ValidateDispatchOptions(registration.DispatchOptions);

        ValidateLocations(registration);
        ValidateRelocationStoreRequirement(registration);

        foreach (var channel in registration.Channels.Values)
            ValidateChannel(
                channel,
                registration.Locations.Enabled,
                handlerExposure);

        foreach (var streamNode in registration.StreamNodes.Values) ValidateStreamNode(streamNode, registration);

        foreach (var spotNode in registration.SpotNodes.Values)
            ValidateSpotNode(
                spotNode,
                registration,
                globalSpotFactories,
                globalEntrySpots,
                handlerExposure);

        var clientServerChannels = registration.Channels.Values
            .Where(static channel =>
                channel.AutoConnectType == ZLinkLocationAutoConnectType.ClientServer)
            .Select(static channel => channel.ChannelName)
            .ToHashSet(StringComparer.Ordinal);
        foreach (var node in registration.SpotNodes.Values)
        foreach (var membership in node.ChannelMemberships)
            if (clientServerChannels.Contains(membership.ChannelName))
                throw new ZLinkConfigurationException(
                    $"ChannelName '{membership.ChannelName}' is registered on both RouteMesh and ClientServer physical paths.");

        registration.ActorCatalog.Build(registration.SpotNodes.Values);
    }

    private static void ValidateRelocationStoreRequirement(
        ZLinkFrameworkRegistration registration)
    {
        var requiresRelocationStore = registration.SpotNodes.Values.Any(
            static node =>
                node.InstanceSpotFactories.Count > 0
                || node.SpotRelocations.Values.Any(
                    static relocation => relocation.PolicyKind != 0)
                || node.ActorRelocations.Values.Any(
                    static relocation => relocation.PolicyKind != 0));
        if (!requiresRelocationStore)
            return;

        if (registration.Locations.ResolveRelocationStore() is null)
            throw new ZLinkConfigurationException(
                "An Object Server with a Recreate or Snapshot relocation policy, "
                + "or any Instance Spot factory, requires exactly one Relocation Store. "
                + "Register it via AddRelocationStore(...) before startup.");
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

    private static void ValidateStreamNode(
        ZLinkStreamNodeRegistration streamNode,
        ZLinkFrameworkRegistration registration)
    {
        if (string.IsNullOrWhiteSpace(streamNode.BindEndpoint)
            && streamNode.ListenPort is null)
            throw new ZLinkConfigurationException(
                $"STREAM node '{streamNode.StreamNodeName}' must define a bind endpoint.");

        if (streamNode.HeaderSessionType is null)
            throw new ZLinkConfigurationException(
                $"STREAM node '{streamNode.StreamNodeName}' must register a header stream session.");

        if (!streamNode.ActorDispatchEnabled)
            return;
        if (!registration.Locations.Enabled)
            throw new ZLinkConfigurationException(
                $"STREAM node '{streamNode.StreamNodeName}' enables Actor dispatch but no Location Store is registered.");

        var objectMeshes = registration.SpotNodes.Values
            .Where(static node => node.ObjectRoleSelected)
            .ToArray();
        if (objectMeshes.Length == 0)
            throw new ZLinkConfigurationException(
                $"STREAM node '{streamNode.StreamNodeName}' requires at least one local Object Client or Server MeshNode.");
    }
}

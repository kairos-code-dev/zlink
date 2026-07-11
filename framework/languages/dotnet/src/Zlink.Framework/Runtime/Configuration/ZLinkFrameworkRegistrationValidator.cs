namespace Zlink.Framework.Runtime.Configuration;

internal static partial class ZLinkFrameworkRegistrationValidator
{
    public static void Validate(ZLinkFrameworkRegistration registration)
    {
        var globalSpotFactories = new HashSet<Type>();
        var channelHandlerEndpoints = ScanChannelHandlerEndpoints(registration);
        var routeHandlerEndpoints = ScanRouteHandlerEndpoints(registration);
        var handlerGroups = BuildHandlerGroupCatalog(channelHandlerEndpoints, routeHandlerEndpoints);

        ValidateDispatchOptions(registration.DispatchOptions);

        ValidateLocations(registration);

        foreach (var channel in registration.Channels.Values)
            ValidateChannel(
                channel,
                registration.Locations.Enabled,
                handlerGroups,
                channelHandlerEndpoints);

        foreach (var streamNode in registration.StreamNodes.Values) ValidateStreamNode(streamNode, registration);

        foreach (var routed in registration.RouteChannels.Values)
            ValidateRouteChannel(
                routed,
                registration.Locations.Enabled,
                handlerGroups,
                routeHandlerEndpoints);

        foreach (var spotNode in registration.SpotNodes.Values)
            ValidateSpotNode(
                spotNode,
                registration,
                globalSpotFactories);

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

    private static IReadOnlyList<ZLinkHandlerEndpointDescriptor> ScanChannelHandlerEndpoints(
        ZLinkFrameworkRegistration registration)
    {
        var endpoints = new List<ZLinkHandlerEndpointDescriptor>();
        foreach (var assembly in registration.EnumerateHandlerScanAssemblies())
            endpoints.AddRange(ZLinkHandlerScanner.Scan(assembly));

        return endpoints;
    }

    private static IReadOnlyList<ZLinkRouteHandlerEndpointDescriptor> ScanRouteHandlerEndpoints(
        ZLinkFrameworkRegistration registration)
    {
        var endpoints = new List<ZLinkRouteHandlerEndpointDescriptor>();
        foreach (var assembly in registration.EnumerateHandlerScanAssemblies())
            endpoints.AddRange(ZLinkHandlerScanner.ScanRoute(assembly));

        return endpoints;
    }

    private static IReadOnlyDictionary<string, HashSet<ZLinkHandlerGroupCatalogEntry>> BuildHandlerGroupCatalog(
        IReadOnlyList<ZLinkHandlerEndpointDescriptor> channelHandlerEndpoints,
        IReadOnlyList<ZLinkRouteHandlerEndpointDescriptor> routeHandlerEndpoints)
    {
        var groups = new Dictionary<string, HashSet<ZLinkHandlerGroupCatalogEntry>>(StringComparer.Ordinal);
        foreach (var endpoint in channelHandlerEndpoints)
        foreach (var group in endpoint.Groups)
            AddHandlerGroupCatalogEntry(
                groups,
                group,
                new ZLinkHandlerGroupCatalogEntry(
                    ZLinkHandlerEndpointSurface.Channel,
                    endpoint.Kind));

        foreach (var endpoint in routeHandlerEndpoints)
        foreach (var group in endpoint.Groups)
            AddHandlerGroupCatalogEntry(
                groups,
                group,
                new ZLinkHandlerGroupCatalogEntry(
                    ZLinkHandlerEndpointSurface.Route,
                    endpoint.Kind));

        return groups;
    }

    private static void AddHandlerGroupCatalogEntry(
        IDictionary<string, HashSet<ZLinkHandlerGroupCatalogEntry>> groups,
        string group,
        ZLinkHandlerGroupCatalogEntry entry)
    {
        if (!groups.TryGetValue(group, out var entries))
        {
            entries = [];
            groups.Add(group, entries);
        }

        entries.Add(entry);
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

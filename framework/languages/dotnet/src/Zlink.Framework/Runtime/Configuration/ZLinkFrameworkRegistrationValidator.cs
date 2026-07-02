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

        ValidateLocations(registration.Locations);

        if (registration.RegistrySpotRemoteAddresses is not null
            && registration.Discovery?.Endpoints.Count is not > 0
            && !registration.SpotDiscoveries.Values.Any(discovery =>
                ResolveSpotDiscoveryEndpoints(registration, discovery).Count > 0))
            throw new ZLinkConfigurationException(
                "Registry remote address resolver requires discovery endpoints from UseDiscovery(...AddRegistryEndpoint...).");

        ValidateRegistryRouteChannel(
            registration.RegistrySpotRemoteAddresses is not null,
            registration.RegistrySpotRemoteAddresses?.RouterChannelId,
            registration,
            "Registry SPOT remote address resolver");

        foreach (var channel in registration.Channels.Values)
            ValidateChannel(
                channel,
                registration.Discovery is not null,
                false,
                handlerGroups,
                channelHandlerEndpoints);

        foreach (var streamNode in registration.StreamNodes.Values) ValidateStreamNode(streamNode, registration);

        foreach (var routed in registration.RouteChannels.Values)
            ValidateRouteChannel(
                routed,
                registration.Discovery is not null,
                registration.SpotNodes.Values.Any(static spotNode => spotNode.Router is not null),
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
        foreach (var assembly in registration.HandlerAssemblies) endpoints.AddRange(ZLinkHandlerScanner.Scan(assembly));

        return endpoints;
    }

    private static IReadOnlyList<ZLinkRouteHandlerEndpointDescriptor> ScanRouteHandlerEndpoints(
        ZLinkFrameworkRegistration registration)
    {
        var endpoints = new List<ZLinkRouteHandlerEndpointDescriptor>();
        foreach (var assembly in registration.HandlerAssemblies)
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

    private static void ValidateRegistryRouteChannel(
        bool enabled,
        string? routerChannelId,
        ZLinkFrameworkRegistration registration,
        string capabilityName)
    {
        if (!enabled) return;

        if (!string.IsNullOrWhiteSpace(routerChannelId))
        {
            if (!registration.RouteChannels.ContainsKey(routerChannelId)
                && !registration.SpotNodes.ContainsKey(routerChannelId))
                throw new ZLinkConfigurationException(
                    $"{capabilityName} references unknown route mesh channel or SPOT node '{routerChannelId}'.");

            return;
        }

        var routerCapableSpotNodeCount =
            registration.SpotNodes.Values.Count(static spotNode => spotNode.Router is not null);
        if (registration.RouteChannels.Count + routerCapableSpotNodeCount != 1)
            throw new ZLinkConfigurationException(
                $"{capabilityName} requires RouterChannelId when there is not exactly one route mesh channel or router-capable SPOT node.");
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

    internal static IReadOnlyList<string> ResolveSpotDiscoveryEndpoints(
        ZLinkFrameworkRegistration registration,
        ZLinkSpotDiscoveryRegistration? discovery = null)
    {
        if (discovery is { Endpoints.Count: > 0 }) return discovery.Endpoints;

        if (discovery is null
            && registration.SpotDiscovery is { Endpoints.Count: > 0 } singleDiscovery)
            return singleDiscovery.Endpoints;

        return registration.Discovery?.Endpoints ?? [];
    }
}
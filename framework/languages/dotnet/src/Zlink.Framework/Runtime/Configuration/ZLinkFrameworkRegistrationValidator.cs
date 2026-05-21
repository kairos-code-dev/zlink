namespace Zlink.Framework.Runtime.Configuration;

internal static partial class ZLinkFrameworkRegistrationValidator
{
    public static void Validate(ZLinkFrameworkRegistration registration)
    {
        var globalSpotFactories = new HashSet<string>(StringComparer.Ordinal);
        var globalSpotPublisherChannels = new HashSet<string>(StringComparer.Ordinal);
        var channelHandlerEndpoints = ScanChannelHandlerEndpoints(registration);
        var routeHandlerEndpoints = ScanRouteHandlerEndpoints(registration);
        var handlerGroups = BuildHandlerGroupCatalog(channelHandlerEndpoints, routeHandlerEndpoints);

        if (registration.SpotDiscovery is { RequiresUseDiscovery: true, UseDiscoveryCalled: false })
        {
            throw new ZLinkConfigurationException(
                "AddSpotMesh(...) requires spotMesh.UseDiscovery(...).");
        }

        if (registration.ActorFactories.Count > 0 && registration.SpotNodes.Count == 0)
        {
            throw new ZLinkConfigurationException(
                "Actor factory registration requires at least one SpotNode.");
        }

        if ((registration.ActorSessionBindingStoreType is not null
                || registration.RegistryActorSessionBindings is not null)
            && registration.RouteChannels.Count == 0)
        {
            throw new ZLinkConfigurationException(
                "Actor session proxy requires AddRouteMeshChannel(...).");
        }

        if (registration.RegistryActorSessionBindings is not null
            && (registration.Discovery is null
                || registration.Discovery.Endpoints.Count == 0))
        {
            throw new ZLinkConfigurationException(
                "Registry actor-session binding store requires UseDiscovery(...).");
        }

        if (registration.RegistryActorSessionBindings is not null
            && registration.RouteChannels.Count != 1)
        {
            throw new ZLinkConfigurationException(
                "Registry actor-session binding store requires exactly one route mesh channel.");
        }

        if ((registration.RegistryActorRoutes is not null
                || registration.RegistrySpotRoutes is not null)
            && registration.RouteChannels.Count == 0)
        {
            throw new ZLinkConfigurationException(
                "Registry route resolver requires AddRouteMeshChannel(...).");
        }

        if (registration.RegistryActorRoutes is not null
            && (registration.Discovery is null
                || registration.Discovery.Endpoints.Count == 0))
        {
            throw new ZLinkConfigurationException(
                "Registry actor route resolver requires UseDiscovery(...).");
        }

        if (registration.RegistrySpotRoutes is not null
            && (registration.SpotDiscovery is null
                || registration.SpotDiscovery.Endpoints.Count == 0))
        {
            throw new ZLinkConfigurationException(
                "Registry route resolver requires AddSpotMesh(...).UseDiscovery(...).");
        }

        ValidateRegistryRouteChannel(
            registration.RegistryActorRoutes is not null,
            registration.RegistryActorRoutes?.RouterChannelId,
            registration,
            "Registry actor route resolver");
        ValidateRegistryRouteChannel(
            registration.RegistrySpotRoutes is not null,
            registration.RegistrySpotRoutes?.RouterChannelId,
            registration,
            "Registry SPOT route resolver");

        foreach (var channel in registration.Channels.Values)
        {
            ValidateChannel(
                channel,
                registration.Discovery is not null,
                IsAcceptedSpotRouteChannel(registration, channel.ChannelName),
                handlerGroups,
                channelHandlerEndpoints);
        }

        foreach (var streamNode in registration.StreamNodes.Values)
        {
            ValidateStreamNode(streamNode);
        }

        foreach (var routed in registration.RouteChannels.Values)
        {
            ValidateRouteChannel(
                routed,
                registration.Discovery is not null,
                IsAcceptedSpotRouteChannel(registration, routed.RouterChannelId),
                handlerGroups,
                routeHandlerEndpoints);
        }

        foreach (var spotNode in registration.SpotNodes.Values)
        {
            ValidateSpotNode(
                spotNode,
                registration,
                globalSpotFactories,
                globalSpotPublisherChannels);
        }
    }

    private static IReadOnlyList<ZLinkHandlerEndpointDescriptor> ScanChannelHandlerEndpoints(
        ZLinkFrameworkRegistration registration)
    {
        var endpoints = new List<ZLinkHandlerEndpointDescriptor>();
        foreach (var assembly in registration.HandlerAssemblies)
        {
            endpoints.AddRange(ZLinkHandlerScanner.Scan(assembly));
        }

        return endpoints;
    }

    private static IReadOnlyList<ZLinkRouteHandlerEndpointDescriptor> ScanRouteHandlerEndpoints(
        ZLinkFrameworkRegistration registration)
    {
        var endpoints = new List<ZLinkRouteHandlerEndpointDescriptor>();
        foreach (var assembly in registration.HandlerAssemblies)
        {
            endpoints.AddRange(ZLinkHandlerScanner.ScanRoute(assembly));
        }

        return endpoints;
    }

    private static IReadOnlyDictionary<string, HashSet<ZLinkHandlerGroupCatalogEntry>> BuildHandlerGroupCatalog(
        IReadOnlyList<ZLinkHandlerEndpointDescriptor> channelHandlerEndpoints,
        IReadOnlyList<ZLinkRouteHandlerEndpointDescriptor> routeHandlerEndpoints)
    {
        var groups = new Dictionary<string, HashSet<ZLinkHandlerGroupCatalogEntry>>(StringComparer.Ordinal);
        foreach (var endpoint in channelHandlerEndpoints)
        {
            foreach (var group in endpoint.Groups)
            {
                AddHandlerGroupCatalogEntry(
                    groups,
                    group,
                    new ZLinkHandlerGroupCatalogEntry(
                        ZLinkHandlerEndpointSurface.Channel,
                        endpoint.Kind));
            }
        }

        foreach (var endpoint in routeHandlerEndpoints)
        {
            foreach (var group in endpoint.Groups)
            {
                AddHandlerGroupCatalogEntry(
                    groups,
                    group,
                    new ZLinkHandlerGroupCatalogEntry(
                        ZLinkHandlerEndpointSurface.Route,
                        endpoint.Kind));
            }
        }

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

    private static bool IsAcceptedSpotRouteChannel(
        ZLinkFrameworkRegistration registration,
        string channelName)
    {
        return registration.SpotNodes.Values.Any(
            spotNode => spotNode.AcceptedSpotRouteChannels.ContainsKey(channelName));
    }

    private static void ValidateRegistryRouteChannel(
        bool enabled,
        string? routerChannelId,
        ZLinkFrameworkRegistration registration,
        string capabilityName)
    {
        if (!enabled)
        {
            return;
        }

        if (!string.IsNullOrWhiteSpace(routerChannelId))
        {
            if (!registration.RouteChannels.ContainsKey(routerChannelId))
            {
                throw new ZLinkConfigurationException(
                    $"{capabilityName} references unknown route mesh channel '{routerChannelId}'.");
            }

            return;
        }

        if (registration.RouteChannels.Count != 1)
        {
            throw new ZLinkConfigurationException(
                $"{capabilityName} requires RouterChannelId when there is not exactly one route mesh channel.");
        }
    }

    private static void ValidateStreamNode(ZLinkStreamNodeRegistration streamNode)
    {
        if (string.IsNullOrWhiteSpace(streamNode.BindEndpoint))
        {
            throw new ZLinkConfigurationException(
                $"STREAM node '{streamNode.StreamNodeName}' must define a bind endpoint.");
        }

        if (streamNode.HeaderSessionType is null)
        {
            throw new ZLinkConfigurationException(
                $"STREAM node '{streamNode.StreamNodeName}' must register a header stream session.");
        }
    }

}

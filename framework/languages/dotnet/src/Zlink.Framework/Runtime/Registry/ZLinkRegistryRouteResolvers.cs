using System.Text;
using System.Buffers.Binary;

namespace Zlink.Framework.Runtime.Registry;

internal sealed class ZLinkRegistryActorRouteResolver(
    ZLinkFrameworkRuntime runtime,
    ZLinkFrameworkRegistration registration) : IZLinkActorPlayRouteResolver
{
    public async ValueTask<ZLinkActorRoute> ResolvePlayRouteAsync(
        string actorId,
        CancellationToken cancellationToken)
    {
        var options = registration.RegistryActorRoutes
            ?? throw new ZLinkConfigurationException("Registry actor routes are not configured.");
        var state = await runtime.GetStartedStateForRoutingAsync(cancellationToken)
            .ConfigureAwait(false);
        var routerChannelId = ResolveRouterChannelId(state, options.RouterChannelId);
        var discovery = ResolveSpotDiscovery(state);

        try
        {
            var route = discovery.ResolveActor(actorId);
            if (!string.Equals(route.Actor.ActorId, actorId, StringComparison.Ordinal))
            {
                throw NotFound(actorId);
            }

            return new ZLinkActorRoute(routerChannelId, route.Actor.NodeRid);
        }
        catch (ZlinkConfigException error) when (error.InternalErrno == 2)
        {
            throw NotFound(actorId, error);
        }
    }

    private static ZLinkFrameworkException NotFound(
        string actorId,
        Exception? inner = null)
        => new(
            ZLinkFrameworkErrorKind.ActorRouteNotFound,
            $"Actor route was not found for '{actorId}'.",
            isRetriable: true,
            innerException: inner);

    internal static string ResolveRouterChannelId(
        ZLinkFrameworkRuntimeState state,
        string? configured)
    {
        if (!string.IsNullOrWhiteSpace(configured))
        {
            if (!state.RouteChannels.ContainsKey(configured))
            {
                throw new ZLinkConfigurationException(
                    $"Route mesh channel '{configured}' is not registered.");
            }

            return configured;
        }

        if (state.RouteChannels.Count == 1)
        {
            return state.RouteChannels.Keys.Single();
        }

        throw new ZLinkConfigurationException(
            "Registry route resolver requires RouterChannelId when there is not exactly one route mesh channel.");
    }

    internal static IZLinkBackendDiscovery ResolveSpotDiscovery(
        ZLinkFrameworkRuntimeState state)
    {
        if (state.SpotDiscoveries.Count == 1)
        {
            return state.SpotDiscoveries.Values.Single();
        }

        throw new ZLinkConfigurationException(
            "Registry route resolver requires exactly one configured SPOT discovery.");
    }
}

internal sealed class ZLinkRegistrySpotRouteResolver(
    ZLinkFrameworkRuntime runtime,
    ZLinkFrameworkRegistration registration) : IZLinkSpotRouteResolver
{
    private const byte SpotNameRoutePayloadVersion = 1;

    public async ValueTask<ZLinkSpotRoute> ResolveSpotRouteAsync(
        string spotName,
        CancellationToken cancellationToken)
    {
        var options = registration.RegistrySpotRoutes
            ?? throw new ZLinkConfigurationException("Registry SPOT routes are not configured.");
        var state = await runtime.GetStartedStateForRoutingAsync(cancellationToken)
            .ConfigureAwait(false);
        var discovery = ZLinkRegistryActorRouteResolver.ResolveSpotDiscovery(state);
        var routeKey = BuildSpotNameRouteKey(options.Namespace, spotName);

        try
        {
            using var route = discovery.ResolveRoute(DiscoveryRouteKind.SpotName, routeKey);
            var spotRid = DecodeSpotNameRouteValue(
                route.Value,
                options.Namespace,
                spotName);
            return await ResolveSpotRouteAsync(spotRid, cancellationToken)
                .ConfigureAwait(false);
        }
        catch (ZlinkConfigException error) when (error.InternalErrno == 2)
        {
            throw NotFound(spotName, error);
        }
        catch (FormatException error)
        {
            throw NotFound(spotName, error);
        }
    }

    public async ValueTask<ZLinkSpotRoute> ResolveSpotRouteAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken)
    {
        var options = registration.RegistrySpotRoutes
            ?? throw new ZLinkConfigurationException("Registry SPOT routes are not configured.");
        var state = await runtime.GetStartedStateForRoutingAsync(cancellationToken)
            .ConfigureAwait(false);
        var routerChannelId = ZLinkRegistryActorRouteResolver.ResolveRouterChannelId(
            state,
            options.RouterChannelId);
        var discovery = ZLinkRegistryActorRouteResolver.ResolveSpotDiscovery(state);

        try
        {
            var ownerRid = discovery.ResolveSpot(spotRid);
            return new ZLinkSpotRoute(routerChannelId, ownerRid, spotRid);
        }
        catch (ZlinkConfigException error) when (error.InternalErrno == 2)
        {
            throw NotFound(spotRid.ToHex(), error);
        }
    }

    internal static byte[] BuildSpotNameRouteKey(string namespaceName, string spotName)
    {
        if (string.IsNullOrWhiteSpace(spotName))
        {
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.SpotRouteNotFound,
                "SPOT name route requires a non-empty SPOT name.",
                isRetriable: false);
        }

        return Encoding.UTF8.GetBytes($"{namespaceName}\0{spotName}");
    }

    internal static byte[] EncodeSpotNameRouteValue(
        string namespaceName,
        string spotName,
        RoutingId spotRid)
    {
        var namespaceBytes = Encoding.UTF8.GetBytes(namespaceName);
        var spotNameBytes = Encoding.UTF8.GetBytes(spotName);
        var spotRidBytes = spotRid.ToBytes();
        if (namespaceBytes.Length > ushort.MaxValue
            || spotNameBytes.Length > ushort.MaxValue
            || spotRidBytes.Length > byte.MaxValue)
        {
            throw new ZLinkConfigurationException("SPOT name route payload is too large.");
        }

        var value = new byte[
            1
            + sizeof(ushort) + namespaceBytes.Length
            + sizeof(ushort) + spotNameBytes.Length
            + 1 + spotRidBytes.Length];
        var span = value.AsSpan();
        span[0] = SpotNameRoutePayloadVersion;
        var offset = 1;
        BinaryPrimitives.WriteUInt16BigEndian(
            span.Slice(offset, sizeof(ushort)),
            (ushort)namespaceBytes.Length);
        offset += sizeof(ushort);
        namespaceBytes.CopyTo(span[offset..]);
        offset += namespaceBytes.Length;
        BinaryPrimitives.WriteUInt16BigEndian(
            span.Slice(offset, sizeof(ushort)),
            (ushort)spotNameBytes.Length);
        offset += sizeof(ushort);
        spotNameBytes.CopyTo(span[offset..]);
        offset += spotNameBytes.Length;
        span[offset++] = (byte)spotRidBytes.Length;
        spotRidBytes.CopyTo(span[offset..]);
        return value;
    }

    private static RoutingId DecodeSpotNameRouteValue(
        Message value,
        string expectedNamespace,
        string expectedSpotName)
    {
        var bytes = value.AsReadOnlySpan();
        if (bytes.Length < 6 || bytes[0] != SpotNameRoutePayloadVersion)
        {
            throw new FormatException("Invalid SPOT name route payload.");
        }

        var offset = 1;
        var namespaceLength = BinaryPrimitives.ReadUInt16BigEndian(
            bytes.Slice(offset, sizeof(ushort)));
        offset += sizeof(ushort);
        if (bytes.Length < offset + namespaceLength + sizeof(ushort))
        {
            throw new FormatException("Invalid SPOT name route payload.");
        }

        var namespaceName = Encoding.UTF8.GetString(
            bytes.Slice(offset, namespaceLength));
        offset += namespaceLength;
        var spotNameLength = BinaryPrimitives.ReadUInt16BigEndian(
            bytes.Slice(offset, sizeof(ushort)));
        offset += sizeof(ushort);
        if (bytes.Length < offset + spotNameLength + 1)
        {
            throw new FormatException("Invalid SPOT name route payload.");
        }

        var spotName = Encoding.UTF8.GetString(bytes.Slice(offset, spotNameLength));
        offset += spotNameLength;
        var ridLength = bytes[offset++];
        if (bytes.Length != offset + ridLength)
        {
            throw new FormatException("Invalid SPOT name route payload.");
        }

        if (!string.Equals(namespaceName, expectedNamespace, StringComparison.Ordinal)
            || !string.Equals(spotName, expectedSpotName, StringComparison.Ordinal))
        {
            throw new FormatException("SPOT name route payload identity mismatch.");
        }

        return RoutingId.FromBytes(bytes.Slice(offset, ridLength));
    }

    private static ZLinkFrameworkException NotFound(
        string identity,
        Exception? inner = null)
        => new(
            ZLinkFrameworkErrorKind.SpotRouteNotFound,
            $"SPOT route was not found for '{identity}'.",
            isRetriable: true,
            innerException: inner);
}

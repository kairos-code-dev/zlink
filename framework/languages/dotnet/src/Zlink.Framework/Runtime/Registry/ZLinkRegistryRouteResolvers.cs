using System.Collections.Concurrent;
namespace Zlink.Framework.Runtime.Registry;

internal sealed class ZLinkRegistryActorRemoteAddressResolver(
    ZLinkFrameworkRuntime runtime,
    ZLinkFrameworkRegistration registration) : IZLinkActorRemoteAddressResolver
{
    public async ValueTask<ZLinkActorRemoteLocation> ResolveActorRemoteAddressAsync(
        string actorId,
        CancellationToken cancellationToken)
    {
        var options = registration.RegistryActorRemoteAddresses
            ?? throw new ZLinkConfigurationException("Registry actor routes are not configured.");
        var state = await runtime.GetStartedStateForRoutingAsync(cancellationToken)
            .ConfigureAwait(false);
        var routerChannelId = ZLinkRegistryRouteRuntime.ResolveRouterChannelId(
            state,
            options.RouterChannelId);
        var discovery = ZLinkRegistryRouteRuntime.ResolveSingleSpotDiscovery(state);

        try
        {
            var route = discovery.ResolveActor(actorId);
            return new ZLinkActorRemoteLocation(
                route.Actor.ActorId,
                new ZLinkActorRemoteAddress(
                    routerChannelId,
                    route.Actor.NodeRid,
                    route.Actor.Generation),
                route.CurrentSpotRid,
                route.CurrentSpotKind.ToFramework());
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
            $"Actor remote address was not found for '{actorId}'.",
            isRetriable: true,
            innerException: inner);

}

internal sealed class ZLinkRegistrySpotRemoteAddressResolver(
    ZLinkFrameworkRuntime runtime,
    ZLinkFrameworkRegistration registration) : IZLinkSpotRemoteAddressResolver
{
    private const byte SpotNameRoutePayloadVersion = 1;
    private const string SpotNameRouteTooLarge = "SPOT name route payload is too large.";
    private const string InvalidSpotNameRoutePayload = "Invalid SPOT name route payload.";

    public async ValueTask<ZLinkSpotRemoteAddress> ResolveSpotRemoteAddressAsync(
        string spotName,
        CancellationToken cancellationToken)
    {
        var options = registration.RegistrySpotRemoteAddresses
            ?? throw new ZLinkConfigurationException("Registry SPOT routes are not configured.");
        var state = await runtime.GetStartedStateForRoutingAsync(cancellationToken)
            .ConfigureAwait(false);
        var discovery = ZLinkRegistryRouteRuntime.ResolveSingleSpotDiscovery(state);
        var routeKey = BuildSpotNameRouteKey(options.Namespace, spotName);

        try
        {
            using var nameRoute = discovery.ResolveRoute(DiscoveryRouteKind.SpotName, routeKey);
            var spotRid = DecodeSpotNameRouteValue(
                nameRoute.Value,
                options.Namespace,
                spotName);
            var routerChannelId = ZLinkRegistryRouteRuntime.ResolveRouterChannelId(
                state,
                options.RouterChannelId);
            var spotRoute = discovery.ResolveSpot(spotRid);
            return new ZLinkSpotRemoteAddress(
                routerChannelId,
                spotRoute.OwnerNodeRid,
                spotRoute.SpotRid,
                spotRoute.SpotKind.ToFramework());
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

    public async ValueTask<ZLinkSpotRemoteAddress> ResolveSpotRemoteAddressAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken)
    {
        var options = registration.RegistrySpotRemoteAddresses
            ?? throw new ZLinkConfigurationException("Registry SPOT routes are not configured.");
        var state = await runtime.GetStartedStateForRoutingAsync(cancellationToken)
            .ConfigureAwait(false);
        var routerChannelId = ZLinkRegistryRouteRuntime.ResolveRouterChannelId(
            state,
            options.RouterChannelId);
        var discovery = ZLinkRegistryRouteRuntime.ResolveSingleSpotDiscovery(state);

        try
        {
            var spotRoute = discovery.ResolveSpot(spotRid);
            return new ZLinkSpotRemoteAddress(
                routerChannelId,
                spotRoute.OwnerNodeRid,
                spotRoute.SpotRid,
                spotRoute.SpotKind.ToFramework());
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

        return ZLinkRegistryRoutePayloadCodec.EncodeNamespacedKey(namespaceName, spotName);
    }

    internal static byte[] EncodeSpotNameRouteValue(
        string namespaceName,
        string spotName,
        RoutingId spotRid)
    {
        var identity = ZLinkRegistryRoutePayloadCodec.EncodeIdentity(
            SpotNameRoutePayloadVersion,
            namespaceName,
            spotName,
            SpotNameRouteTooLarge);

        var value = new byte[
            identity.Length
            + ZLinkRegistryRoutePayloadCodec.EncodedRoutingIdLength(spotRid, SpotNameRouteTooLarge)];
        var span = value.AsSpan();
        identity.CopyTo(span);
        var offset = identity.Length;
        ZLinkRegistryRoutePayloadCodec.WriteRoutingId(
            span,
            ref offset,
            spotRid,
            SpotNameRouteTooLarge);
        return value;
    }

    private static RoutingId DecodeSpotNameRouteValue(
        Message value,
        string expectedNamespace,
        string expectedSpotName)
    {
        var bytes = value.AsReadOnlySpan();
        var identity = ZLinkRegistryRoutePayloadCodec.DecodeIdentity(
            bytes,
            SpotNameRoutePayloadVersion,
            InvalidSpotNameRoutePayload);

        if (!identity.Matches(expectedNamespace, expectedSpotName))
        {
            throw new FormatException("SPOT name route payload identity mismatch.");
        }

        var offset = identity.Offset;
        var spotRid = ZLinkRegistryRoutePayloadCodec.ReadRoutingId(
            bytes,
            ref offset,
            InvalidSpotNameRoutePayload);
        ZLinkRegistryRoutePayloadCodec.EnsureFullyRead(
            bytes,
            offset,
            InvalidSpotNameRoutePayload);
        return spotRid;
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

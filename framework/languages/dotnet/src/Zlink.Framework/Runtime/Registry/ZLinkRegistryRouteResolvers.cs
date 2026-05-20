using System.Collections.Concurrent;
namespace Zlink.Framework.Runtime.Registry;

internal sealed class ZLinkRegistryActorRouteResolver(
    ZLinkFrameworkRuntime runtime,
    ZLinkFrameworkRegistration registration) : IZLinkActorPlayRouteResolver
{
    private const byte ActorRoutePayloadVersion = 2;
    private const string ActorRouteTooLarge = "Actor route payload is too large.";
    private const string InvalidActorRoutePayload = "Invalid actor route payload.";

    public async ValueTask<ZLinkActorRoute> ResolvePlayRouteAsync(
        string actorId,
        CancellationToken cancellationToken)
    {
        var options = registration.RegistryActorRoutes
            ?? throw new ZLinkConfigurationException("Registry actor routes are not configured.");
        var state = await runtime.GetStartedStateForRoutingAsync(cancellationToken)
            .ConfigureAwait(false);
        var routerChannelId = ZLinkRegistryRouteRuntime.ResolveRouterChannelId(
            state,
            options.RouterChannelId);
        var discovery = ZLinkRegistryRouteRuntime.ResolveDiscoveryAttachedRouteChannel(
            state,
            routerChannelId,
            "Registry actor route resolver");
        var routeKey = BuildActorRouteKey(options.Namespace, actorId);

        try
        {
            using var route = discovery.ResolveRoute(DiscoveryRouteKind.Actor, routeKey);
            var (targetNodeRid, actorGeneration) = DecodeActorRouteValue(
                route.Value,
                options.Namespace,
                actorId);
            if (actorGeneration == 0)
            {
                throw new FormatException("Invalid actor route generation.");
            }

            return new ZLinkActorRoute(routerChannelId, targetNodeRid, actorGeneration);
        }
        catch (ZlinkConfigException error) when (error.InternalErrno == 2)
        {
            throw NotFound(actorId, error);
        }
        catch (FormatException error)
        {
            throw NotFound(actorId, error);
        }
    }

    internal static async ValueTask PublishActorRouteAsync(
        ZLinkFrameworkRuntime runtime,
        ZLinkFrameworkRegistration registration,
        string actorId,
        CancellationToken cancellationToken)
    {
        var options = registration.RegistryActorRoutes;
        if (options is null)
        {
            return;
        }

        var state = await runtime.GetStartedStateForRoutingAsync(cancellationToken)
            .ConfigureAwait(false);
        var routerChannelId = ZLinkRegistryRouteRuntime.ResolveRouterChannelId(
            state,
            options.RouterChannelId);
        var targetNodeRid = runtime.ResolveSessionRouterId(routerChannelId);
        var actorState = runtime.GetOrCreateActorState(actorId);
        var actorGeneration = actorState.CurrentActorGeneration;
        if (actorGeneration == 0)
        {
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                $"Actor route for '{actorId}' does not have a concrete generation.",
                isRetriable: false);
        }

        var discovery = ZLinkRegistryRouteRuntime.ResolveDiscoveryAttachedRouteChannel(
            state,
            routerChannelId,
            "Registry actor route resolver");
        var key = BuildActorRouteKey(options.Namespace, actorId);
        var value = EncodeActorRouteValue(
            options.Namespace,
            actorId,
            targetNodeRid,
            actorGeneration);

        await ZLinkRegistryRouteRuntime.RetryRouteOperationAsync(
                () => discovery.BindRoute(DiscoveryRouteKind.Actor, key, value),
                $"Actor route publish failed for '{actorId}'.",
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                registration.DefaultTimeout,
                TimeSpan.FromMilliseconds(150),
                cancellationToken)
            .ConfigureAwait(false);
    }

    internal static byte[] BuildActorRouteKey(
        string namespaceName,
        string actorId)
    {
        if (string.IsNullOrWhiteSpace(actorId))
        {
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                "Actor route requires a non-empty actor id.",
                isRetriable: false);
        }

        return EncodeActorRouteIdentity(namespaceName, actorId);
    }

    internal static byte[] EncodeActorRouteValue(
        string namespaceName,
        string actorId,
        RoutingId targetNodeRid,
        ulong actorGeneration)
    {
        var identity = ZLinkRegistryRoutePayloadCodec.EncodeIdentity(
            ActorRoutePayloadVersion,
            namespaceName,
            actorId,
            ActorRouteTooLarge);

        if (actorGeneration == 0)
        {
            throw new ZLinkConfigurationException("Actor route generation must not be zero.");
        }

        var value = new byte[
            identity.Length
            + ZLinkRegistryRoutePayloadCodec.EncodedRoutingIdLength(targetNodeRid, ActorRouteTooLarge)
            + sizeof(ulong)];
        var span = value.AsSpan();
        identity.CopyTo(span);
        var offset = identity.Length;
        ZLinkRegistryRoutePayloadCodec.WriteRoutingId(
            span,
            ref offset,
            targetNodeRid,
            ActorRouteTooLarge);
        ZLinkRegistryRoutePayloadCodec.WriteUInt64(span, ref offset, actorGeneration);
        return value;
    }

    private static (RoutingId TargetNodeRid, ulong ActorGeneration) DecodeActorRouteValue(
        Message value,
        string expectedNamespace,
        string expectedActorId)
    {
        var bytes = value.AsReadOnlySpan();
        var identity = ZLinkRegistryRoutePayloadCodec.DecodeIdentity(
            bytes,
            ActorRoutePayloadVersion,
            InvalidActorRoutePayload);

        if (!identity.Matches(expectedNamespace, expectedActorId))
        {
            throw new FormatException("Actor route payload identity mismatch.");
        }

        var offset = identity.Offset;
        var targetNodeRid = ZLinkRegistryRoutePayloadCodec.ReadRoutingId(
            bytes,
            ref offset,
            InvalidActorRoutePayload);
        var actorGeneration = ZLinkRegistryRoutePayloadCodec.ReadUInt64(
            bytes,
            ref offset,
            InvalidActorRoutePayload);
        ZLinkRegistryRoutePayloadCodec.EnsureFullyRead(
            bytes,
            offset,
            InvalidActorRoutePayload);
        return (targetNodeRid, actorGeneration);
    }

    private static ZLinkFrameworkException NotFound(
        string actorId,
        Exception? inner = null)
        => new(
            ZLinkFrameworkErrorKind.ActorRouteNotFound,
            $"Actor route was not found for '{actorId}'.",
            isRetriable: true,
            innerException: inner);

    private static byte[] EncodeActorRouteIdentity(
        string namespaceName,
        string actorId)
    {
        return ZLinkRegistryRoutePayloadCodec.EncodeIdentity(
            ActorRoutePayloadVersion,
            namespaceName,
            actorId,
            ActorRouteTooLarge);
    }

}

internal sealed class ZLinkRegistrySpotRouteResolver(
    ZLinkFrameworkRuntime runtime,
    ZLinkFrameworkRegistration registration) : IZLinkSpotRouteResolver
{
    private const byte SpotNameRoutePayloadVersion = 1;
    private const string SpotNameRouteTooLarge = "SPOT name route payload is too large.";
    private const string InvalidSpotNameRoutePayload = "Invalid SPOT name route payload.";

    public async ValueTask<ZLinkSpotRoute> ResolveSpotRouteAsync(
        string spotName,
        CancellationToken cancellationToken)
    {
        var options = registration.RegistrySpotRoutes
            ?? throw new ZLinkConfigurationException("Registry SPOT routes are not configured.");
        var state = await runtime.GetStartedStateForRoutingAsync(cancellationToken)
            .ConfigureAwait(false);
        var discovery = ZLinkRegistryRouteRuntime.ResolveSingleSpotDiscovery(state);
        var routeKey = BuildSpotNameRouteKey(options.Namespace, spotName);

        try
        {
            using var route = discovery.ResolveRoute(DiscoveryRouteKind.SpotName, routeKey);
            var spotRid = DecodeSpotNameRouteValue(
                route.Value,
                options.Namespace,
                spotName);
            var routerChannelId = ZLinkRegistryRouteRuntime.ResolveRouterChannelId(
                state,
                options.RouterChannelId);
            return new ZLinkSpotRoute(routerChannelId, route.OwnerRoutingId, spotRid);
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
        var routerChannelId = ZLinkRegistryRouteRuntime.ResolveRouterChannelId(
            state,
            options.RouterChannelId);
        var discovery = ZLinkRegistryRouteRuntime.ResolveSingleSpotDiscovery(state);

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

internal sealed class ZLinkRegistryActorSessionBindingStore(
    ZLinkFrameworkRuntime runtime,
    ZLinkFrameworkRegistration registration) : IZLinkActorSessionBindingStore
{
    private const byte ActorSessionRoutePayloadVersion = 1;
    private const string ActorSessionRouteTooLarge = "Actor-session route payload is too large.";
    private const string InvalidActorSessionRoutePayload = "Invalid actor-session route payload.";
    private readonly ConcurrentDictionary<string, SemaphoreSlim> _actorGates = new(StringComparer.Ordinal);

    public async ValueTask BindSessionAsync(
        ZLinkActorSessionBinding binding,
        CancellationToken cancellationToken)
    {
        var options = registration.RegistryActorSessionBindings
            ?? throw new ZLinkConfigurationException("Registry actor-session bindings are not configured.");
        var gate = GateFor(binding.ActorId);
        await gate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            var state = await runtime.GetStartedStateForRoutingAsync(cancellationToken)
                .ConfigureAwait(false);
            var discovery = ResolveRouteChannelDiscovery(state);
            var key = BuildActorSessionRouteKey(options.Namespace, binding.ActorId);
            var value = EncodeActorSessionRouteValue(
                options.Namespace,
                binding.ActorId,
                binding.SessionRouterId,
                binding.BindingToken);
            discovery.BindRoute(DiscoveryRouteKind.ActorSession, key, value);
        }
        finally
        {
            gate.Release();
        }
    }

    public async ValueTask UnbindSessionAsync(
        ZLinkActorSessionUnbind binding,
        CancellationToken cancellationToken)
    {
        var options = registration.RegistryActorSessionBindings
            ?? throw new ZLinkConfigurationException("Registry actor-session bindings are not configured.");
        var gate = GateFor(binding.ActorId);
        await gate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            var state = await runtime.GetStartedStateForRoutingAsync(cancellationToken)
                .ConfigureAwait(false);
            var discovery = ResolveRouteChannelDiscovery(state);
            var key = BuildActorSessionRouteKey(options.Namespace, binding.ActorId);

            try
            {
                using var route = discovery.ResolveRoute(DiscoveryRouteKind.ActorSession, key);
                var current = DecodeActorSessionRouteValue(
                    route.Value,
                    options.Namespace,
                    binding.ActorId);
                if (!string.Equals(current.BindingToken, binding.BindingToken, StringComparison.Ordinal))
                {
                    return;
                }

                discovery.UnbindRoute(DiscoveryRouteKind.ActorSession, key);
            }
            catch (ZlinkConfigException error) when (error.InternalErrno == 2)
            {
            }
            catch (FormatException)
            {
            }
        }
        finally
        {
            gate.Release();
        }
    }

    public async ValueTask<ZLinkActorSessionRoute> FindSessionAsync(
        string actorId,
        CancellationToken cancellationToken)
    {
        var options = registration.RegistryActorSessionBindings
            ?? throw new ZLinkConfigurationException("Registry actor-session bindings are not configured.");
        var state = await runtime.GetStartedStateForRoutingAsync(cancellationToken)
            .ConfigureAwait(false);
        var discovery = ResolveRouteChannelDiscovery(state);
        var key = BuildActorSessionRouteKey(options.Namespace, actorId);

        try
        {
            using var route = discovery.ResolveRoute(DiscoveryRouteKind.ActorSession, key);
            return DecodeActorSessionRouteValue(
                route.Value,
                options.Namespace,
                actorId);
        }
        catch (ZlinkConfigException error) when (error.InternalErrno == 2)
        {
            throw NotFound(actorId, error);
        }
        catch (FormatException error)
        {
            throw NotFound(actorId, error);
        }
    }

    internal static byte[] BuildActorSessionRouteKey(string namespaceName, string actorId)
    {
        if (string.IsNullOrWhiteSpace(actorId))
        {
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.SessionRouteNotFound,
                "Actor-session route requires a non-empty actor id.",
                isRetriable: false);
        }

        return ZLinkRegistryRoutePayloadCodec.EncodeNamespacedKey(namespaceName, actorId);
    }

    internal static byte[] EncodeActorSessionRouteValue(
        string namespaceName,
        string actorId,
        RoutingId sessionRouterId,
        string bindingToken)
    {
        var identity = ZLinkRegistryRoutePayloadCodec.EncodeIdentity(
            ActorSessionRoutePayloadVersion,
            namespaceName,
            actorId,
            ActorSessionRouteTooLarge);

        var value = new byte[
            identity.Length
            + ZLinkRegistryRoutePayloadCodec.EncodedRoutingIdLength(sessionRouterId, ActorSessionRouteTooLarge)
            + ZLinkRegistryRoutePayloadCodec.EncodedStringLength(bindingToken, ActorSessionRouteTooLarge)];
        var span = value.AsSpan();
        identity.CopyTo(span);
        var offset = identity.Length;
        ZLinkRegistryRoutePayloadCodec.WriteRoutingId(
            span,
            ref offset,
            sessionRouterId,
            ActorSessionRouteTooLarge);
        ZLinkRegistryRoutePayloadCodec.WriteString(
            span,
            ref offset,
            bindingToken,
            ActorSessionRouteTooLarge);
        return value;
    }

    private static ZLinkActorSessionRoute DecodeActorSessionRouteValue(
        Message value,
        string expectedNamespace,
        string expectedActorId)
    {
        var bytes = value.AsReadOnlySpan();
        var identity = ZLinkRegistryRoutePayloadCodec.DecodeIdentity(
            bytes,
            ActorSessionRoutePayloadVersion,
            InvalidActorSessionRoutePayload);

        if (!identity.Matches(expectedNamespace, expectedActorId))
        {
            throw new FormatException("Actor-session route payload identity mismatch.");
        }

        var offset = identity.Offset;
        var sessionRouterId = ZLinkRegistryRoutePayloadCodec.ReadRoutingId(
            bytes,
            ref offset,
            InvalidActorSessionRoutePayload);
        var bindingToken = ZLinkRegistryRoutePayloadCodec.ReadString(
            bytes,
            ref offset,
            InvalidActorSessionRoutePayload);
        ZLinkRegistryRoutePayloadCodec.EnsureFullyRead(
            bytes,
            offset,
            InvalidActorSessionRoutePayload);
        return new ZLinkActorSessionRoute(sessionRouterId, bindingToken);
    }

    private static IZLinkBackendDiscovery ResolveRouteChannelDiscovery(
        ZLinkFrameworkRuntimeState state)
    {
        if (state.RouteChannels.Count != 1)
        {
            throw new ZLinkConfigurationException(
                "Registry actor-session binding store requires exactly one route mesh channel.");
        }

        var discovery = state.RouteChannels.Values.Single().Discovery;
        return discovery
            ?? throw new ZLinkConfigurationException(
                "Registry actor-session binding store requires a discovery-attached route mesh channel.");
    }

    private SemaphoreSlim GateFor(string actorId)
        => _actorGates.GetOrAdd(actorId, static _ => new SemaphoreSlim(1, 1));

    private static ZLinkFrameworkException NotFound(
        string actorId,
        Exception? inner = null)
        => new(
            ZLinkFrameworkErrorKind.SessionRouteNotFound,
            $"Session route for actor '{actorId}' was not found.",
            isRetriable: true,
            innerException: inner);
}

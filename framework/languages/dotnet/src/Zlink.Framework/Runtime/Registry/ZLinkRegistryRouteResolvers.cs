using System.Buffers.Binary;
using System.Collections.Concurrent;
using System.Text;

namespace Zlink.Framework.Runtime.Registry;

internal sealed class ZLinkRegistryActorRouteResolver(
    ZLinkFrameworkRuntime runtime,
    ZLinkFrameworkRegistration registration) : IZLinkActorPlayRouteResolver
{
    private const byte ActorRoutePayloadVersion = 2;

    public async ValueTask<ZLinkActorRoute> ResolvePlayRouteAsync(
        string actorId,
        CancellationToken cancellationToken)
    {
        var options = registration.RegistryActorRoutes
            ?? throw new ZLinkConfigurationException("Registry actor routes are not configured.");
        var state = await runtime.GetStartedStateForRoutingAsync(cancellationToken)
            .ConfigureAwait(false);
        var routerChannelId = ResolveRouterChannelId(state, options.RouterChannelId);
        var discovery = ResolveRouteChannelDiscovery(state, routerChannelId);
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
        var routerChannelId = ResolveRouterChannelId(state, options.RouterChannelId);
        var targetNodeRid = runtime.ResolveSessionRouterId(routerChannelId);
        var actorState = runtime.GetOrCreateActorState(actorId);
        var actorGeneration = actorState.NativeActorRef?.Generation ?? actorState.ActorGeneration;
        if (actorGeneration == 0)
        {
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                $"Actor route for '{actorId}' does not have a concrete generation.",
                isRetriable: false);
        }

        var discovery = ResolveRouteChannelDiscovery(state, routerChannelId);
        var key = BuildActorRouteKey(options.Namespace, actorId);
        var value = EncodeActorRouteValue(
            options.Namespace,
            actorId,
            targetNodeRid,
            actorGeneration);

        await RetryRouteOperationAsync(
                () => discovery.BindRoute(DiscoveryRouteKind.Actor, key, value),
                $"Actor route publish failed for '{actorId}'.",
                registration.DefaultTimeout,
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
        var identity = EncodeActorRouteIdentity(namespaceName, actorId);
        var targetNodeRidBytes = targetNodeRid.ToBytes();
        if (targetNodeRidBytes.Length > byte.MaxValue)
        {
            throw new ZLinkConfigurationException("Actor route payload is too large.");
        }

        if (actorGeneration == 0)
        {
            throw new ZLinkConfigurationException("Actor route generation must not be zero.");
        }

        var value = new byte[identity.Length + 1 + targetNodeRidBytes.Length + sizeof(ulong)];
        var span = value.AsSpan();
        identity.CopyTo(span);
        var offset = identity.Length;
        span[offset++] = (byte)targetNodeRidBytes.Length;
        targetNodeRidBytes.CopyTo(span[offset..]);
        offset += targetNodeRidBytes.Length;
        BinaryPrimitives.WriteUInt64BigEndian(
            span.Slice(offset, sizeof(ulong)),
            actorGeneration);
        return value;
    }

    private static (RoutingId TargetNodeRid, ulong ActorGeneration) DecodeActorRouteValue(
        Message value,
        string expectedNamespace,
        string expectedActorId)
    {
        var bytes = value.AsReadOnlySpan();
        if (bytes.Length < 6 || bytes[0] != ActorRoutePayloadVersion)
        {
            throw new FormatException("Invalid actor route payload.");
        }

        var offset = 1;
        var namespaceLength = ReadUInt16(bytes, ref offset);
        EnsureAvailable(bytes, offset, namespaceLength + sizeof(ushort));
        var namespaceName = Encoding.UTF8.GetString(
            bytes.Slice(offset, namespaceLength));
        offset += namespaceLength;

        var actorIdLength = ReadUInt16(bytes, ref offset);
        EnsureAvailable(bytes, offset, actorIdLength + 1);
        var actorId = Encoding.UTF8.GetString(bytes.Slice(offset, actorIdLength));
        offset += actorIdLength;

        var targetNodeRidLength = bytes[offset++];
        if (bytes.Length != offset + targetNodeRidLength + sizeof(ulong))
        {
            throw new FormatException("Invalid actor route payload.");
        }

        if (!string.Equals(namespaceName, expectedNamespace, StringComparison.Ordinal)
            || !string.Equals(actorId, expectedActorId, StringComparison.Ordinal))
        {
            throw new FormatException("Actor route payload identity mismatch.");
        }

        var targetNodeRid = RoutingId.FromBytes(bytes.Slice(offset, targetNodeRidLength));
        offset += targetNodeRidLength;
        var actorGeneration = BinaryPrimitives.ReadUInt64BigEndian(
            bytes.Slice(offset, sizeof(ulong)));
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

    private static IZLinkBackendDiscovery ResolveRouteChannelDiscovery(
        ZLinkFrameworkRuntimeState state,
        string routerChannelId)
    {
        if (!state.RouteChannels.TryGetValue(routerChannelId, out var routeChannel))
        {
            throw new ZLinkConfigurationException(
                $"Route mesh channel '{routerChannelId}' is not registered.");
        }

        return routeChannel.Discovery
            ?? throw new ZLinkConfigurationException(
                $"Registry actor route resolver requires discovery-attached route mesh channel '{routerChannelId}'.");
    }

    private static async ValueTask RetryRouteOperationAsync(
        Action operation,
        string errorMessage,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        using var timeoutSource = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeoutSource.CancelAfter(timeout);
        while (true)
        {
            timeoutSource.Token.ThrowIfCancellationRequested();
            try
            {
                operation();
                return;
            }
            catch (ZlinkConfigException error)
                when (error.InternalErrno is 2 or 11)
            {
                if (timeoutSource.IsCancellationRequested)
                {
                    throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.ActorRouteNotFound,
                        errorMessage,
                        isRetriable: true,
                        innerException: error);
                }

                await Task.Delay(TimeSpan.FromMilliseconds(150), timeoutSource.Token)
                    .ConfigureAwait(false);
            }
            catch (ZlinkConfigException error)
            {
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.ActorRouteNotFound,
                    errorMessage,
                    isRetriable: true,
                    innerException: error);
            }
        }
    }

    private static byte[] EncodeActorRouteIdentity(
        string namespaceName,
        string actorId)
    {
        var namespaceBytes = Encoding.UTF8.GetBytes(namespaceName);
        var actorIdBytes = Encoding.UTF8.GetBytes(actorId);
        if (namespaceBytes.Length > ushort.MaxValue
            || actorIdBytes.Length > ushort.MaxValue)
        {
            throw new ZLinkConfigurationException("Actor route payload is too large.");
        }

        var value = new byte[
            1
            + sizeof(ushort) + namespaceBytes.Length
            + sizeof(ushort) + actorIdBytes.Length];
        var span = value.AsSpan();
        span[0] = ActorRoutePayloadVersion;
        var offset = 1;
        BinaryPrimitives.WriteUInt16BigEndian(
            span.Slice(offset, sizeof(ushort)),
            (ushort)namespaceBytes.Length);
        offset += sizeof(ushort);
        namespaceBytes.CopyTo(span[offset..]);
        offset += namespaceBytes.Length;
        BinaryPrimitives.WriteUInt16BigEndian(
            span.Slice(offset, sizeof(ushort)),
            (ushort)actorIdBytes.Length);
        offset += sizeof(ushort);
        actorIdBytes.CopyTo(span[offset..]);
        return value;
    }

    private static ushort ReadUInt16(ReadOnlySpan<byte> bytes, ref int offset)
    {
        EnsureAvailable(bytes, offset, sizeof(ushort));
        var value = BinaryPrimitives.ReadUInt16BigEndian(
            bytes.Slice(offset, sizeof(ushort)));
        offset += sizeof(ushort);
        return value;
    }

    private static void EnsureAvailable(ReadOnlySpan<byte> bytes, int offset, int length)
    {
        if (offset < 0 || length < 0 || bytes.Length < offset + length)
        {
            throw new FormatException("Invalid actor route payload.");
        }
    }

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
        var spotNodeDiscoveries = state.SpotNodes.Values
            .Select(static node => node.SpotDiscovery)
            .OfType<IZLinkBackendDiscovery>()
            .Distinct()
            .ToArray();
        if (spotNodeDiscoveries.Length == 1)
        {
            return spotNodeDiscoveries[0];
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
            var routerChannelId = ZLinkRegistryActorRouteResolver.ResolveRouterChannelId(
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

internal sealed class ZLinkRegistryActorSessionBindingStore(
    ZLinkFrameworkRuntime runtime,
    ZLinkFrameworkRegistration registration) : IZLinkActorSessionBindingStore
{
    private const byte ActorSessionRoutePayloadVersion = 1;
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

        return Encoding.UTF8.GetBytes($"{namespaceName}\0{actorId}");
    }

    internal static byte[] EncodeActorSessionRouteValue(
        string namespaceName,
        string actorId,
        RoutingId sessionRouterId,
        string bindingToken)
    {
        var namespaceBytes = Encoding.UTF8.GetBytes(namespaceName);
        var actorIdBytes = Encoding.UTF8.GetBytes(actorId);
        var routerRidBytes = sessionRouterId.ToBytes();
        var tokenBytes = Encoding.UTF8.GetBytes(bindingToken);
        if (namespaceBytes.Length > ushort.MaxValue
            || actorIdBytes.Length > ushort.MaxValue
            || routerRidBytes.Length > byte.MaxValue
            || tokenBytes.Length > ushort.MaxValue)
        {
            throw new ZLinkConfigurationException("Actor-session route payload is too large.");
        }

        var value = new byte[
            1
            + sizeof(ushort) + namespaceBytes.Length
            + sizeof(ushort) + actorIdBytes.Length
            + 1 + routerRidBytes.Length
            + sizeof(ushort) + tokenBytes.Length];
        var span = value.AsSpan();
        span[0] = ActorSessionRoutePayloadVersion;
        var offset = 1;
        BinaryPrimitives.WriteUInt16BigEndian(
            span.Slice(offset, sizeof(ushort)),
            (ushort)namespaceBytes.Length);
        offset += sizeof(ushort);
        namespaceBytes.CopyTo(span[offset..]);
        offset += namespaceBytes.Length;
        BinaryPrimitives.WriteUInt16BigEndian(
            span.Slice(offset, sizeof(ushort)),
            (ushort)actorIdBytes.Length);
        offset += sizeof(ushort);
        actorIdBytes.CopyTo(span[offset..]);
        offset += actorIdBytes.Length;
        span[offset++] = (byte)routerRidBytes.Length;
        routerRidBytes.CopyTo(span[offset..]);
        offset += routerRidBytes.Length;
        BinaryPrimitives.WriteUInt16BigEndian(
            span.Slice(offset, sizeof(ushort)),
            (ushort)tokenBytes.Length);
        offset += sizeof(ushort);
        tokenBytes.CopyTo(span[offset..]);
        return value;
    }

    private static ZLinkActorSessionRoute DecodeActorSessionRouteValue(
        Message value,
        string expectedNamespace,
        string expectedActorId)
    {
        var bytes = value.AsReadOnlySpan();
        if (bytes.Length < 8 || bytes[0] != ActorSessionRoutePayloadVersion)
        {
            throw new FormatException("Invalid actor-session route payload.");
        }

        var offset = 1;
        var namespaceLength = ReadUInt16(bytes, ref offset);
        EnsureAvailable(bytes, offset, namespaceLength + sizeof(ushort));
        var namespaceName = Encoding.UTF8.GetString(bytes.Slice(offset, namespaceLength));
        offset += namespaceLength;

        var actorIdLength = ReadUInt16(bytes, ref offset);
        EnsureAvailable(bytes, offset, actorIdLength + 1);
        var actorId = Encoding.UTF8.GetString(bytes.Slice(offset, actorIdLength));
        offset += actorIdLength;

        var routerRidLength = bytes[offset++];
        EnsureAvailable(bytes, offset, routerRidLength + sizeof(ushort));
        var sessionRouterId = RoutingId.FromBytes(bytes.Slice(offset, routerRidLength));
        offset += routerRidLength;

        var tokenLength = ReadUInt16(bytes, ref offset);
        EnsureAvailable(bytes, offset, tokenLength);
        if (bytes.Length != offset + tokenLength)
        {
            throw new FormatException("Invalid actor-session route payload.");
        }

        var bindingToken = Encoding.UTF8.GetString(bytes.Slice(offset, tokenLength));
        if (!string.Equals(namespaceName, expectedNamespace, StringComparison.Ordinal)
            || !string.Equals(actorId, expectedActorId, StringComparison.Ordinal))
        {
            throw new FormatException("Actor-session route payload identity mismatch.");
        }

        return new ZLinkActorSessionRoute(sessionRouterId, bindingToken);
    }

    private static ushort ReadUInt16(ReadOnlySpan<byte> bytes, ref int offset)
    {
        EnsureAvailable(bytes, offset, sizeof(ushort));
        var value = BinaryPrimitives.ReadUInt16BigEndian(
            bytes.Slice(offset, sizeof(ushort)));
        offset += sizeof(ushort);
        return value;
    }

    private static void EnsureAvailable(ReadOnlySpan<byte> bytes, int offset, int length)
    {
        if (offset < 0 || length < 0 || bytes.Length < offset + length)
        {
            throw new FormatException("Invalid actor-session route payload.");
        }
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

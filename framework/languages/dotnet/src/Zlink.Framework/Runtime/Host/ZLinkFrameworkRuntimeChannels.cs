namespace Zlink.Framework.Runtime.Host;

internal sealed partial class ZLinkFrameworkRuntime
{
    internal ZLinkChannelRuntimeBundle GetPublisherBundle(string channelName)
    {
        return _channels.GetPublisherBundle(GetOrStartState(), channelName);
    }

    internal ZLinkChannelRuntimeBundle GetClientServerClientBundle(string channelName)
    {
        return _channels.GetClientServerClientBundle(GetOrStartState(), channelName);
    }

    internal ZLinkClientServerClientRuntime GetClientServerClientRuntime(
        string channelName) =>
        _channels.GetClientServerClientRuntime(
            GetOrStartState(),
            channelName);

    internal async ValueTask<ZLinkSubmitResult> SendToChannelAsync(
        string channelName,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken,
        ReadOnlyMemory<byte> metadata = default)
    {
        if (Registration.Channels.TryGetValue(channelName, out var channel)
            && channel.HasClientServerClient)
        {
            if (!metadata.IsEmpty)
                throw ZLinkClassicCallSupport.MetadataNotSupported();
            return await GetClientServerClientRuntime(channelName)
                .SendAsync(parts, cancellationToken)
                .ConfigureAwait(false);
        }

        var meshName = ResolveRouteMeshForChannel(channelName);
        return await GetMeshNodeRuntime(meshName).EntryOutbound
            .SendToChannelAsync(channelName, parts, cancellationToken, metadata)
            .ConfigureAwait(false);
    }

    internal async ValueTask<IReadOnlyList<Message>> RequestToChannelAsync(
        string channelName,
        IReadOnlyList<Message> parts,
        TimeSpan timeout,
        CancellationToken cancellationToken,
        ReadOnlyMemory<byte> metadata = default)
    {
        if (Registration.Channels.TryGetValue(channelName, out var channel)
            && channel.HasClientServerClient)
        {
            if (!metadata.IsEmpty)
                throw ZLinkClassicCallSupport.MetadataNotSupported();
            return await GetClientServerClientRuntime(channelName)
                .RequestAsync(
                    parts,
                    timeout,
                    cancellationToken)
                .ConfigureAwait(false);
        }

        var meshName = ResolveRouteMeshForChannel(channelName);
        return await GetMeshNodeRuntime(meshName).EntryOutbound
            .RequestToChannelAsync(channelName, parts, timeout, cancellationToken, metadata)
            .ConfigureAwait(false);
    }

    private string ResolveRouteMeshForChannel(string channelName)
    {
        var matches = Registration.SpotNodes.Values
            .Where(node => node.ChannelMemberships.Any(
                membership => StringComparer.Ordinal.Equals(
                    membership.ChannelName,
                    channelName)))
            .Select(node => node.SpotNodeName)
            .Distinct(StringComparer.Ordinal)
            .ToArray();
        return matches.Length switch
        {
            1 => matches[0],
            0 => throw new ZLinkConfigurationException(
                $"No process-local RouteMesh or ClientServer client is registered for ChannelName '{channelName}'."),
            _ => throw new ZLinkConfigurationException(
                $"ChannelName '{channelName}' resolves to more than one process-local RouteMesh.")
        };
    }

    /// <summary>
    /// Classifies the target from the auto-connect reconciler's desired-set
    /// snapshot — never from the store, so the send path stays free of
    /// hidden store I/O. True: a known route mesh peer, the route socket is
    /// the delivery path. False: the mesh does not know this rid (a spot
    /// rid or a stale node) — the egress owns it. Null: no loop manages
    /// this mesh yet, keep the default ordering.
    /// </summary>
    private bool? IsKnownRouteMeshPeer(string routerChannelId, RoutingId targetNodeRid)
    {
        return _topologyQuery?.IsKnownRouteMeshPeer(routerChannelId, targetNodeRid);
    }

    internal void EnsureKnownRouteMeshPeer(
        string routerChannelId,
        RoutingId targetNodeRid,
        string targetDescription)
    {
        var nodeRuntime = GetMeshNodeRuntime(routerChannelId);
        if (nodeRuntime.Node.RoutingId == targetNodeRid) return;

        if (nodeRuntime.UsesManualRouterAcquisition)
        {
            if (nodeRuntime.TryClassifyManualRouterTarget(targetNodeRid, out var connected))
            {
                if (connected) return;
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.RouteNotConnected,
                    $"Route channel '{routerChannelId}' is not connected to node '{targetNodeRid}' for {targetDescription}.",
                    isRetriable: true);
            }

            throw CreateUnknownRouteTargetException(
                routerChannelId, targetNodeRid, targetDescription);
        }

        if (IsKnownRouteMeshPeer(routerChannelId, targetNodeRid) != false) return;

        throw CreateUnknownRouteTargetException(routerChannelId, targetNodeRid, targetDescription);
    }

    internal async ValueTask<ZLinkSubmitResult> SendToSpotViaRouterChannelAsync(
        string routerChannelId,
        RoutingId targetNodeRid,
        string targetSpotId,
        ulong targetSpotGeneration,
        ulong authorityOwnerGeneration,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken,
        ReadOnlyMemory<byte> metadata = default)
    {
        using var operation = EnterOperation();
        var handedOff = false;
        try
        {
            EnsureKnownRouteMeshPeer(routerChannelId, targetNodeRid, $"SPOT '{targetSpotId}'");

            var accepted = _spotRouteRouter.SendAsync(
                routerChannelId,
                targetNodeRid,
                targetSpotId,
                targetSpotGeneration,
                authorityOwnerGeneration,
                parts,
                cancellationToken,
                metadata);
            handedOff = true;
            return await accepted.ConfigureAwait(false);
        }
        catch
        {
            if (!handedOff) ZLinkMessageParts.DisposeAll(parts);
            throw;
        }
    }

    internal RoutingId ResolveAcceptedSpotRouteNodeRid(string targetSpotNodeChannelName)
    {
        return _spotRouteRouter.ResolveAcceptedSpotRouteNodeRid(targetSpotNodeChannelName);
    }

    /// <summary>Performs the first non-blocking spot-send admission attempt
    /// after the route-mesh peer check.</summary>
    internal bool TrySendToSpotViaRouterChannelOnce(
        string routerChannelId,
        RoutingId targetNodeRid,
        string targetSpotId,
        ulong targetSpotGeneration,
        ulong authorityOwnerGeneration,
        IReadOnlyList<Message> parts,
        ReadOnlyMemory<byte> metadata = default)
    {
        using var operation = EnterOperation();
        EnsureKnownRouteMeshPeer(routerChannelId, targetNodeRid, $"SPOT '{targetSpotId}'");
        return _spotRouteRouter.TrySendOnce(
            routerChannelId,
            targetNodeRid,
            targetSpotId,
            targetSpotGeneration,
            authorityOwnerGeneration,
            parts,
            metadata);
    }

    internal async ValueTask<IReadOnlyList<Message>> RequestToSpotViaRouterChannelAsync(
        string routerChannelId,
        RoutingId targetNodeRid,
        string targetSpotId,
        ulong targetSpotGeneration,
        ulong authorityOwnerGeneration,
        IReadOnlyList<Message> parts,
        TimeSpan timeout,
        CancellationToken cancellationToken,
        ReadOnlyMemory<byte> metadata = default)
    {
        try
        {
            using var operation = EnterOperation(countAsRequest: true);
            var metricStarted = ZLinkRuntimeMetrics.StartChannelRequest();
            var timedOut = false;
            try
            {
                EnsureKnownRouteMeshPeer(routerChannelId, targetNodeRid, $"SPOT '{targetSpotId}'");

                return await _spotRouteRouter.RequestAsync(
                        routerChannelId,
                        targetNodeRid,
                        targetSpotId,
                        targetSpotGeneration,
                        authorityOwnerGeneration,
                        parts,
                        timeout,
                        cancellationToken,
                        metadata)
                    .ConfigureAwait(false);
            }
            catch (TimeoutException)
            {
                timedOut = true;
                throw;
            }
            finally
            {
                ZLinkRuntimeMetrics.CompleteChannelRequest(metricStarted, timedOut);
            }
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(parts);
        }
    }

    private static ZLinkFrameworkException CreateUnknownRouteTargetException(
        string routerChannelId,
        RoutingId targetNodeRid,
        string targetDescription,
        Exception? innerException = null)
    {
        return new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.RequestTargetNotFound,
            $"Route channel '{routerChannelId}' does not know node '{targetNodeRid}' for {targetDescription}.",
            innerException: innerException);
    }

    internal IZLinkBackendSocket GetMonitoringSocket(string sourceName)
    {
        return _channels.GetMonitoringSocket(GetOrStartState(), sourceName);
    }
}

namespace Zlink.Framework.Runtime.Registry;

internal static class ZLinkRegistryRouteRuntime
{
    private const int RouteNotFoundErrno = 2;
    private const int RouteRetryErrno = 11;

    public static string ResolveRouterChannelId(
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

    public static IZLinkBackendDiscovery ResolveDiscoveryAttachedRouteChannel(
        ZLinkFrameworkRuntimeState state,
        string routerChannelId,
        string capabilityName)
    {
        if (!state.RouteChannels.TryGetValue(routerChannelId, out var routeChannel))
        {
            throw new ZLinkConfigurationException(
                $"Route mesh channel '{routerChannelId}' is not registered.");
        }

        return routeChannel.Discovery
            ?? throw new ZLinkConfigurationException(
                $"{capabilityName} requires discovery-attached route mesh channel '{routerChannelId}'.");
    }

    public static IZLinkBackendDiscovery ResolveSingleSpotDiscovery(
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

    public static async ValueTask RetryRouteOperationAsync(
        Action operation,
        string errorMessage,
        ZLinkFrameworkErrorKind errorKind,
        TimeSpan timeout,
        TimeSpan retryDelay,
        CancellationToken cancellationToken,
        bool ignoreNotFound = false)
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
                when (ignoreNotFound && error.InternalErrno == RouteNotFoundErrno)
            {
                return;
            }
            catch (ZlinkConfigException error)
                when (error.InternalErrno is RouteNotFoundErrno or RouteRetryErrno)
            {
                if (timeoutSource.IsCancellationRequested)
                {
                    throw RouteOperationFailed(errorKind, errorMessage, error);
                }

                try
                {
                    await Task.Delay(retryDelay, timeoutSource.Token)
                        .ConfigureAwait(false);
                }
                catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested)
                {
                    throw RouteOperationFailed(errorKind, errorMessage, error);
                }
            }
            catch (ZlinkConfigException error)
            {
                throw RouteOperationFailed(errorKind, errorMessage, error);
            }
        }
    }

    private static ZLinkFrameworkException RouteOperationFailed(
        ZLinkFrameworkErrorKind errorKind,
        string errorMessage,
        Exception innerException)
    {
        return new ZLinkFrameworkException(
            errorKind,
            errorMessage,
            isRetriable: true,
            innerException: innerException);
    }
}

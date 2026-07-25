namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkStreamRuntimeManager(
    IServiceProvider services,
    IZLinkBackendAdapterFactory backendAdapterFactory,
    ZLinkFrameworkRegistration registration)
{
    public async ValueTask InitializeStreamNodesAsync(ZLinkFrameworkComponentState state)
    {
        if (registration.StreamNodes.Count == 0) return;

        var streamAdapter = backendAdapterFactory.CreateStreamAdapter();
        var monitoringAdapter = backendAdapterFactory.CreateMonitoringAdapter();

        foreach (var streamNodeRegistration in registration.StreamNodes.Values)
        {
            // Actor dispatch binds STREAM sessions on the MeshNode pinned by
            // EnableActorDispatch(meshName) (spec 31 §2, gap §12.28). The MeshName is
            // never inferred from "the sole spot node"; a STREAM node without actor
            // dispatch threads no node and the adapter mints a standalone node, while
            // a MeshName with no matching local MeshNode is a startup config error.
            var actorDispatchNode = ResolveActorDispatchNode(state, streamNodeRegistration);

            IZLinkBackendStreamSocket? socket = null;
            IZLinkBackendSocketMonitor? monitor = null;
            ZLinkStreamNodeRuntime? runtime = null;
            try
            {
                // The standalone fallback node (minted only when no shared MeshNode
                // exists) is named by the stream's actor-dispatch MeshName, else the
                // stream node name — Core requires a non-empty mesh name.
                var standaloneMeshName = streamNodeRegistration.ActorDispatchMeshName
                    ?? streamNodeRegistration.StreamNodeName;
                socket = streamAdapter.CreateStreamSocket(
                    state.Context, standaloneMeshName, actorDispatchNode);
                if (streamNodeRegistration.TlsServer is { } tlsServer)
                    socket.SetTlsServer(tlsServer.CertPath, tlsServer.KeyPath, tlsServer.RequireClientCert);

                socket.Bind(ZLinkNetworkEndpointResolver.Bind(
                    streamNodeRegistration.BindEndpoint,
                    streamNodeRegistration.ListenPort,
                    streamNodeRegistration.BindHost,
                    registration.NetworkOptions));
                monitor = monitoringAdapter.OpenSocketMonitor(socket);

                runtime = new ZLinkStreamNodeRuntime(
                    streamNodeRegistration.StreamNodeName,
                    services,
                    socket,
                    monitor,
                    streamNodeRegistration.HeaderSessionType,
                    state.TaskRunner,
                    streamNodeRegistration.TlsServer is null ? "tcp" : "tls",
                    actorDispatchMeshName: streamNodeRegistration.ActorDispatchMeshName);
                state.StreamNodes.Add(streamNodeRegistration.StreamNodeName, runtime);
                runtime.Start();
            }
            catch (Exception initializationFailure)
            {
                state.StreamNodes.Remove(streamNodeRegistration.StreamNodeName);
                var failures = new ZLinkFailureCollector(initializationFailure);
                if (runtime is not null)
                {
                    await failures.CaptureAsync(runtime.DisposeAsync).ConfigureAwait(false);
                }
                else
                {
                    if (monitor is not null)
                        await failures.CaptureAsync(monitor.DisposeAsync).ConfigureAwait(false);

                    if (socket is not null)
                        await failures.CaptureAsync(socket.DisposeAsync).ConfigureAwait(false);
                }

                failures.ThrowIfAny();
                throw new InvalidOperationException("Unreachable after startup cleanup failure propagation.");
            }
        }
    }

    private static IZLinkBackendSpotNode? ResolveActorDispatchNode(
        ZLinkFrameworkComponentState state,
        ZLinkStreamNodeRegistration streamNodeRegistration)
    {
        if (streamNodeRegistration.ActorDispatchMeshName is not { } dispatchMeshName)
            return null;

        if (!state.SpotNodes.TryGetValue(dispatchMeshName, out var dispatchNodeRuntime))
            throw new ZLinkConfigurationException(
                $"STREAM node '{streamNodeRegistration.StreamNodeName}' enabled actor dispatch for "
                + $"MeshName '{dispatchMeshName}', but no RouteMesh with that name is registered on this node.");

        return dispatchNodeRuntime.Node;
    }
}

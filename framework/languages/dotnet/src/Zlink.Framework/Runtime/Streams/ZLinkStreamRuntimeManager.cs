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
            IZLinkBackendStreamSocket? socket = null;
            IZLinkBackendSocketMonitor? monitor = null;
            ZLinkStreamNodeRuntime? runtime = null;
            try
            {
                socket = streamAdapter.CreateStreamSocket(
                    state.Context,
                    streamNodeRegistration.StreamNodeName,
                    actorDispatchNode: null);
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
                    actorDispatchEnabled: streamNodeRegistration.ActorDispatchEnabled);
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

}

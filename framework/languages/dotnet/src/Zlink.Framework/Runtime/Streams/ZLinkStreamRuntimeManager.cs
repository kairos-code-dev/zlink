namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkStreamRuntimeManager(
    IServiceProvider services,
    IZLinkBackendAdapterFactory backendAdapterFactory,
    ZLinkFrameworkRegistration registration)
{
    public async ValueTask InitializeStreamNodesAsync(ZLinkFrameworkRuntimeState state)
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
                socket = streamAdapter.CreateStreamSocket(state.Context);
                if (streamNodeRegistration.TlsServer is { } tlsServer)
                    socket.SetTlsServer(tlsServer.CertPath, tlsServer.KeyPath, tlsServer.RequireClientCert);

                socket.Bind(streamNodeRegistration.BindEndpoint!);
                monitor = monitoringAdapter.OpenSocketMonitor(socket);

                runtime = new ZLinkStreamNodeRuntime(
                    streamNodeRegistration.StreamNodeName,
                    services,
                    socket,
                    monitor,
                    streamNodeRegistration.HeaderSessionType,
                    state.TaskRunner);
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

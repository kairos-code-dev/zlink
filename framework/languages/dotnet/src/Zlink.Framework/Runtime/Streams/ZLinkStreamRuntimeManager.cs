namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkStreamRuntimeManager(
    IServiceProvider services,
    IZLinkBackendAdapterFactory backendAdapterFactory,
    ZLinkFrameworkRegistration registration)
{
    public void InitializeStreamNodes(ZLinkFrameworkRuntimeState state)
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
            catch
            {
                state.StreamNodes.Remove(streamNodeRegistration.StreamNodeName);
                if (runtime is not null)
                {
                    runtime.DisposeAsync().AsTask().GetAwaiter().GetResult();
                }
                else
                {
                    if (monitor is not null) monitor.DisposeAsync().AsTask().GetAwaiter().GetResult();

                    if (socket is not null) socket.DisposeAsync().AsTask().GetAwaiter().GetResult();
                }

                throw;
            }
        }
    }
}
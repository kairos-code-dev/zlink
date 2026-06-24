using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Host;
using Zlink.Framework.Runtime.Spots;

namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkStreamRuntimeManager(
    IServiceProvider services,
    IZLinkBackendAdapterFactory backendAdapterFactory,
    ZLinkFrameworkRegistration registration)
{
    public void InitializeStreamNodes(ZLinkFrameworkRuntimeState state)
    {
        if (registration.StreamNodes.Count == 0)
        {
            return;
        }

        var streamAdapter = backendAdapterFactory.CreateStreamAdapter();
        var monitoringAdapter = backendAdapterFactory.CreateMonitoringAdapter();
        foreach (var streamNodeRegistration in registration.StreamNodes.Values)
        {
            var socket = streamAdapter.CreateStreamSocket(state.Context);
            if (streamNodeRegistration.TlsServer is { } tlsServer)
            {
                socket.SetTlsServer(tlsServer.CertPath, tlsServer.KeyPath, tlsServer.RequireClientCert);
            }
            socket.Bind(streamNodeRegistration.BindEndpoint!);
            var monitor = monitoringAdapter.OpenSocketMonitor(socket);

            var runtime = new ZLinkStreamNodeRuntime(
                streamNodeRegistration.StreamNodeName,
                services,
                socket,
                monitor,
                streamNodeRegistration.HeaderSessionType,
                state.TaskRunner);
            runtime.Start();
            state.StreamNodes.Add(streamNodeRegistration.StreamNodeName, runtime);
        }
    }

}

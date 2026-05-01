using Zlink.Framework.Backend.Contracts;

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
            socket.Bind(streamNodeRegistration.BindEndpoint!);
            var monitor = monitoringAdapter.OpenSocketMonitor(socket);

            var runtime = new ZLinkStreamNodeRuntime(
                streamNodeRegistration.StreamNodeName,
                services,
                socket,
                monitor,
                streamNodeRegistration.HeaderSessionType);
            runtime.Start();
            state.StreamNodes.Add(streamNodeRegistration.StreamNodeName, runtime);
        }
    }
}

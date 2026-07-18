namespace Zlink.Framework.Runtime.Backend.DotNet.Adapters;

internal sealed class ZLinkDotNetChannelBackendAdapter : IZLinkChannelBackendAdapter
{
    public IZLinkBackendContext CreateContext()
    {
        var context = Systems.Zlink.Zlink.CreateContext();
        try
        {
            context.Options.Blocky = false;
            return new ZLinkBackendContextWrapper(context);
        }
        catch
        {
            context.Dispose();
            throw;
        }
    }

    public IZLinkBackendDealerSocket CreateDealerSocket(IZLinkBackendContext context)
    {
        var socket = RequireContext(context).NativeContext.CreateDealerSocket();
        return new ZLinkBackendDealerSocketWrapper(
            socket);
    }

    public IZLinkBackendRouterSocket CreateRouterSocket(IZLinkBackendContext context)
    {
        var socket = RequireContext(context).NativeContext.CreateRouterSocket();
        return new ZLinkBackendRouterSocketWrapper(
            socket);
    }

    public IZLinkBackendPublisherSocket CreatePublisherSocket(IZLinkBackendContext context)
    {
        var socket = RequireContext(context).NativeContext.CreatePubSocket();
        return new ZLinkBackendPublisherSocketWrapper(
            socket);
    }

    public IZLinkBackendSubscriberSocket CreateSubscriberSocket(IZLinkBackendContext context)
    {
        var socket = RequireContext(context).NativeContext.CreateSubSocket();
        return new ZLinkBackendSubscriberSocketWrapper(
            socket);
    }

    private static ZLinkBackendContextWrapper RequireContext(IZLinkBackendContext context) =>
        context as ZLinkBackendContextWrapper
        ?? throw new InvalidOperationException("Expected the .NET backend context wrapper.");
}

internal sealed class ZLinkDotNetSpotBackendAdapter : IZLinkSpotBackendAdapter
{
    public IZLinkBackendSpotNode CreateSpotNode(IZLinkBackendContext context)
    {
        return new ZLinkBackendSpotNodeWrapper(
            (context as ZLinkBackendContextWrapper)?.NativeContext.CreateMeshNode()
            ?? throw new InvalidOperationException("Expected the .NET backend context wrapper."));
    }
}

internal sealed class ZLinkDotNetStreamBackendAdapter : IZLinkStreamBackendAdapter
{
    public IZLinkBackendStreamSocket CreateStreamSocket(IZLinkBackendContext context)
    {
        var nativeContext = (context as ZLinkBackendContextWrapper)?.NativeContext
                            ?? throw new InvalidOperationException(
                                "Expected the .NET backend context wrapper.");
        var socket = nativeContext.CreateStreamSocket();
        socket.Options.Linger = TimeSpan.Zero;
        var node = nativeContext.CreateMeshNode();
        return new ZLinkBackendStreamSocketWrapper(socket, node);
    }
}

internal sealed class ZLinkDotNetMonitoringBackendAdapter : IZLinkMonitoringBackendAdapter
{
    public IZLinkBackendSocketMonitor OpenSocketMonitor(IZLinkBackendSocket socket)
    {
        var nativeMonitor = socket switch
        {
            ZLinkBackendDealerSocketWrapper dealer => dealer.NativeSocket.MonitorOpen(),
            ZLinkBackendRouterSocketWrapper router => router.NativeSocket.MonitorOpen(),
            ZLinkBackendPublisherSocketWrapper publisher => publisher.NativeSocket.MonitorOpen(),
            ZLinkBackendSubscriberSocketWrapper subscriber => subscriber.NativeSocket.MonitorOpen(),
            ZLinkBackendStreamSocketWrapper stream => stream.NativeSocket.MonitorOpen(),
            _ => throw new InvalidOperationException("Expected a .NET backend socket wrapper.")
        };
        return new ZLinkBackendSocketMonitorWrapper(nativeMonitor);
    }
}

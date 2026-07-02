namespace Zlink.Framework.Runtime.Backend.DotNet.Adapters;

internal sealed class ZLinkDotNetChannelBackendAdapter : IZLinkChannelBackendAdapter
{
    public IZLinkBackendContext CreateContext()
    {
        return new ZLinkBackendContextWrapper(
            Systems.Zlink.Zlink.CreateContext());
    }

    public IZLinkBackendDealerSocket CreateDealerSocket(IZLinkBackendContext context)
    {
        var socket = context.RequireNative<IContext>().CreateDealerSocket();
        socket.Options.Linger = TimeSpan.Zero;
        return new ZLinkBackendDealerSocketWrapper(
            socket);
    }

    public IZLinkBackendRouterSocket CreateRouterSocket(IZLinkBackendContext context)
    {
        var socket = context.RequireNative<IContext>().CreateRouterSocket();
        socket.Options.Linger = TimeSpan.Zero;
        return new ZLinkBackendRouterSocketWrapper(
            socket);
    }

    public IZLinkBackendPublisherSocket CreatePublisherSocket(IZLinkBackendContext context)
    {
        var socket = context.RequireNative<IContext>().CreatePubSocket();
        socket.Options.Linger = TimeSpan.Zero;
        return new ZLinkBackendPublisherSocketWrapper(
            socket);
    }

    public IZLinkBackendSubscriberSocket CreateSubscriberSocket(IZLinkBackendContext context)
    {
        var socket = context.RequireNative<IContext>().CreateSubSocket();
        socket.Options.Linger = TimeSpan.Zero;
        return new ZLinkBackendSubscriberSocketWrapper(
            socket);
    }
}

internal sealed class ZLinkDotNetSpotBackendAdapter : IZLinkSpotBackendAdapter
{
    public IZLinkBackendSpotNode CreateSpotNode(
        IZLinkBackendContext context,
        SpotNodeMode mode)
    {
        return new ZLinkBackendSpotNodeWrapper(
            context.RequireNative<IContext>().CreateSpotNode(mode));
    }
}

internal sealed class ZLinkDotNetStreamBackendAdapter : IZLinkStreamBackendAdapter
{
    public IZLinkBackendStreamSocket CreateStreamSocket(IZLinkBackendContext context)
    {
        var socket = context.RequireNative<IContext>().CreateStreamSocket();
        socket.Options.Linger = TimeSpan.Zero;
        return new ZLinkBackendStreamSocketWrapper(
            socket);
    }
}

internal sealed class ZLinkDotNetMonitoringBackendAdapter : IZLinkMonitoringBackendAdapter
{
    public IZLinkBackendSocketMonitor OpenSocketMonitor(IZLinkBackendSocket socket)
    {
        var nativeMonitor = socket.OpenNativeMonitor();
        return new ZLinkBackendSocketMonitorWrapper(nativeMonitor);
    }
}

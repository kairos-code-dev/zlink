namespace Zlink.Framework.Backend.DotNet.Mappings;


internal static class ZLinkBackendNativeAccess
{
    private const System.Reflection.BindingFlags InstanceMethod =
        System.Reflection.BindingFlags.Instance
        | System.Reflection.BindingFlags.Public
        | System.Reflection.BindingFlags.NonPublic;

    public static T RequireNative<T>(this IZLinkBackendObject backendObject)
        where T : class
    {
        return backendObject.NativeInstance as T
            ?? throw new InvalidOperationException(
                $"Expected native instance '{typeof(T).FullName}'.");
    }

    public static SocketMonitor OpenNativeMonitor(this IZLinkBackendSocket socket)
    {
        return socket.NativeInstance switch
        {
            DealerSocket native => native.MonitorOpen(),
            RouterSocket native => native.MonitorOpen(),
            PubSocket native => native.MonitorOpen(),
            SubSocket native => native.MonitorOpen(),
            StreamSocket native => native.MonitorOpen(),
            _ => throw new InvalidOperationException("Expected a native socket instance."),
        };
    }

    public static void SetNativeChannelName(object socket, string channelName)
    {
        var method = socket.GetType().GetMethod(
            "SetChannelName",
            InstanceMethod,
            binder: null,
            types: [typeof(string)],
            modifiers: null);
        if (method is null)
        {
            throw new MissingMethodException(
                socket.GetType().FullName,
                "SetChannelName");
        }

        method.Invoke(socket, [channelName]);
    }

    public static void DisconnectNativeStreamPeer(StreamSocket socket, RoutingId routingId)
    {
        var method = typeof(StreamSocket).GetMethod(
            "DisconnectRid",
            InstanceMethod,
            binder: null,
            types: [typeof(RoutingId)],
            modifiers: null);
        if (method is null)
        {
            throw new MissingMethodException(
                typeof(StreamSocket).FullName,
                "DisconnectRid");
        }

        method.Invoke(socket, [routingId]);
    }

    public static void OnNativeStreamRawPacket(
        StreamSocket socket,
        Func<string, Message, int> handler)
    {
        var method = typeof(StreamSocket).GetMethod(
            "OnPacket",
            InstanceMethod,
            binder: null,
            types: [typeof(Func<string, Message, int>)],
            modifiers: null);
        if (method is null)
        {
            throw new MissingMethodException(
                typeof(StreamSocket).FullName,
                "OnPacket");
        }

        method.Invoke(socket, [handler]);
    }
}

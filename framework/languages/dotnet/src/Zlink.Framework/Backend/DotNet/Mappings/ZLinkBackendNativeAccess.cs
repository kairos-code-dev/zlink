namespace Zlink.Framework.Backend.DotNet.Mappings;


internal static class ZLinkBackendNativeAccess
{
    public static T RequireNative<T>(this IZLinkBackendObject backendObject)
        where T : class
    {
        return backendObject.NativeInstance as T
            ?? throw new InvalidOperationException(
                $"Expected native instance '{typeof(T).FullName}'.");
    }

    public static global::Zlink.SocketMonitor OpenNativeMonitor(this IZLinkBackendSocket socket)
    {
        return socket.NativeInstance switch
        {
            global::Zlink.DealerSocket native => native.MonitorOpen(),
            global::Zlink.RouterSocket native => native.MonitorOpen(),
            global::Zlink.PubSocket native => native.MonitorOpen(),
            global::Zlink.SubSocket native => native.MonitorOpen(),
            global::Zlink.StreamSocket native => native.MonitorOpen(),
            _ => throw new InvalidOperationException("Expected a native socket instance."),
        };
    }
}

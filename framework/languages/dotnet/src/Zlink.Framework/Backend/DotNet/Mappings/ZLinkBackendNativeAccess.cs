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
}

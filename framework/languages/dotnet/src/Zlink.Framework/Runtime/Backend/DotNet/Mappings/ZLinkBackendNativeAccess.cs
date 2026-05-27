namespace Zlink.Framework.Runtime.Backend.DotNet.Mappings;


internal static class ZLinkBackendNativeAccess
{
    public static T RequireNative<T>(this IZLinkBackendObject backendObject)
        where T : class
    {
        return backendObject.NativeInstance as T
            ?? throw new InvalidOperationException(
                $"Expected native instance '{typeof(T).FullName}'.");
    }

    public static ISocketMonitor OpenNativeMonitor(this IZLinkBackendSocket socket)
    {
        return socket.NativeInstance switch
        {
            IDealerSocket native => native.MonitorOpen(),
            IRouterSocket native => native.MonitorOpen(),
            IPubSocket native => native.MonitorOpen(),
            ISubSocket native => native.MonitorOpen(),
            IStreamSocket native => native.MonitorOpen(),
            _ => throw new InvalidOperationException("Expected a native socket instance."),
        };
    }
}

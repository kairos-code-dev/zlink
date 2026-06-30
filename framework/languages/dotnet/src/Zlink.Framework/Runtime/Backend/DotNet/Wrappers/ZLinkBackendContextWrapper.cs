namespace Zlink.Framework.Runtime.Backend.DotNet.Wrappers;

internal sealed class ZLinkBackendContextWrapper(IContext nativeContext) : IZLinkBackendContext
{
    public object NativeInstance => nativeContext;

    public void Shutdown()
    {
        nativeContext.Shutdown();
    }

    public ValueTask DisposeAsync()
    {
        return nativeContext.DisposeAsync();
    }
}
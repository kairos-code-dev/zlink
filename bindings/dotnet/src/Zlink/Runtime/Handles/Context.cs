// SPDX-License-Identifier: MPL-2.0

using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

internal sealed class Context : IContext
{
    public Context()
    {
        Options = new ContextOptions(this);
        Handle = NativeMethods.zlink_ctx_new();
        if (Handle == IntPtr.Zero)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
    }

    internal IntPtr Handle { get; private set; }

    public ContextOptions Options { get; }

    IContextOptions IContext.Options => Options;

    public IPairSocket CreatePairSocket()
    {
        return new PairSocket(this);
    }

    public IDealerSocket CreateDealerSocket()
    {
        return new DealerSocket(this);
    }

    public IRouterSocket CreateRouterSocket()
    {
        return new RouterSocket(this);
    }

    public IPubSocket CreatePubSocket()
    {
        return new PubSocket(this);
    }

    public ISubSocket CreateSubSocket()
    {
        return new SubSocket(this);
    }

    public IXPubSocket CreateXPubSocket()
    {
        return new XPubSocket(this);
    }

    public IXSubSocket CreateXSubSocket()
    {
        return new XSubSocket(this);
    }

    public IStreamSocket CreateStreamSocket()
    {
        return new StreamSocket(this);
    }

    public IRegistry CreateRegistry()
    {
        return new Registry(this);
    }

    public IRegistryQueryClient CreateRegistryQueryClient()
    {
        return new RegistryQueryClient(this);
    }

    public IDiscovery CreateDiscovery(AutoConnectType autoConnectType,
        string channelName)
    {
        return new Discovery(this, autoConnectType, channelName);
    }

    public ISpotNode CreateSpotNode()
    {
        return new SpotNode(this);
    }

    public ISpotNode CreateSpotNode(SpotNodeMode mode)
    {
        return new SpotNode(this, mode);
    }

    public void Shutdown()
    {
        EnsureNotDisposed();
        var rc = NativeMethods.zlink_ctx_shutdown(Handle);
        if (rc < 0)
            throw ZlinkException.CreateCloseException(NativeMethods.zlink_errno());
    }

    public void RecalculateAutoHwm()
    {
        EnsureNotDisposed();
        var rc = NativeMethods.zlink_ctx_auto_hwm_recalculate(Handle);
        if (rc != 0)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
    }

    public void Dispose()
    {
        if (Handle == IntPtr.Zero)
            return;
        while (true)
        {
            var shutdownRc = NativeMethods.zlink_ctx_shutdown(Handle);
            if (shutdownRc == 0)
                break;
            var shutdownErrno = NativeMethods.zlink_errno();
            var shutdownCode = ZlinkException.MapErrorCode(shutdownErrno);
            if (shutdownCode == ErrorCode.EIntr || shutdownErrno == 4)
                continue;
            break;
        }

        int rc;
        while (true)
        {
            rc = NativeMethods.zlink_ctx_term(Handle);
            if (rc == 0)
                break;
            var errno = NativeMethods.zlink_errno();
            var code = ZlinkException.MapErrorCode(errno);
            if (code == ErrorCode.EIntr || errno == 4)
                continue;
            break;
        }

        Handle = IntPtr.Zero;
        if (rc < 0)
            throw ZlinkException.CreateCloseException(NativeMethods.zlink_errno());
        GC.SuppressFinalize(this);
    }

    public ValueTask DisposeAsync()
    {
        Dispose();
        return ValueTask.CompletedTask;
    }

    internal void SetOption(ContextOption option, int value)
    {
        EnsureNotDisposed();
        EnumValidation.EnsureContextOption(option, nameof(option));
        var rc = NativeMethods.zlink_ctx_set(Handle, (int)option, value);
        if (rc != 0)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
    }

    internal int GetOption(ContextOption option)
    {
        EnsureNotDisposed();
        EnumValidation.EnsureContextOption(option, nameof(option));
        var value = NativeMethods.zlink_ctx_get(Handle, (int)option,
            out var errorOut);
        if (errorOut != (int)ConfigResult.Ok)
            throw new ZlinkConfigException((ConfigResult)errorOut,
                NativeMethods.zlink_errno());
        return value;
    }

    ~Context()
    {
        if (Handle == IntPtr.Zero)
            return;

        try
        {
            _ = NativeMethods.zlink_ctx_shutdown(Handle);
        }
        catch
        {
        }
    }

    private void EnsureNotDisposed()
    {
        if (Handle == IntPtr.Zero)
            throw new ObjectDisposedException(nameof(Context));
    }
}
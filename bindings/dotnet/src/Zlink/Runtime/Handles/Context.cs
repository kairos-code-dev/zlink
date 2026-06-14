// SPDX-License-Identifier: MPL-2.0

using System;
using System.Threading.Tasks;
using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

internal sealed class Context : IContext
{
    private IntPtr _handle;

    public Context()
    {
        Options = new ContextOptions(this);
        _handle = NativeMethods.zlink_ctx_new();
        if (_handle == IntPtr.Zero)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
    }

    internal IntPtr Handle => _handle;

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

    internal void SetOption(ContextOption option, int value)
    {
        EnsureNotDisposed();
        EnumValidation.EnsureContextOption(option, nameof(option));
        int rc = NativeMethods.zlink_ctx_set(_handle, (int)option, value);
        if (rc != 0)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
    }

    internal int GetOption(ContextOption option)
    {
        EnsureNotDisposed();
        EnumValidation.EnsureContextOption(option, nameof(option));
        int value = NativeMethods.zlink_ctx_get(_handle, (int)option,
            out int errorOut);
        if (errorOut != (int)ConfigResult.Ok)
            throw new ZlinkConfigException((ConfigResult)errorOut,
                NativeMethods.zlink_errno());
        return value;
    }

    public void Shutdown()
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_ctx_shutdown(_handle);
        if (rc < 0)
            throw ZlinkException.CreateCloseException(NativeMethods.zlink_errno());
    }

    public void RecalculateAutoHwm()
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_ctx_auto_hwm_recalculate(_handle);
        if (rc != 0)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
    }

    public void Dispose()
    {
        if (_handle == IntPtr.Zero)
            return;
        while (true)
        {
            int shutdownRc = NativeMethods.zlink_ctx_shutdown(_handle);
            if (shutdownRc == 0)
                break;
            int shutdownErrno = NativeMethods.zlink_errno();
            ErrorCode shutdownCode = ZlinkException.MapErrorCode(shutdownErrno);
            if (shutdownCode == ErrorCode.EIntr || shutdownErrno == 4)
                continue;
            break;
        }
        int rc;
        while (true)
        {
            rc = NativeMethods.zlink_ctx_term(_handle);
            if (rc == 0)
                break;
            int errno = NativeMethods.zlink_errno();
            ErrorCode code = ZlinkException.MapErrorCode(errno);
            if (code == ErrorCode.EIntr || errno == 4)
                continue;
            break;
        }
        _handle = IntPtr.Zero;
        if (rc < 0)
            throw ZlinkException.CreateCloseException(NativeMethods.zlink_errno());
        GC.SuppressFinalize(this);
    }

    public ValueTask DisposeAsync()
    {
        Dispose();
        return ValueTask.CompletedTask;
    }

    ~Context()
    {
        if (_handle == IntPtr.Zero)
            return;

        try
        {
            _ = NativeMethods.zlink_ctx_shutdown(_handle);
        }
        catch
        {
        }
    }

    private void EnsureNotDisposed()
    {
        if (_handle == IntPtr.Zero)
            throw new ObjectDisposedException(nameof(Context));
    }
}

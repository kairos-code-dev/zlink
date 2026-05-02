// SPDX-License-Identifier: MPL-2.0

using System;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using Zlink;
using Zlink.Native;

namespace Zlink;

public sealed class Discovery : IDisposable, IAsyncDisposable
{
    private IntPtr _handle;

    public Discovery(Context context, AutoConnectType autoConnectType,
        string channelName)
    {
        if (context == null)
            throw new ArgumentNullException(nameof(context));
        BoundaryValidation.ValidateFixedUtf8(channelName, nameof(channelName));
        _handle = NativeMethods.zlink_discovery_new(context.Handle,
            (int)autoConnectType, channelName);
        if (_handle == IntPtr.Zero)
            throw ZlinkException.CreateConfigException(
                NativeMethods.zlink_errno());
    }

    internal IntPtr Handle => _handle;

    public void ConnectRegistry(string registryPubEndpoint)
    {
        BoundaryValidation.ValidateFixedUtf8(registryPubEndpoint,
            nameof(registryPubEndpoint));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_discovery_connect_registry(_handle,
            registryPubEndpoint);
        ZlinkException.ThrowConnectIfError(rc);
    }

    public void SetValue(long value)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_discovery_set_value(_handle, value);
        ZlinkException.ThrowConfigIfError(rc);
    }

    public long GetValue()
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_discovery_get_value(_handle, out long value);
        ZlinkException.ThrowConfigIfError(rc);
        return value;
    }

    public void SetMetadata(Message metadata)
    {
        if (metadata == null)
            throw new ArgumentNullException(nameof(metadata));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_discovery_set_metadata(_handle,
            NativeMethods.zlink_msg_data(ref metadata.Handle),
            (nuint)metadata.Size);
        ZlinkException.ThrowConfigIfError(rc);
    }

    public Message GetMetadata()
    {
        EnsureNotDisposed();
        using var metadata = new Message();
        int rc = NativeMethods.zlink_discovery_get_metadata(_handle,
            ref metadata.Handle);
        ZlinkException.ThrowConfigIfError(rc);
        return metadata.Move();
    }

    public MemberPeerEntry[] MemberPeers()
    {
        EnsureNotDisposed();
        nuint count = 0;
        int rc = NativeMethods.zlink_discovery_member_peers(_handle,
            IntPtr.Zero, ref count);
        ZlinkException.ThrowConfigIfError(rc);
        if (count == 0)
            return Array.Empty<MemberPeerEntry>();

        int entrySize = Marshal.SizeOf<ZlinkMemberPeerEntry>();
        IntPtr entries = Marshal.AllocHGlobal(checked((int)(count * (nuint)entrySize)));
        try
        {
            nuint actual = count;
            rc = NativeMethods.zlink_discovery_member_peers(_handle, entries,
                ref actual);
            ZlinkException.ThrowConfigIfError(rc);

            MemberPeerEntry[] result = new MemberPeerEntry[(int)actual];
            for (int i = 0; i < result.Length; i++)
            {
                IntPtr current = IntPtr.Add(entries, i * entrySize);
                ZlinkMemberPeerEntry native =
                    Marshal.PtrToStructure<ZlinkMemberPeerEntry>(current);
                result[i] = MemberPeerEntry.FromNative(ref native);
            }
            return result;
        }
        finally
        {
            Marshal.FreeHGlobal(entries);
        }
    }

    public Message MemberPeerMetadata(ServiceRole serviceRole, string endpoint)
    {
        BoundaryValidation.ValidateFixedUtf8(endpoint, nameof(endpoint));
        EnsureNotDisposed();
        using var metadata = new Message();
        int rc = NativeMethods.zlink_discovery_member_peer_metadata(_handle,
            (int)serviceRole, endpoint, ref metadata.Handle);
        ZlinkException.ThrowConfigIfError(rc);
        return metadata.Move();
    }

    public RoutingId ResolveSpot(RoutingId spotRid)
    {
        EnsureNotDisposed();
        byte[] spotRidBytes = spotRid.ToByteArray();
        unsafe
        {
            fixed (byte* spotRidPtr = spotRidBytes)
            {
                ZlinkRoutingId nativeSpotRid = default;
                if (spotRidBytes.Length != 0)
                {
                    nativeSpotRid = NativeHelpers.WriteRoutingId(
                        new ReadOnlySpan<byte>(spotRidPtr, spotRidBytes.Length));
                }

                int rc = NativeMethods.zlink_discovery_resolve_spot(_handle,
                    ref nativeSpotRid, out ZlinkRoutingId ownerNodeRoutingId);
                ZlinkException.ThrowConfigIfError(rc);
                return RoutingId.FromBytes(
                    NativeHelpers.ReadRoutingId(ref ownerNodeRoutingId));
            }
        }
    }

    public void Close()
    {
        Dispose();
    }

    public void Dispose()
    {
        Destroy(throwOnError: true);
        GC.SuppressFinalize(this);
    }

    public ValueTask DisposeAsync()
    {
        Dispose();
        return ValueTask.CompletedTask;
    }

    ~Discovery()
    {
        Destroy(throwOnError: false);
    }

    private void Destroy(bool throwOnError)
    {
        if (_handle == IntPtr.Zero)
            return;

        IntPtr originalHandle = _handle;
        IntPtr handle = _handle;
        int rc = NativeMethods.zlink_discovery_destroy(ref handle);
        if (rc == 0)
        {
            _handle = IntPtr.Zero;
            return;
        }

        _handle = originalHandle;
        if (throwOnError)
            throw ZlinkException.CreateCloseException(NativeMethods.zlink_errno());
    }

    private void EnsureNotDisposed()
    {
        if (_handle == IntPtr.Zero)
            throw new ObjectDisposedException(nameof(Discovery));
    }
}

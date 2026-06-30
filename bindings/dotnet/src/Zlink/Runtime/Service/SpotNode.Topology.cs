// SPDX-License-Identifier: MPL-2.0

using System.Runtime.InteropServices;
using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

internal sealed partial class SpotNode : ISpotNode
{
    public SpotNodeStatus Status()
    {
        EnsureNotDisposed();
        var rc = NativeMethods.zlink_spot_node_status(Handle,
            out var native);
        ZlinkException.ThrowConfigIfError(rc);
        return TopologyModelConverters.FromNative(ref native);
    }

    public SpotNodePeerEntry[] Peers()
    {
        EnsureNotDisposed();
        return ReadPeerEntries(IntPtr.Zero);
    }

    public SpotNodePeerEntry[] PeersQuery(SpotNodePeerFilter filter)
    {
        EnsureNotDisposed();
        unsafe
        {
            ZlinkSpotNodePeerFilter nativeFilter = default;
            nativeFilter.Source = (int)filter.Source.GetValueOrDefault();
            nativeFilter.State = (int)filter.State.GetValueOrDefault();
            if (!string.IsNullOrEmpty(filter.PeerEndpoint))
            {
                BoundaryValidation.ValidateFixedUtf8(filter.PeerEndpoint,
                    nameof(filter.PeerEndpoint));
                WriteFixedString(filter.PeerEndpoint, nativeFilter.PeerEndpoint,
                    256);
            }

            return ReadPeerEntries((IntPtr)(&nativeFilter));
        }
    }

    public SpotNodeSubjectEntry[] Subjects(
        SpotNodeSubjectFilter? filter = null)
    {
        EnsureNotDisposed();
        unsafe
        {
            ZlinkSpotNodeSubjectFilter nativeFilter = default;
            var filterPtr = IntPtr.Zero;
            if (filter != null)
            {
                var value = filter;
                if (value.Role.HasValue || !string.IsNullOrEmpty(value.Subject)
                                        || value.SubjectKind.HasValue)
                {
                    nativeFilter.Role = (int)value.Role.GetValueOrDefault();
                    nativeFilter.SubjectKind =
                        (uint)value.SubjectKind.GetValueOrDefault();
                    if (!string.IsNullOrEmpty(value.Subject))
                    {
                        BoundaryValidation.ValidateFixedUtf8(value.Subject,
                            nameof(SpotNodeSubjectFilter.Subject));
                        WriteFixedString(value.Subject, nativeFilter.Subject,
                            256);
                    }

                    filterPtr = (IntPtr)(&nativeFilter);
                }
            }

            return ReadSubjectEntries(filterPtr);
        }
    }

    public SpotNodeSocketEntry[] InternalSockets(
        SpotNodeSocketFilter? filter = null)
    {
        EnsureNotDisposed();
        unsafe
        {
            ZlinkSpotNodeSocketFilter nativeFilter = default;
            var filterPtr = IntPtr.Zero;
            if (filter != null)
            {
                var value = filter;
                nativeFilter.Owner =
                    value.Owner.GetValueOrDefault(SpotNodeSocketOwner.Any);
                nativeFilter.SocketType =
                    value.SocketType.GetValueOrDefault(SpotNodeSocketType.Any);
                if (!string.IsNullOrEmpty(value.SocketName))
                {
                    BoundaryValidation.ValidateFixedUtf8(value.SocketName,
                        nameof(SpotNodeSocketFilter.SocketName));
                    WriteFixedString(value.SocketName,
                        nativeFilter.SocketName, 64);
                }

                filterPtr = (IntPtr)(&nativeFilter);
            }

            return ReadInternalSocketEntries(filterPtr);
        }
    }

    public SpotNodeSpotEntry[] Spots()
    {
        EnsureNotDisposed();
        nuint count = 0;
        var rc = NativeMethods.zlink_spot_node_spots(Handle,
            IntPtr.Zero, ref count);
        ZlinkException.ThrowConfigIfError(rc);
        if (count == 0)
            return Array.Empty<SpotNodeSpotEntry>();

        var entrySize = Marshal.SizeOf<ZlinkSpotNodeSpotEntry>();
        var entries = Marshal.AllocHGlobal(
            checked((int)(count * (nuint)entrySize)));
        try
        {
            var actual = count;
            rc = NativeMethods.zlink_spot_node_spots(Handle,
                entries, ref actual);
            ZlinkException.ThrowConfigIfError(rc);
            var result = new SpotNodeSpotEntry[(int)actual];
            for (var i = 0; i < result.Length; i++)
            {
                var native =
                    Marshal.PtrToStructure<ZlinkSpotNodeSpotEntry>(
                        IntPtr.Add(entries, i * entrySize));
                result[i] = new SpotNodeSpotEntry(
                    RoutingIdInterop.FromNative(ref native.SpotRid),
                    (SpotKind)native.SpotKind,
                    native.DispatchHandlerAttached != 0,
                    native.JoinedActorCount,
                    native.PendingActorJoinCount,
                    native.RouteSynced != 0,
                    native.LastChangedMs);
            }

            return result;
        }
        finally
        {
            Marshal.FreeHGlobal(entries);
        }
    }

    public SpotNodeActorEntry[] Actors()
    {
        EnsureNotDisposed();
        nuint count = 0;
        var rc = NativeMethods.zlink_spot_node_actors(Handle,
            IntPtr.Zero, ref count);
        ZlinkException.ThrowConfigIfError(rc);
        if (count == 0)
            return Array.Empty<SpotNodeActorEntry>();

        var entrySize = Marshal.SizeOf<ZlinkSpotNodeActorEntry>();
        var entries = Marshal.AllocHGlobal(
            checked((int)(count * (nuint)entrySize)));
        try
        {
            var actual = count;
            rc = NativeMethods.zlink_spot_node_actors(Handle,
                entries, ref actual);
            ZlinkException.ThrowConfigIfError(rc);
            var result = new SpotNodeActorEntry[(int)actual];
            for (var i = 0; i < result.Length; i++)
            {
                var native =
                    Marshal.PtrToStructure<ZlinkSpotNodeActorEntry>(
                        IntPtr.Add(entries, i * entrySize));
                result[i] = new SpotNodeActorEntry(
                    ActorInterop.FromNative(ref native.Actor),
                    RoutingIdInterop.FromNative(ref native.CurrentSpotRid)
                    ?? throw new ZlinkConfigException(
                        ZlinkConfigException.ErrorCode.InternalError),
                    (SpotKind)native.CurrentSpotKind,
                    native.RouteSynced != 0,
                    native.PendingMessageCount,
                    native.LastChangedMs);
            }

            return result;
        }
        finally
        {
            Marshal.FreeHGlobal(entries);
        }
    }

    private SpotNodePeerEntry[] ReadPeerEntries(IntPtr filterPtr)
    {
        nuint count = 0;
        var rc = NativeMethods.zlink_spot_node_peers(Handle, filterPtr,
            IntPtr.Zero, ref count);
        ZlinkException.ThrowConfigIfError(rc);
        if (count == 0)
            return Array.Empty<SpotNodePeerEntry>();

        var entrySize = Marshal.SizeOf<ZlinkSpotNodePeerEntry>();
        var entries = Marshal.AllocHGlobal(checked((int)(count * (nuint)entrySize)));
        try
        {
            var actual = count;
            rc = NativeMethods.zlink_spot_node_peers(Handle, filterPtr,
                entries, ref actual);
            ZlinkException.ThrowConfigIfError(rc);

            var result = new SpotNodePeerEntry[(int)actual];
            for (var i = 0; i < result.Length; i++)
            {
                var current = IntPtr.Add(entries, i * entrySize);
                var native =
                    Marshal.PtrToStructure<ZlinkSpotNodePeerEntry>(current);
                result[i] = TopologyModelConverters.FromNative(ref native);
            }

            return result;
        }
        finally
        {
            Marshal.FreeHGlobal(entries);
        }
    }

    private SpotNodeSubjectEntry[] ReadSubjectEntries(IntPtr filterPtr)
    {
        nuint count = 0;
        var rc = NativeMethods.zlink_spot_node_subjects(Handle,
            filterPtr, IntPtr.Zero, ref count);
        ZlinkException.ThrowConfigIfError(rc);
        if (count == 0)
            return Array.Empty<SpotNodeSubjectEntry>();

        var entrySize = Marshal.SizeOf<ZlinkSpotNodeSubjectEntry>();
        var entries = Marshal.AllocHGlobal(checked((int)(count * (nuint)entrySize)));
        try
        {
            var actual = count;
            rc = NativeMethods.zlink_spot_node_subjects(Handle,
                filterPtr, entries, ref actual);
            ZlinkException.ThrowConfigIfError(rc);

            var result = new SpotNodeSubjectEntry[(int)actual];
            for (var i = 0; i < result.Length; i++)
            {
                var current = IntPtr.Add(entries, i * entrySize);
                var native =
                    Marshal.PtrToStructure<ZlinkSpotNodeSubjectEntry>(current);
                result[i] = TopologyModelConverters.FromNative(ref native);
            }

            return result;
        }
        finally
        {
            Marshal.FreeHGlobal(entries);
        }
    }

    private SpotNodeSocketEntry[] ReadInternalSocketEntries(
        IntPtr filterPtr)
    {
        nuint count = 0;
        var rc = NativeMethods.zlink_spot_node_internal_sockets(
            Handle, filterPtr, IntPtr.Zero, ref count);
        ZlinkException.ThrowConfigIfError(rc);
        if (count == 0)
            return Array.Empty<SpotNodeSocketEntry>();

        var entrySize = Marshal.SizeOf<ZlinkSpotNodeSocketEntry>();
        var entries = Marshal.AllocHGlobal(
            checked((int)(count * (nuint)entrySize)));
        try
        {
            var actual = count;
            rc = NativeMethods.zlink_spot_node_internal_sockets(
                Handle, filterPtr, entries, ref actual);
            ZlinkException.ThrowConfigIfError(rc);

            var result =
                new SpotNodeSocketEntry[(int)actual];
            for (var i = 0; i < result.Length; i++)
            {
                var current = IntPtr.Add(entries, i * entrySize);
                var native =
                    Marshal.PtrToStructure<ZlinkSpotNodeSocketEntry>(
                        current);
                result[i] = TopologyModelConverters.FromNative(ref native);
            }

            return result;
        }
        finally
        {
            Marshal.FreeHGlobal(entries);
        }
    }
}
// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Systems.Zlink.Runtime.Native;
using Systems.Zlink.Runtime.Sockets.Internal;

namespace Systems.Zlink;

internal sealed partial class SpotNode : ISpotNode
{
    public SpotNodeStatus Status()
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_node_status(_handle,
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
            IntPtr filterPtr = IntPtr.Zero;
            if (filter != null)
            {
                SpotNodeSubjectFilter value = filter;
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
            IntPtr filterPtr = IntPtr.Zero;
            if (filter != null)
            {
                SpotNodeSocketFilter value = filter;
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
        int rc = NativeMethods.zlink_spot_node_spots(_handle,
            IntPtr.Zero, ref count);
        ZlinkException.ThrowConfigIfError(rc);
        if (count == 0)
            return Array.Empty<SpotNodeSpotEntry>();

        int entrySize = Marshal.SizeOf<ZlinkSpotNodeSpotEntry>();
        IntPtr entries = Marshal.AllocHGlobal(
            checked((int)(count * (nuint)entrySize)));
        try
        {
            nuint actual = count;
            rc = NativeMethods.zlink_spot_node_spots(_handle,
                entries, ref actual);
            ZlinkException.ThrowConfigIfError(rc);
            SpotNodeSpotEntry[] result = new SpotNodeSpotEntry[(int)actual];
            for (int i = 0; i < result.Length; i++)
            {
                ZlinkSpotNodeSpotEntry native =
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
        int rc = NativeMethods.zlink_spot_node_actors(_handle,
            IntPtr.Zero, ref count);
        ZlinkException.ThrowConfigIfError(rc);
        if (count == 0)
            return Array.Empty<SpotNodeActorEntry>();

        int entrySize = Marshal.SizeOf<ZlinkSpotNodeActorEntry>();
        IntPtr entries = Marshal.AllocHGlobal(
            checked((int)(count * (nuint)entrySize)));
        try
        {
            nuint actual = count;
            rc = NativeMethods.zlink_spot_node_actors(_handle,
                entries, ref actual);
            ZlinkException.ThrowConfigIfError(rc);
            SpotNodeActorEntry[] result = new SpotNodeActorEntry[(int)actual];
            for (int i = 0; i < result.Length; i++)
            {
                ZlinkSpotNodeActorEntry native =
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
        int rc = NativeMethods.zlink_spot_node_peers(_handle, filterPtr,
            IntPtr.Zero, ref count);
        ZlinkException.ThrowConfigIfError(rc);
        if (count == 0)
            return Array.Empty<SpotNodePeerEntry>();

        int entrySize = Marshal.SizeOf<ZlinkSpotNodePeerEntry>();
        IntPtr entries = Marshal.AllocHGlobal(checked((int)(count * (nuint)entrySize)));
        try
        {
            nuint actual = count;
            rc = NativeMethods.zlink_spot_node_peers(_handle, filterPtr,
                entries, ref actual);
            ZlinkException.ThrowConfigIfError(rc);

            SpotNodePeerEntry[] result = new SpotNodePeerEntry[(int)actual];
            for (int i = 0; i < result.Length; i++)
            {
                IntPtr current = IntPtr.Add(entries, i * entrySize);
                ZlinkSpotNodePeerEntry native =
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
        int rc = NativeMethods.zlink_spot_node_subjects(_handle,
            filterPtr, IntPtr.Zero, ref count);
        ZlinkException.ThrowConfigIfError(rc);
        if (count == 0)
            return Array.Empty<SpotNodeSubjectEntry>();

        int entrySize = Marshal.SizeOf<ZlinkSpotNodeSubjectEntry>();
        IntPtr entries = Marshal.AllocHGlobal(checked((int)(count * (nuint)entrySize)));
        try
        {
            nuint actual = count;
            rc = NativeMethods.zlink_spot_node_subjects(_handle,
                filterPtr, entries, ref actual);
            ZlinkException.ThrowConfigIfError(rc);

            SpotNodeSubjectEntry[] result = new SpotNodeSubjectEntry[(int)actual];
            for (int i = 0; i < result.Length; i++)
            {
                IntPtr current = IntPtr.Add(entries, i * entrySize);
                ZlinkSpotNodeSubjectEntry native =
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
        int rc = NativeMethods.zlink_spot_node_internal_sockets(
            _handle, filterPtr, IntPtr.Zero, ref count);
        ZlinkException.ThrowConfigIfError(rc);
        if (count == 0)
            return Array.Empty<SpotNodeSocketEntry>();

        int entrySize = Marshal.SizeOf<ZlinkSpotNodeSocketEntry>();
        IntPtr entries = Marshal.AllocHGlobal(
            checked((int)(count * (nuint)entrySize)));
        try
        {
            nuint actual = count;
            rc = NativeMethods.zlink_spot_node_internal_sockets(
                _handle, filterPtr, entries, ref actual);
            ZlinkException.ThrowConfigIfError(rc);

            SpotNodeSocketEntry[] result =
                new SpotNodeSocketEntry[(int)actual];
            for (int i = 0; i < result.Length; i++)
            {
                IntPtr current = IntPtr.Add(entries, i * entrySize);
                ZlinkSpotNodeSocketEntry native =
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

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
                    value.SocketType.GetValueOrDefault(SocketType.Any);
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
        return NativeSnapshotReader.Read
            <ZlinkSpotNodeSpotEntry, SpotNodeSpotEntry>(
                (IntPtr entries, ref nuint count) =>
                    NativeMethods.zlink_spot_node_spots(Handle, entries,
                        ref count),
                static (ref ZlinkSpotNodeSpotEntry native) =>
                    new SpotNodeSpotEntry(
                        RoutingId.FromNative(ref native.SpotRid),
                        (SpotKind)native.SpotKind,
                        native.DispatchHandlerAttached != 0,
                        native.JoinedActorCount,
                        native.PendingActorJoinCount,
                        native.RouteSynced != 0,
                        native.LastChangedMs));
    }

    public SpotNodeActorEntry[] Actors()
    {
        EnsureNotDisposed();
        return NativeSnapshotReader.Read
            <ZlinkSpotNodeActorEntry, SpotNodeActorEntry>(
                (IntPtr entries, ref nuint count) =>
                    NativeMethods.zlink_spot_node_actors(Handle, entries,
                        ref count),
                static (ref ZlinkSpotNodeActorEntry native) =>
                    new SpotNodeActorEntry(
                        ActorInterop.FromNative(ref native.Actor),
                        RoutingId.FromNative(ref native.CurrentSpotRid)
                        ?? throw new ZlinkConfigException(
                            ZlinkConfigException.ErrorCode.InternalError),
                        (SpotKind)native.CurrentSpotKind,
                        native.RouteSynced != 0,
                        native.PendingMessageCount,
                        native.LastChangedMs));
    }

    private SpotNodePeerEntry[] ReadPeerEntries(IntPtr filterPtr)
    {
        return NativeSnapshotReader.Read
            <ZlinkSpotNodePeerEntry, SpotNodePeerEntry>(
                (IntPtr entries, ref nuint count) =>
                    NativeMethods.zlink_spot_node_peers(Handle, filterPtr,
                        entries, ref count),
                static (ref ZlinkSpotNodePeerEntry native) =>
                    TopologyModelConverters.FromNative(ref native));
    }

    private SpotNodeSubjectEntry[] ReadSubjectEntries(IntPtr filterPtr)
    {
        return NativeSnapshotReader.Read
            <ZlinkSpotNodeSubjectEntry, SpotNodeSubjectEntry>(
                (IntPtr entries, ref nuint count) =>
                    NativeMethods.zlink_spot_node_subjects(Handle, filterPtr,
                        entries, ref count),
                static (ref ZlinkSpotNodeSubjectEntry native) =>
                    TopologyModelConverters.FromNative(ref native));
    }

    private SpotNodeSocketEntry[] ReadInternalSocketEntries(
        IntPtr filterPtr)
    {
        return NativeSnapshotReader.Read
            <ZlinkSpotNodeSocketEntry, SpotNodeSocketEntry>(
                (IntPtr entries, ref nuint count) =>
                    NativeMethods.zlink_spot_node_internal_sockets(
                        Handle, filterPtr, entries, ref count),
                static (ref ZlinkSpotNodeSocketEntry native) =>
                    TopologyModelConverters.FromNative(ref native));
    }
}

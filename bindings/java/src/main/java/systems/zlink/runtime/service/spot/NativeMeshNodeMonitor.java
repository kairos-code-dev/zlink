/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.service.spot;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.errors.ConfigResult;
import systems.zlink.contracts.errors.ErrorCategory;
import systems.zlink.contracts.errors.ZlinkConfigException;
import systems.zlink.contracts.errors.ZlinkException;
import systems.zlink.contracts.errors.ZlinkRecvException;
import systems.zlink.contracts.service.spot.*;
import systems.zlink.contracts.sockets.RecvFlags;
import systems.zlink.contracts.sockets.RecvResult;
import systems.zlink.runtime.nativeapi.*;

import java.lang.foreign.Arena;
import java.lang.foreign.MemoryLayout;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;

final class NativeMeshNodeMonitor implements MeshNodeMonitor {
    private MemorySegment handle;

    private NativeMeshNodeMonitor(MemorySegment handle) {
        this.handle = handle;
    }

    static NativeMeshNodeMonitor open(NativeMeshNode node, long events) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment options = ServiceInterop.allocStamped(
                arena, ServiceLayouts.MESH_MONITOR_OPEN_OPTIONS);
            options.set(ValueLayout.JAVA_LONG_UNALIGNED,
                ServiceLayouts.off(ServiceLayouts.MESH_MONITOR_OPEN_OPTIONS, "events"), events);
            MemorySegment handle = NativeServiceSymbols.meshNodeMonitorOpen(
                node.handle(), options);
            if (handle == null || handle.address() == 0)
                throw ZlinkException.fromLastError(ErrorCategory.CONFIG);
            return new NativeMeshNodeMonitor(handle);
        }
    }

    @Override
    public MeshMonitorEvent recv(RecvFlags flags) {
        ensureOpen();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment event = ServiceInterop.allocStamped(
                arena, ServiceLayouts.MESH_MONITOR_EVENT);
            int rc = NativeServiceSymbols.meshNodeMonitorRecv(
                handle, event, flags.value());
            if (rc == RecvResult.NO_DATA.value() && flags == RecvFlags.DONT_WAIT)
                return null;
            if (rc != RecvResult.OK.value())
                throw new ZlinkRecvException(RecvResult.fromValue(rc), Native.errno());
            return convertEvent(event);
        }
    }

    @Override
    public MeshMonitorStatus status() {
        ensureOpen();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment status = ServiceInterop.allocStamped(
                arena, ServiceLayouts.MESH_MONITOR_STATUS);
            int rc = NativeServiceSymbols.meshNodeMonitorStatus(handle, status);
            if (rc != ConfigResult.OK.value())
                throw new ZlinkConfigException(ConfigResult.fromValue(rc), Native.errno());
            return new MeshMonitorStatus(
                MeshNodeState.fromValue(i(status, "state")),
                l(status, "peer_admitted"), l(status, "peer_rejected"),
                l(status, "submitted_messages"), l(status, "completed_operations"),
                l(status, "backpressured_submits"), l(status, "multicast_messages"),
                l(status, "multicast_dropped_targets"), l(status, "active_claims"),
                l(status, "pending_application_messages"),
                l(status, "pending_infrastructure_messages"), l(status, "pending_bytes"));
        }
    }

    @Override
    public void close() {
        if (handle == null || handle.address() == 0)
            return;
        int rc = NativeServiceSymbols.meshNodeMonitorClose(handle);
        if (rc != 0)
            throw ZlinkException.fromLastError(ErrorCategory.CLOSE);
        handle = MemorySegment.NULL;
    }

    private static MeshMonitorEvent convertEvent(MemorySegment e) {
        MemoryLayout layout = ServiceLayouts.MESH_MONITOR_EVENT;
        RoutingId peer = NativeRoutingIds.readAllowEmptyValue(e.asSlice(
            ServiceLayouts.off(layout, "peer_rid"),
            NativeLayouts.ROUTING_ID_LAYOUT.byteSize()));
        RoutingId spot = NativeRoutingIds.readAllowEmptyValue(e.asSlice(
            ServiceLayouts.off(layout, "spot_rid"),
            NativeLayouts.ROUTING_ID_LAYOUT.byteSize()));
        var actor = ServiceInterop.actorRefFromNative(e.asSlice(
            ServiceLayouts.off(layout, "actor"),
            NativeLayouts.ACTOR_REF_LAYOUT.byteSize()));
        String channel = NativeHelpers.fromCString(e.asSlice(
            ServiceLayouts.off(layout, "channel_name"), 256), 256);
        return new MeshMonitorEvent(
            MeshMonitorEventKind.fromValue(i(e, "kind")),
            l(e, "timestamp_ms"), l(e, "mesh_lifecycle_generation"),
            l(e, "mesh_descriptor_revision"),
            MeshNodeState.fromValue(i(e, "mesh_state")), peer,
            l(e, "peer_lifecycle_generation"), l(e, "peer_descriptor_revision"),
            ownerKindOrNull(i(e, "owner_kind")), spot, actor, channel,
            new OperationId(l(e, "operation_id_high"), l(e, "operation_id_low")),
            i(e, "snapshot_remote_target_count"), i(e, "admitted_remote_target_count"),
            i(e, "dropped_remote_target_count"), i(e, "unreachable_remote_target_count"),
            i(e, "snapshot_local_spot_count"), i(e, "admitted_local_spot_count"),
            i(e, "dropped_local_spot_count"), i(e, "result_code"), i(e, "failure_errno"));
    }

    private static OwnerKind ownerKindOrNull(int value) {
        return value == 0 ? null : OwnerKind.fromValue(value);
    }

    private static int i(MemorySegment s, String field) {
        MemoryLayout l = s.byteSize() == ServiceLayouts.MESH_MONITOR_EVENT.byteSize()
            ? ServiceLayouts.MESH_MONITOR_EVENT : ServiceLayouts.MESH_MONITOR_STATUS;
        return s.get(ValueLayout.JAVA_INT_UNALIGNED, ServiceLayouts.off(l, field));
    }

    private static long l(MemorySegment s, String field) {
        MemoryLayout layout = s.byteSize() == ServiceLayouts.MESH_MONITOR_EVENT.byteSize()
            ? ServiceLayouts.MESH_MONITOR_EVENT : ServiceLayouts.MESH_MONITOR_STATUS;
        return s.get(ValueLayout.JAVA_LONG_UNALIGNED, ServiceLayouts.off(layout, field));
    }

    private void ensureOpen() {
        if (handle == null || handle.address() == 0)
            throw new IllegalStateException("MeshNode monitor is closed");
    }
}

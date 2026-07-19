/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.service.spot;

import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.service.spot.MeshNodePublisher;
import systems.zlink.contracts.service.spot.PublishDetail;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.runtime.nativeapi.NativeHelpers;
import systems.zlink.runtime.nativeapi.NativeServiceSymbols;
import systems.zlink.runtime.nativeapi.ServiceInterop;
import systems.zlink.runtime.nativeapi.ServiceLayouts;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.util.List;
import java.util.Objects;

public final class NativeMeshNodePublisher implements MeshNodePublisher {
    private MemorySegment handle;

    NativeMeshNodePublisher(MemorySegment handle) {
        this.handle = handle;
    }

    @Override
    public void publish(String channel, String topic, List<Message> parts, SendFlags flags) {
        publishInternal(channel, topic, parts, flags, MemorySegment.NULL);
    }

    @Override
    public PublishDetail publishDetailed(String channel, String topic, List<Message> parts,
                                         SendFlags flags) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment detail = ServiceInterop.allocStamped(arena,
                ServiceLayouts.MESH_PUBLISH_DETAIL);
            publishInternal(channel, topic, parts, flags, detail);
            return ServiceInterop.publishDetailFromNative(detail);
        }
    }

    private void publishInternal(String channel, String topic, List<Message> parts,
                                 SendFlags flags, MemorySegment detail) {
        Objects.requireNonNull(channel, "channel");
        Objects.requireNonNull(topic, "topic");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment name = NativeHelpers.toCString(arena, channel);
            MemorySegment topicSeg = NativeHelpers.toCString(arena, topic);
            MemorySegment array = MeshCalls.parts(arena, parts);
            long n = MeshCalls.count(parts);
            int rc = NativeServiceSymbols.publisherPublish(handle, name, topicSeg,
                MemorySegment.NULL, array, n, detail, flags.value());
            MeshCalls.submitOk(rc, array, n, "zlink_mesh_node_publisher_publish");
        }
    }

    @Override
    public void close() {
        if (handle == null || handle.address() == 0) {
            return;
        }
        NativeServiceSymbols.publisherDestroy(handle);
        handle = MemorySegment.NULL;
    }
}

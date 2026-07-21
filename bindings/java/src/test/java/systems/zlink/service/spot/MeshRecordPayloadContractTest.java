package systems.zlink.service.spot;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.lang.foreign.Arena;
import java.lang.foreign.MemoryLayout;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.service.spot.ActorControlRecord;
import systems.zlink.contracts.service.spot.ActorJoinCompletion;
import systems.zlink.contracts.service.spot.ActorRef;
import systems.zlink.contracts.service.spot.MeshDestinationKind;
import systems.zlink.contracts.service.spot.MeshRecordPayload;
import systems.zlink.contracts.service.spot.MeshSendReadyData;
import systems.zlink.contracts.service.spot.OperationKind;
import systems.zlink.contracts.service.spot.RecordKind;
import systems.zlink.contracts.service.spot.ReceiveRecord;
import systems.zlink.runtime.nativeapi.NativeLayouts;
import systems.zlink.runtime.nativeapi.NativeRoutingIds;
import systems.zlink.runtime.nativeapi.ServiceInterop;
import systems.zlink.runtime.nativeapi.ServiceLayouts;

class MeshRecordPayloadContractTest {
    @Test
    void cHeaderOffsetsMatchTheJavaReceiveAndSendReadyLayouts() throws Exception {
        Path output = Files.createTempFile("zlink-mesh-layout-", ".o");
        try {
            Process compiler = new ProcessBuilder(
                "cc",
                "-std=c11",
                "-I../../core/include",
                "-c",
                "src/test/native/mesh_receive_record_layout.c",
                "-o",
                output.toString())
                .redirectErrorStream(true)
                .start();
            String diagnostics = new String(
                compiler.getInputStream().readAllBytes(), StandardCharsets.UTF_8);
            assertEquals(0, compiler.waitFor(), diagnostics);
        } finally {
            Files.deleteIfExists(output);
        }
    }

    @Test
    void receiveRecordExposesTypedActorControlAndJoinCompletionPayloads() throws Exception {
        assertEquals(
            MeshRecordPayload.class,
            ReceiveRecord.class.getMethod("kindData").getReturnType());
        assertEquals(
            ActorControlRecord.class,
            ReceiveRecord.class.getMethod("actorControl").getReturnType());
        assertEquals(
            ActorJoinCompletion.class,
            ReceiveRecord.class.getMethod("joinCompletion").getReturnType());
        assertEquals(
            MeshSendReadyData.class,
            ReceiveRecord.class.getMethod("sendReady").getReturnType());
        assertTrue(MeshRecordPayload.class.isSealed());
    }

    @Test
    void sendReadyKindDataPreservesTheExactMeshDestination() {
        RoutingId nodeRid = RoutingId.from("node-a");
        RoutingId spotRid = RoutingId.from("spot-a");
        ActorRef actor = new ActorRef(nodeRid, "actor-a", 7L);
        byte[] channel = "orders".getBytes(StandardCharsets.UTF_8);

        try (Arena arena = Arena.ofConfined()) {
            MemoryLayout payloadLayout = ServiceLayouts.MESH_SEND_READY_DATA;
            assertEquals(1064L, payloadLayout.byteSize());
            assertEquals(8L, ServiceLayouts.off(payloadLayout, "destination_kind"));
            assertEquals(12L, ServiceLayouts.off(payloadLayout, "target_node_rid"));
            assertEquals(268L, ServiceLayouts.off(payloadLayout, "target_spot_rid"));
            assertEquals(528L, ServiceLayouts.off(payloadLayout, "target_actor"));
            assertEquals(1048L, ServiceLayouts.off(payloadLayout, "channel_name"));
            assertEquals(1056L, ServiceLayouts.off(payloadLayout, "channel_name_size"));
            MemorySegment payload = ServiceInterop.allocStamped(arena, payloadLayout);
            payload.set(ValueLayout.JAVA_INT_UNALIGNED,
                ServiceLayouts.off(payloadLayout, "destination_kind"),
                MeshDestinationKind.ACTOR.value());
            NativeRoutingIds.write(payload.asSlice(
                ServiceLayouts.off(payloadLayout, "target_node_rid"),
                NativeLayouts.ROUTING_ID_LAYOUT.byteSize()), nodeRid);
            NativeRoutingIds.write(payload.asSlice(
                ServiceLayouts.off(payloadLayout, "target_spot_rid"),
                NativeLayouts.ROUTING_ID_LAYOUT.byteSize()), spotRid);
            ServiceInterop.writeActorRef(payload.asSlice(
                ServiceLayouts.off(payloadLayout, "target_actor"),
                NativeLayouts.ACTOR_REF_LAYOUT.byteSize()), actor);
            MemorySegment channelBytes = arena.allocate(channel.length);
            MemorySegment.copy(MemorySegment.ofArray(channel), 0, channelBytes, 0,
                channel.length);
            payload.set(ValueLayout.ADDRESS,
                ServiceLayouts.off(payloadLayout, "channel_name"), channelBytes);
            payload.set(ValueLayout.JAVA_LONG_UNALIGNED,
                ServiceLayouts.off(payloadLayout, "channel_name_size"), channel.length);

            MemoryLayout recordLayout = ServiceLayouts.RECEIVE_RECORD;
            assertEquals(1200L, recordLayout.byteSize());
            assertEquals(528L,
                ServiceLayouts.off(recordLayout, "source_binding_generation"));
            assertEquals(536L, ServiceLayouts.off(recordLayout, "source_actor"));
            assertEquals(1056L, ServiceLayouts.off(recordLayout, "operation_id_high"));
            assertEquals(1160L, ServiceLayouts.off(recordLayout, "kind_data"));
            assertEquals(1176L, ServiceLayouts.off(recordLayout, "part_offset"));
            assertEquals(1184L, ServiceLayouts.off(recordLayout, "part_count"));
            assertEquals(1192L, ServiceLayouts.off(recordLayout, "terminal_result"));
            MemorySegment nativeRecord = ServiceInterop.allocStamped(arena, recordLayout);
            nativeRecord.set(ValueLayout.JAVA_INT_UNALIGNED,
                ServiceLayouts.off(recordLayout, "kind"), RecordKind.SEND_READY.value());
            nativeRecord.set(ValueLayout.JAVA_INT_UNALIGNED,
                ServiceLayouts.off(recordLayout, "operation_kind"), OperationKind.NONE.value());
            nativeRecord.set(ValueLayout.JAVA_LONG_UNALIGNED,
                ServiceLayouts.off(recordLayout, "source_binding_generation"), 17L);
            nativeRecord.set(ValueLayout.ADDRESS,
                ServiceLayouts.off(recordLayout, "kind_data"), payload);
            nativeRecord.set(ValueLayout.JAVA_LONG_UNALIGNED,
                ServiceLayouts.off(recordLayout, "kind_data_size"), payload.byteSize());

            ReceiveRecord record = ServiceInterop.receiveRecordFromNative(nativeRecord);
            assertEquals(17L, record.sourceBindingGeneration());
            MeshSendReadyData decoded = record.sendReady();
            assertEquals(MeshDestinationKind.ACTOR, decoded.destinationKind());
            assertEquals(nodeRid, decoded.targetNodeRid());
            assertEquals(spotRid, decoded.targetSpotRid());
            assertEquals(actor, decoded.targetActor());
            assertEquals("orders", decoded.channelName());
        }
    }
}

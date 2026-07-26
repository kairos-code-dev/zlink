package systems.zlink.framework.runtime.internal.locations;

import java.io.ByteArrayOutputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.Objects;
import java.util.UUID;
import java.util.zip.CRC32C;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.locations.ZLinkRelocationStored;

/** Reads and replaces the canonical relocation slot in authority-payload-v1. */
final class ZLinkCanonicalRelocationAuthorityStateCodec {
    private static final byte[] MAGIC = {0x5a, 0x4c, 0x41, 0x55};
    private static final byte[] EMPTY = {0, 0, 0, 0, 0};

    private ZLinkCanonicalRelocationAuthorityStateCodec() {
    }

    static byte[] publish(
        byte[] authorityPayload,
        ZLinkAggregateRelocationCoordinator.Request request,
        ZLinkRelocationStored stored,
        boolean sourceCleanupCompleted) {
        Owner source = owner(authorityPayload);
        var root = ZLinkServiceRelocationEnvelopeCodec.decode(request.root());
        if (root.relocationHigh() != request.aggregateId().getMostSignificantBits()
            || root.relocationLow() != request.aggregateId().getLeastSignificantBits()) {
            throw new IllegalArgumentException(
                "authority relocation identity differs from canonical root");
        }
        Writer body = new Writer();
        body.u64(root.relocationHigh());
        body.u64(root.relocationLow());
        body.u64(request.aggregateGeneration());
        body.sized8(source.nodeRid().toBytes());
        body.u64(source.nodeGeneration());
        body.text8(source.ownerId());
        body.u64(source.ownerLeaseGeneration());
        body.sized8(request.targetDescriptor().rid().toBytes());
        body.u64(request.targetDescriptorLifecycleGeneration());
        body.text8(request.targetOwner().ownerId());
        body.u64(request.targetOwner().leaseGeneration());
        body.u64(0);
        body.text8(request.targetOwner().ownerId());
        body.u64(request.targetOwner().leaseGeneration());
        body.sized8(request.targetDescriptor().rid().toBytes());
        body.u64(request.targetDescriptorLifecycleGeneration());
        body.u8(sourceCleanupCompleted ? 8 : 4);
        Writer pointer = new Writer();
        pointer.text16(stored.reference());
        pointer.u32(stored.checksumCrc32c());
        body.u8(1);
        body.u16(pointer.size());
        body.raw(pointer.bytes());
        body.u64(root.applicationVersion());
        body.u32(root.participantProgress().size());
        for (var progress : root.participantProgress()) {
            body.u64(progress.participantId());
            body.u64(progress.acceptedBoundary());
            body.u64(progress.replayCursor());
        }
        body.u32(root.terminalCompletions().size());
        body.u32(root.terminalCompletions().stream()
            .filter(value -> value.deliveryState() == 0).count());
        body.u8(sourceCleanupCompleted ? 1 : 0);
        Writer state = new Writer();
        state.u8(1);
        state.u32(body.size());
        state.raw(body.bytes());
        return replace(authorityPayload, state.bytes());
    }

    static Published decode(byte[] authorityPayload) {
        try {
            Slot slot = slot(authorityPayload);
            Reader state = new Reader(slot.state());
            if (state.u8() != 1) return null;
            Reader body = state.reader(state.u32());
            if (!state.end()) return null;
            UUID relocationId = new UUID(body.u64(), body.u64());
            long generation = body.u64();
            body.sized8(); body.u64(); body.text8(); body.u64();
            body.sized8(); body.u64();
            String targetOwnerId = body.text8();
            long targetOwnerLeaseGeneration = body.u64();
            body.u64(); body.text8(); body.u64(); body.sized8(); body.u64();
            body.u8();
            if (body.u8() != 1) return null;
            Reader pointer = body.reader(body.u16());
            String reference = pointer.text16();
            long checksum = pointer.u32Unsigned();
            if (!pointer.end()) return null;
            body.u64();
            int progress = body.u32();
            for (int index = 0; index < progress; index++) {
                body.u64(); body.u64(); body.u64();
            }
            body.u32(); body.u32();
            boolean sourceCleanupCompleted = body.u8() == 1;
            if (!body.end()) return null;
            return new Published(
                reference, checksum, relocationId, generation,
                targetOwnerId, targetOwnerLeaseGeneration,
                replace(authorityPayload, EMPTY),
                sourceCleanupCompleted);
        } catch (RuntimeException invalid) {
            return null;
        }
    }

    private static Owner owner(byte[] payload) {
        Slot slot = slot(payload);
        Reader body = new Reader(slot.body());
        body.u8(); body.u8(); body.skip(body.u16());
        String ownerId = body.text8();
        long lease = body.u64();
        body.text8();
        RoutingId nodeRid = RoutingId.from(body.sized8());
        long nodeGeneration = body.u64();
        return new Owner(ownerId, lease, nodeRid, nodeGeneration);
    }

    private static byte[] replace(byte[] payload, byte[] state) {
        Slot slot = slot(payload);
        byte[] nextBody = new byte[
            slot.start() + state.length + slot.body().length - slot.end()];
        System.arraycopy(slot.body(), 0, nextBody, 0, slot.start());
        System.arraycopy(state, 0, nextBody, slot.start(), state.length);
        System.arraycopy(slot.body(), slot.end(), nextBody,
            slot.start() + state.length, slot.body().length - slot.end());
        Writer envelope = new Writer();
        envelope.raw(MAGIC); envelope.u8(1); envelope.u16(0);
        envelope.u32(nextBody.length); envelope.raw(nextBody);
        byte[] withoutChecksum = envelope.bytes();
        envelope.u32(crc32c(withoutChecksum));
        return envelope.bytes();
    }

    private static Slot slot(byte[] payload) {
        Reader envelope = new Reader(payload);
        envelope.expect(MAGIC);
        if (envelope.u8() != 1 || envelope.u16() != 0) throw invalid();
        Reader body = envelope.reader(envelope.u32());
        int checksumOffset = envelope.offset();
        if (envelope.u32Unsigned() != crc32c(payload, checksumOffset)
            || !envelope.end()) throw invalid();
        body.u8(); body.u8(); body.skip(body.u16());
        body.text8(); body.u64(); body.text8(); body.sized8(); body.u64();
        int start = body.offset();
        body.u8();
        int size = body.u32();
        body.skip(size);
        int end = body.offset();
        return new Slot(body.bytes(), start, end,
            java.util.Arrays.copyOfRange(body.bytes(), start, end));
    }

    record Published(
        String reference,
        long checksumCrc32c,
        UUID aggregateId,
        long aggregateGeneration,
        String targetOwnerId,
        long targetOwnerLeaseGeneration,
        byte[] applicationPayload,
        boolean sourceCleanupCompleted) {
        Published { applicationPayload = applicationPayload.clone(); }
        @Override public byte[] applicationPayload() {
            return applicationPayload.clone();
        }
    }
    private record Owner(String ownerId, long ownerLeaseGeneration,
                         RoutingId nodeRid, long nodeGeneration) {}
    private record Slot(byte[] body, int start, int end, byte[] state) {}

    private static long crc32c(byte[] bytes) {
        return crc32c(bytes, bytes.length);
    }
    private static long crc32c(byte[] bytes, int length) {
        CRC32C crc = new CRC32C(); crc.update(bytes, 0, length); return crc.getValue();
    }
    private static IllegalArgumentException invalid() {
        return new IllegalArgumentException("invalid canonical authority payload");
    }

    private static final class Writer {
        private final ByteArrayOutputStream out = new ByteArrayOutputStream();
        void u8(long v) { out.write((int) v); }
        void u16(long v) { out.write((int) (v >>> 8)); out.write((int) v); }
        void u32(long v) { raw(ByteBuffer.allocate(4).order(ByteOrder.BIG_ENDIAN).putInt((int) v).array()); }
        void u64(long v) { raw(ByteBuffer.allocate(8).order(ByteOrder.BIG_ENDIAN).putLong(v).array()); }
        void text8(String v) { sized8(v.getBytes(StandardCharsets.UTF_8)); }
        void text16(String v) { byte[] b=v.getBytes(StandardCharsets.UTF_8); u16(b.length); raw(b); }
        void sized8(byte[] b) { if (b.length<1||b.length>255) throw invalid(); u8(b.length); raw(b); }
        void raw(byte[] b) { out.writeBytes(b); }
        int size() { return out.size(); }
        byte[] bytes() { return out.toByteArray(); }
    }
    private static final class Reader {
        private final byte[] bytes; private int offset;
        Reader(byte[] bytes) { this.bytes=Objects.requireNonNull(bytes); }
        void expect(byte[] v) { for(byte b:v) if(u8()!=Byte.toUnsignedInt(b)) throw invalid(); }
        int u8(){ require(1); return Byte.toUnsignedInt(bytes[offset++]); }
        int u16(){ require(2); int v=(u8()<<8)|u8(); return v; }
        int u32(){ long v=u32Unsigned(); if(v>Integer.MAX_VALUE) throw invalid(); return (int)v; }
        long u32Unsigned(){ require(4); long v=Integer.toUnsignedLong(ByteBuffer.wrap(bytes,offset,4).order(ByteOrder.BIG_ENDIAN).getInt()); offset+=4; return v; }
        long u64(){ require(8); long v=ByteBuffer.wrap(bytes,offset,8).order(ByteOrder.BIG_ENDIAN).getLong(); offset+=8; return v; }
        String text8(){ return new String(sized8(), StandardCharsets.UTF_8); }
        String text16(){ int n=u16(); if(n==0) throw invalid(); return new String(take(n),StandardCharsets.UTF_8); }
        byte[] sized8(){ int n=u8(); if(n==0) throw invalid(); return take(n); }
        Reader reader(int n){ return new Reader(take(n)); }
        void skip(int n){ take(n); }
        byte[] take(int n){ require(n); byte[] v=java.util.Arrays.copyOfRange(bytes,offset,offset+n); offset+=n; return v; }
        int offset(){ return offset; }
        byte[] bytes(){ return bytes; }
        boolean end(){ return offset==bytes.length; }
        void require(int n){ if(n<0||offset+n>bytes.length) throw invalid(); }
    }
}

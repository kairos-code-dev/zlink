package systems.zlink.framework.runtime.binding;

import java.util.Objects;
import java.util.function.Consumer;
import java.util.function.IntUnaryOperator;
import systems.zlink.contracts.service.spot.Claim;
import systems.zlink.contracts.service.spot.ClaimRecvResult;
import systems.zlink.contracts.service.spot.DrainResult;
import systems.zlink.contracts.service.spot.MeshNode;
import systems.zlink.contracts.service.spot.ReadyBatch;
import systems.zlink.contracts.service.spot.ReadyRecord;
import systems.zlink.contracts.service.spot.ReceiveBatch;
import systems.zlink.contracts.service.spot.ReceiveRequirements;
import systems.zlink.contracts.sockets.RecvFlags;
import systems.zlink.framework.runtime.internal.backend.ZLinkMeshDispatchRecord;

/**
 * Owns native reusable dispatch batches and hides claim/buffer management from
 * the framework scheduler.
 */
final class ZLinkJavaMeshDispatchSource implements ZLinkJavaMeshDispatchPump.Source {
    private static final int RECEIVE_OK = 0;
    private static final int RECEIVE_BUFFER_TOO_SMALL = 207;
    private static final int READY_CAPACITY = 64;
    private static final int MESSAGE_CAPACITY = 64;
    private static final int PART_CAPACITY = 256;
    private static final int BYTE_CAPACITY = 1024 * 1024;

    private final MeshNode node;
    private ReadyBatch readyBatch = ReadyBatch.create(READY_CAPACITY);
    private ReceiveBatch receiveBatch =
        ReceiveBatch.create(MESSAGE_CAPACITY, PART_CAPACITY, BYTE_CAPACITY);

    ZLinkJavaMeshDispatchSource(MeshNode node) {
        this.node = Objects.requireNonNull(node, "node");
    }

    @Override
    public void setReadyHandler(IntUnaryOperator handler) {
        node.setReadyHandler(handler == null ? null : handler::applyAsInt);
    }

    @Override
    public boolean drain(
        int domains,
        Consumer<ZLinkMeshDispatchRecord> receiver) {
        readyBatch.reset();
        DrainResult result = node.drainReady(domains, readyBatch, RecvFlags.DONT_WAIT);
        if (result.resultCode() != RECEIVE_OK) {
            throw new IllegalStateException(
                "MeshNode ready drain failed: " + result.resultCode());
        }
        for (int index = 0; index < readyBatch.count(); index++) {
            ReadyRecord owner = readyBatch.at(index);
            try (Claim claim = readyBatch.takeClaim(index)) {
                drainClaim(owner, claim, receiver);
            }
        }
        return result.hasResidue();
    }

    private void drainClaim(
        ReadyRecord owner,
        Claim claim,
        Consumer<ZLinkMeshDispatchRecord> receiver) {
        receiveBatch.reset();
        ClaimRecvResult result = claim.recvBatch(receiveBatch, RecvFlags.DONT_WAIT);
        if (result.resultCode() == RECEIVE_BUFFER_TOO_SMALL) {
            resizeReceiveBatch(result.required());
            result = claim.recvBatch(receiveBatch, RecvFlags.DONT_WAIT);
        }
        if (result.resultCode() != RECEIVE_OK) {
            throw new IllegalStateException(
                "MeshNode claim receive failed: " + result.resultCode());
        }
        for (int index = 0; index < receiveBatch.count(); index++) {
            ZLinkMeshDispatchRecord record = new ZLinkMeshDispatchRecord(
                owner,
                receiveBatch.at(index),
                receiveBatch.retainMessage(index));
            boolean accepted = false;
            try {
                receiver.accept(record);
                accepted = true;
            } finally {
                if (!accepted) {
                    record.close();
                }
            }
        }
    }

    private void resizeReceiveBatch(ReceiveRequirements required) {
        receiveBatch.close();
        receiveBatch = ReceiveBatch.create(
            exactCapacity(required.messageCount(), "message"),
            exactCapacity(required.partCount(), "part"),
            exactCapacity(required.byteCount(), "byte"));
    }

    private static int exactCapacity(long value, String label) {
        if (value <= 0 || value > Integer.MAX_VALUE) {
            throw new IllegalStateException(
                "invalid required " + label + " capacity: " + value);
        }
        return (int) value;
    }

    @Override
    public void close() {
        node.setReadyHandler(null);
        receiveBatch.close();
        readyBatch.close();
    }
}

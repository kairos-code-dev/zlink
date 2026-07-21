/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.perf;

import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.service.spot.Claim;
import systems.zlink.contracts.service.spot.MeshNode;
import systems.zlink.contracts.service.spot.ReadyBatch;
import systems.zlink.contracts.service.spot.ReadyDomain;
import systems.zlink.contracts.service.spot.ReadyRecord;
import systems.zlink.contracts.service.spot.ReceiveBatch;
import systems.zlink.contracts.service.spot.ReceiveRecord;
import systems.zlink.contracts.sockets.RecvFlags;
import java.util.List;

/**
 * Reusable pull-dispatch buffers for binding benchmarks.
 *
 * <p>The benchmark owns one instance per MeshNode. Calls are serialized because
 * a MeshNode ready claim is exclusive and the reusable batches are not
 * thread-safe.
 */
public final class PerfMeshDispatch implements AutoCloseable {
    @FunctionalInterface
    public interface RecordHandler {
        void handle(ReadyRecord owner, ReceiveRecord record, List<Message> parts);
    }

    private final MeshNode node;
    private final ReadyBatch ready;
    private final ReceiveBatch received;

    public PerfMeshDispatch(MeshNode node, int recordCapacity) {
        this.node = node;
        this.ready = ReadyBatch.create(Math.max(8, recordCapacity));
        this.received = ReceiveBatch.create(
            Math.max(32, recordCapacity * 4),
            Math.max(128, recordCapacity * 8),
            1 << 20);
    }

    public synchronized int drain(RecordHandler handler) {
        int handled = 0;
        ready.reset();
        node.drainReady(ReadyDomain.mask(ReadyDomain.ALL), ready, RecvFlags.DONT_WAIT);
        for (int i = 0; i < ready.count(); i++) {
            ReadyRecord owner = ready.at(i);
            try (Claim claim = ready.takeClaim(i)) {
                while (claim.valid()) {
                    received.reset();
                    if (claim.recvBatch(received, RecvFlags.DONT_WAIT).resultCode() != 0) {
                        break;
                    }
                    for (int r = 0; r < received.count(); r++) {
                        ReceiveRecord record = received.at(r);
                        List<Message> parts = record.partCount() == 0
                            ? List.of()
                            : received.retainMessage(r);
                        try {
                            handler.handle(owner, record, parts);
                            handled++;
                        } finally {
                            parts.forEach(Message::close);
                        }
                    }
                }
            }
        }
        return handled;
    }

    @Override
    public void close() {
        received.close();
        ready.close();
    }
}

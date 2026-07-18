/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.runtime.nativeapi.InternalAccess;

/** A reusable buffer that receives dispatch ready records from a mesh node. */
public interface ReadyBatch extends AutoCloseable {
    /** Returns the number of ready records currently held. */
    int count();

    /** Returns the ready record at the given index. */
    ReadyRecord at(int index);

    /** Clears the batch for reuse. */
    void reset();

    /** Takes ownership of the pending work for the record at the given index. */
    Claim takeClaim(int index);

    @Override
    void close();

    /** Creates a ready batch with the given record capacity. */
    static ReadyBatch create(int recordCapacity) {
        return InternalAccess.dispatchNewReadyBatch(recordCapacity);
    }
}

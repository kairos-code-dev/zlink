/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.service.spot;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.util.List;
import java.util.Objects;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.contracts.service.spot.SendOperation;
import systems.zlink.contracts.service.spot.SpotNode;
import systems.zlink.contracts.service.spot.SpotNodePublisher;
import systems.zlink.runtime.messaging.MessageOperations;
import systems.zlink.runtime.nativeapi.InternalAccess;
import systems.zlink.runtime.nativeapi.Native;
import systems.zlink.runtime.nativeapi.NativeHelpers;
import systems.zlink.runtime.nativeapi.NativeMessage;
import systems.zlink.runtime.nativeapi.RoutedRequestSupport;

final class NativeSpotNodePublisher implements SpotNodePublisher {
    private MemorySegment handle;

    NativeSpotNodePublisher(SpotNode node) {
        Objects.requireNonNull(node, "node");
        handle = Native.spotNodePublisherNew(InternalAccess.spotNodeHandle(node));
        if (handle == null || handle.address() == 0) {
            throw InternalAccess.zlinkExceptionFromLastError(
                systems.zlink.contracts.errors.ErrorCategory.CONFIG);
        }
    }

    @Override
    public SendOperation publish(String topicId) {
        String topic = requireTopic(topicId);
        return MessageOperations.send((parts, flags) ->
            publishInternal(topic, parts, flags));
    }

    @Override
    public void close() {
        MemorySegment current = handle;
        if (current == null || current.address() == 0) {
            return;
        }
        handle = MemorySegment.NULL;
        int rc = Native.spotNodePublisherClose(current);
        if (rc != 0) {
            throw InternalAccess.zlinkExceptionFromLastError(
                systems.zlink.contracts.errors.ErrorCategory.CLOSE);
        }
    }

    private boolean publishInternal(String topic, List<Message> parts,
                                    SendFlags flags) {
        Objects.requireNonNull(flags, "flags");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeParts =
                RoutedRequestSupport.movePayloadToNative(arena, parts);
            int rc = Native.spotNodePublisherPublish(handle(),
                NativeHelpers.toCString(arena, topic), nativeParts,
                parts.size(), flags.value());
            if (rc != 0) {
                NativeMessage.multipartClose(nativeParts, parts.size());
                throw InternalAccess.zlinkExceptionFromLastError(
                    systems.zlink.contracts.errors.ErrorCategory.SUBMIT);
            }
            return true;
        }
    }

    private MemorySegment handle() {
        if (handle == null || handle.address() == 0) {
            throw new IllegalStateException("spot node publisher is closed");
        }
        return handle;
    }

    private static String requireTopic(String topicId) {
        Objects.requireNonNull(topicId, "topicId");
        if (topicId.isEmpty()) {
            throw new IllegalArgumentException("topicId must not be empty");
        }
        return topicId;
    }
}

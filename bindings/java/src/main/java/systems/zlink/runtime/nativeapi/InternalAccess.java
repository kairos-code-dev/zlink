/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.nativeapi;

import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.errors.ZlinkException;
import systems.zlink.contracts.eventing.Timer;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.messaging.Received;
import systems.zlink.contracts.messaging.TopicMessage;
import systems.zlink.contracts.service.discovery.Discovery;
import systems.zlink.contracts.service.spot.ActorPart;
import systems.zlink.contracts.service.spot.Spot;
import systems.zlink.contracts.service.spot.SpotNode;
import systems.zlink.contracts.sockets.RequestCallback;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.contracts.sockets.Socket;
import systems.zlink.contracts.sockets.SpotDispatchEvent;
import systems.zlink.contracts.sockets.SpotDispatchInfo;
import systems.zlink.contracts.sockets.SpotDispatchSubjectKind;
import java.lang.foreign.MemorySegment;
import java.time.Duration;
import java.util.List;
import java.util.Objects;
import java.util.function.BiConsumer;
import java.util.function.BiFunction;

/**
 * Non-exported bridge from runtime code to contract-owned internals.
 *
 * <p>Contract classes register their own accessors from inside the defining
 * package. This keeps implementation hooks out of the public API without using
 * reflection or MethodHandle-based private access.
 */
public final class InternalAccess {
    private static volatile ContextAccess contextAccess;
    private static volatile DiscoveryAccess discoveryAccess;
    private static volatile SocketAccess socketAccess;
    private static volatile SpotAccess spotAccess;
    private static volatile SpotNodeAccess spotNodeAccess;
    private static volatile SpotDispatchInfoAccess spotDispatchInfoAccess;
    private static volatile TimerAccess timerAccess;
    private static volatile MessageAccess messageAccess;
    private static volatile ReceivedAccess receivedAccess;
    private static volatile TopicMessageAccess topicMessageAccess;
    private static volatile RoutingIdAccess routingIdAccess;
    private static volatile ErrorAccess errorAccess;

    private InternalAccess() {
    }

    public interface ContextAccess {
        MemorySegment handle(Context context);
    }

    public interface DiscoveryAccess {
        MemorySegment handle(Discovery discovery);
    }

    public interface SocketAccess {
        MemorySegment handle(Socket socket);
        boolean inCallback();
        void enterCallback();
        void leaveCallback();
    }

    public interface SpotAccess {
        MemorySegment handle(Spot spot);

        boolean requestToSpotPart(Spot spot, RoutingId destNodeRid,
                                  RoutingId destSpotRid, Message part,
                                  RequestCallback callback, SendFlags flags,
                                  Duration timeout);
    }

    public interface SpotNodeAccess {
        MemorySegment handle(SpotNode node);
    }

    public interface SpotDispatchInfoAccess {
        MemorySegment subject(SpotDispatchInfo info);

        SpotDispatchInfo create(SpotDispatchEvent event,
                                SpotDispatchSubjectKind subjectKind,
                                MemorySegment subject);

        SpotDispatchInfo create(SpotDispatchEvent event,
                                SpotDispatchSubjectKind subjectKind,
                                MemorySegment subject,
                                List<ActorPart> actorParts);

        SpotDispatchInfo create(SpotDispatchEvent event,
                                SpotDispatchSubjectKind subjectKind,
                                MemorySegment subject,
                                Timer timer,
                                String channelName,
                                List<ActorPart> actorParts);
    }

    public interface TimerAccess {
        Timer fromBorrowedHandle(MemorySegment handle);
    }

    public interface MessageAccess {
        MemorySegment dataSegment(Message message);
        MemorySegment dataSegment(Message message, int knownSize);
        void copyTo(Message message, MemorySegment destination);
        void moveTo(Message message, MemorySegment destination);
        MemorySegment nativeHandle(Message message);
        void setMore(Message message, boolean more);
        boolean more(Message message);
        void finishReceive(Message message, boolean more);
        void transferTo(Message message, MemorySegment destination);
        void restoreFromNative(Message message, MemorySegment source,
                               boolean moreFlag);
        void markTransferred(Message message);
        int moveInto(Message source, Message target, boolean moreFlag);
        Message sharedCopyOf(Message message);
        Message[] fromMsgVector(MemorySegment partsAddr, long count);
        Message[] fromOwnedMsgVector(MemorySegment partsAddr, long count);
        Message[] fromOwnedMsgVectorShared(MemorySegment partsAddr, long count);
        Message fromOwnedNative(MemorySegment nativeMsg);
    }

    public interface ReceivedAccess {
        Received create(RoutingId routingId, Message[] parts);
        Received create(RoutingId routingId, RoutingId spotRid,
                        Message[] parts, boolean trustedParts,
                        long requestSeq, boolean hasRequestSeq,
                        BiConsumer<List<Message>, SendFlags> replySender);

        Received create(RoutingId routingId, RoutingId spotRid,
                        Message[] parts, boolean trustedParts,
                        long requestSeq, boolean hasRequestSeq,
                        BiConsumer<List<Message>, SendFlags> replySender,
                        Runnable onTerminalState);

        Received create(byte[] routingIdBytes, byte[] spotRidBytes,
                        Message[] parts, boolean trustedParts,
                        long requestSeq, boolean hasRequestSeq,
                        BiConsumer<List<Message>, SendFlags> replySender,
                        Runnable onTerminalState);

        Received create(RoutingId routingId, RoutingId spotRid,
                        Message[] parts, long requestSeq,
                        boolean hasRequestSeq,
                        BiConsumer<List<Message>, SendFlags> replySender);

        Received createLazy(byte[] routingIdBytes, byte[] spotRidBytes,
                            Message firstPart, ReceivedPartCursor cursor,
                            long requestSeq, boolean hasRequestSeq,
                            BiConsumer<List<Message>, SendFlags> replySender,
                            Runnable onTerminalState);

        Received createLazy(RoutingId routingId, RoutingId spotRid,
                            Message firstPart, ReceivedPartCursor cursor,
                            long requestSeq, boolean hasRequestSeq,
                            BiConsumer<List<Message>, SendFlags> replySender,
                            Runnable onTerminalState);

        void forceMaterialize(Received received);
        List<Message> takeParts(Received received);
        void setSendSender(Received received,
                           BiFunction<List<Message>, SendFlags, Boolean> sendSender);
    }

    public interface TopicMessageAccess {
        TopicMessage create(RoutingId routingId, String topicId,
                            Message[] parts);
        void adoptSingle(TopicMessage target, RoutingId routingId,
                         String topicId, Message part);
        Message prepareReusableSinglePart(TopicMessage target);
    }

    public interface RoutingIdAccess {
        RoutingId fromTrusted(byte[] value);
        byte[] trustedBytes(RoutingId routingId);
    }

    public interface ErrorAccess {
        ZlinkException fromLastError(String operation);
        ZlinkException fromErrno(String operation, int errno);
    }

    public static void register(ContextAccess access) {
        contextAccess = Objects.requireNonNull(access, "access");
    }

    public static void register(DiscoveryAccess access) {
        discoveryAccess = Objects.requireNonNull(access, "access");
    }

    public static void register(SocketAccess access) {
        socketAccess = Objects.requireNonNull(access, "access");
    }

    public static void register(SpotAccess access) {
        spotAccess = Objects.requireNonNull(access, "access");
    }

    public static void register(SpotNodeAccess access) {
        spotNodeAccess = Objects.requireNonNull(access, "access");
    }

    public static void register(SpotDispatchInfoAccess access) {
        spotDispatchInfoAccess = Objects.requireNonNull(access, "access");
    }

    public static void register(TimerAccess access) {
        timerAccess = Objects.requireNonNull(access, "access");
    }

    public static void register(MessageAccess access) {
        messageAccess = Objects.requireNonNull(access, "access");
    }

    public static void register(ReceivedAccess access) {
        receivedAccess = Objects.requireNonNull(access, "access");
    }

    public static void register(TopicMessageAccess access) {
        topicMessageAccess = Objects.requireNonNull(access, "access");
    }

    public static void register(RoutingIdAccess access) {
        routingIdAccess = Objects.requireNonNull(access, "access");
    }

    public static void register(ErrorAccess access) {
        errorAccess = Objects.requireNonNull(access, "access");
    }

    public static MemorySegment contextHandle(Context context) {
        return contextAccess().handle(context);
    }

    public static MemorySegment discoveryHandle(Discovery discovery) {
        return discoveryAccess().handle(discovery);
    }

    public static MemorySegment socketHandle(Socket socket) {
        return socketAccess().handle(socket);
    }

    public static MemorySegment spotHandle(Spot spot) {
        return spotAccess().handle(spot);
    }

    public static MemorySegment spotNodeHandle(SpotNode node) {
        return spotNodeAccess().handle(node);
    }

    public static boolean spotRequestToSpotPart(Spot spot,
                                                RoutingId destNodeRid,
                                                RoutingId destSpotRid,
                                                Message part,
                                                RequestCallback callback,
                                                SendFlags flags,
                                                Duration timeout) {
        return spotAccess().requestToSpotPart(spot, destNodeRid, destSpotRid,
            part, callback, flags, timeout);
    }

    public static MemorySegment spotDispatchSubject(SpotDispatchInfo info) {
        return spotDispatchInfoAccess().subject(info);
    }

    public static SpotDispatchInfo spotDispatchInfo(
      SpotDispatchEvent event,
      SpotDispatchSubjectKind subjectKind,
      MemorySegment subject) {
        return spotDispatchInfoAccess().create(event, subjectKind, subject);
    }

    public static SpotDispatchInfo spotDispatchInfo(
      SpotDispatchEvent event,
      SpotDispatchSubjectKind subjectKind,
      MemorySegment subject,
      List<ActorPart> actorParts) {
        return spotDispatchInfoAccess().create(event, subjectKind, subject,
            actorParts);
    }

    public static SpotDispatchInfo spotDispatchInfo(
      SpotDispatchEvent event,
      SpotDispatchSubjectKind subjectKind,
      MemorySegment subject,
      Timer timer,
      String channelName,
      List<ActorPart> actorParts) {
        return spotDispatchInfoAccess().create(event, subjectKind, subject,
            timer, channelName, actorParts);
    }

    public static Timer timerFromBorrowedHandle(MemorySegment handle) {
        return timerAccess().fromBorrowedHandle(handle);
    }

    public static MemorySegment messageDataSegment(Message message) {
        return messageAccess().dataSegment(message);
    }

    public static MemorySegment messageDataSegment(Message message,
                                                   int knownSize) {
        return messageAccess().dataSegment(message, knownSize);
    }

    public static void messageCopyTo(Message message,
                                     MemorySegment destination) {
        messageAccess().copyTo(message, destination);
    }

    public static void messageMoveTo(Message message,
                                     MemorySegment destination) {
        messageAccess().moveTo(message, destination);
    }

    public static MemorySegment messageNativeHandle(Message message) {
        return messageAccess().nativeHandle(message);
    }

    public static void messageSetMore(Message message, boolean more) {
        messageAccess().setMore(message, more);
    }

    public static boolean messageMore(Message message) {
        return messageAccess().more(message);
    }

    public static void messageFinishReceive(Message message, boolean more) {
        messageAccess().finishReceive(message, more);
    }

    public static Object messageTransferTo(Message message,
                                           MemorySegment destination) {
        messageAccess().transferTo(message, destination);
        return null;
    }

    public static void messageRestoreFromNative(Message message,
                                                MemorySegment source,
                                                boolean moreFlag,
                                                Object anchor) {
        messageAccess().restoreFromNative(message, source, moreFlag);
    }

    public static void messageMarkTransferred(Message message) {
        messageAccess().markTransferred(message);
    }

    public static int messageMoveInto(Message source, Message target,
                                      boolean moreFlag) {
        return messageAccess().moveInto(source, target, moreFlag);
    }

    public static Message messageSharedCopyOf(Message message) {
        return messageAccess().sharedCopyOf(message);
    }

    public static Message[] messageFromMsgVector(MemorySegment partsAddr,
                                                 long count) {
        return messageAccess().fromMsgVector(partsAddr, count);
    }

    public static Message[] messageFromOwnedMsgVector(MemorySegment partsAddr,
                                                      long count) {
        return messageAccess().fromOwnedMsgVector(partsAddr, count);
    }

    public static Message[] messageFromOwnedMsgVectorShared(
      MemorySegment partsAddr,
      long count) {
        return messageAccess().fromOwnedMsgVectorShared(partsAddr, count);
    }

    public static Message messageFromOwnedNative(MemorySegment nativeMsg) {
        return messageAccess().fromOwnedNative(nativeMsg);
    }

    public static Received received(RoutingId routingId, Message[] parts) {
        return receivedAccess().create(routingId, parts);
    }

    public static Received received(RoutingId routingId,
                                    RoutingId spotRid,
                                    Message[] parts,
                                    long requestSeq,
                                    boolean hasRequestSeq,
                                    BiConsumer<List<Message>, SendFlags> replySender) {
        return receivedAccess().create(routingId, spotRid, parts, requestSeq,
            hasRequestSeq, replySender);
    }

    public static Received received(RoutingId routingId,
                                    RoutingId spotRid,
                                    Message[] parts,
                                    boolean trustedParts,
                                    long requestSeq,
                                    boolean hasRequestSeq,
                                    BiConsumer<List<Message>, SendFlags> replySender) {
        return receivedAccess().create(routingId, spotRid, parts, trustedParts,
            requestSeq, hasRequestSeq, replySender);
    }

    public static Received received(RoutingId routingId,
                                    RoutingId spotRid,
                                    Message[] parts,
                                    boolean trustedParts,
                                    long requestSeq,
                                    boolean hasRequestSeq,
                                    BiConsumer<List<Message>, SendFlags> replySender,
                                    Runnable onTerminalState) {
        return receivedAccess().create(routingId, spotRid, parts, trustedParts,
            requestSeq, hasRequestSeq, replySender, onTerminalState);
    }

    public static Received received(byte[] routingIdBytes,
                                    byte[] spotRidBytes,
                                    Message[] parts,
                                    boolean trustedParts,
                                    long requestSeq,
                                    boolean hasRequestSeq,
                                    BiConsumer<List<Message>, SendFlags> replySender,
                                    Runnable onTerminalState) {
        return receivedAccess().create(routingIdBytes, spotRidBytes, parts,
            trustedParts, requestSeq, hasRequestSeq, replySender,
            onTerminalState);
    }

    public static Received receivedLazy(byte[] routingIdBytes,
                                        byte[] spotRidBytes,
                                        Message firstPart,
                                        ReceivedPartCursor cursor,
                                        long requestSeq,
                                        boolean hasRequestSeq,
                                        BiConsumer<List<Message>, SendFlags> replySender,
                                        Runnable onTerminalState) {
        return receivedAccess().createLazy(routingIdBytes, spotRidBytes,
            firstPart, cursor, requestSeq, hasRequestSeq, replySender,
            onTerminalState);
    }

    public static Received receivedLazy(RoutingId routingId,
                                        RoutingId spotRid,
                                        Message firstPart,
                                        ReceivedPartCursor cursor,
                                        long requestSeq,
                                        boolean hasRequestSeq,
                                        BiConsumer<List<Message>, SendFlags> replySender,
                                        Runnable onTerminalState) {
        return receivedAccess().createLazy(routingId, spotRid, firstPart,
            cursor, requestSeq, hasRequestSeq, replySender, onTerminalState);
    }

    public static TopicMessage topicMessage(RoutingId routingId,
                                            String topicId,
                                            Message[] parts) {
        return topicMessageAccess().create(routingId, topicId, parts);
    }

    public static void topicMessageAdoptSingle(TopicMessage target,
                                               RoutingId routingId,
                                               String topicId,
                                               Message part) {
        topicMessageAccess().adoptSingle(target, routingId, topicId, part);
    }

    public static Message topicMessagePrepareReusableSinglePart(
      TopicMessage target) {
        return topicMessageAccess().prepareReusableSinglePart(target);
    }

    public static void receivedForceMaterialize(Received received) {
        receivedAccess().forceMaterialize(received);
    }

    public static List<Message> receivedTakeParts(Received received) {
        return receivedAccess().takeParts(received);
    }

    public static void receivedSetSendSender(Received received,
                                             BiFunction<List<Message>, SendFlags,
                                                 Boolean> sendSender) {
        receivedAccess().setSendSender(received, sendSender);
    }

    public static boolean inCallback() {
        return socketAccess().inCallback();
    }

    public static void enterCallback() {
        socketAccess().enterCallback();
    }

    public static void leaveCallback() {
        socketAccess().leaveCallback();
    }

    public static RoutingId routingIdFromTrusted(byte[] value) {
        return routingIdAccess().fromTrusted(value);
    }

    public static byte[] routingIdTrustedBytes(RoutingId routingId) {
        return routingIdAccess().trustedBytes(routingId);
    }

    public static ZlinkException zlinkExceptionFromLastError(String operation) {
        return errorAccess().fromLastError(operation);
    }

    public static ZlinkException zlinkExceptionFromErrno(String operation,
                                                         int errno) {
        return errorAccess().fromErrno(operation, errno);
    }

    private static ContextAccess contextAccess() {
        if (contextAccess == null) load(Context.class);
        return require(contextAccess, Context.class);
    }

    private static DiscoveryAccess discoveryAccess() {
        if (discoveryAccess == null) load(Discovery.class);
        return require(discoveryAccess, Discovery.class);
    }

    private static SocketAccess socketAccess() {
        if (socketAccess == null) load(Socket.class);
        return require(socketAccess, Socket.class);
    }

    private static SpotAccess spotAccess() {
        if (spotAccess == null) load(Spot.class);
        return require(spotAccess, Spot.class);
    }

    private static SpotNodeAccess spotNodeAccess() {
        if (spotNodeAccess == null) load(SpotNode.class);
        return require(spotNodeAccess, SpotNode.class);
    }

    private static SpotDispatchInfoAccess spotDispatchInfoAccess() {
        if (spotDispatchInfoAccess == null) load(SpotDispatchInfo.class);
        return require(spotDispatchInfoAccess, SpotDispatchInfo.class);
    }

    private static TimerAccess timerAccess() {
        if (timerAccess == null) load(Timer.class);
        return require(timerAccess, Timer.class);
    }

    private static MessageAccess messageAccess() {
        if (messageAccess == null) load(Message.class);
        return require(messageAccess, Message.class);
    }

    private static ReceivedAccess receivedAccess() {
        if (receivedAccess == null) load(Received.class);
        return require(receivedAccess, Received.class);
    }

    private static TopicMessageAccess topicMessageAccess() {
        if (topicMessageAccess == null) load(TopicMessage.class);
        return require(topicMessageAccess, TopicMessage.class);
    }

    private static RoutingIdAccess routingIdAccess() {
        if (routingIdAccess == null) load(RoutingId.class);
        return require(routingIdAccess, RoutingId.class);
    }

    private static ErrorAccess errorAccess() {
        if (errorAccess == null) load(ZlinkException.class);
        return require(errorAccess, ZlinkException.class);
    }

    private static <T> T require(T access, Class<?> ownerType) {
        if (access != null) {
            return access;
        }
        throw new IllegalStateException(
            "internal access not registered: " + ownerType.getName());
    }

    private static void load(Class<?> ownerType) {
        try {
            Class.forName(ownerType.getName(), true, ownerType.getClassLoader());
        } catch (ClassNotFoundException e) {
            throw new ExceptionInInitializerError(e);
        }
    }
}

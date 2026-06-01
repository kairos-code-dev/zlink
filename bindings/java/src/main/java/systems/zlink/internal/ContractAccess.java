/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.internal;

import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.core.ContextOption;
import systems.zlink.contracts.core.AtomicCounter;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.core.ZlinkStopwatch;
import systems.zlink.contracts.core.ZlinkThread;
import systems.zlink.contracts.eventing.PollEvents;
import systems.zlink.contracts.eventing.Poller;
import systems.zlink.contracts.eventing.ZlinkTimer;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.messaging.Received;
import systems.zlink.contracts.messaging.TopicMessage;
import systems.zlink.contracts.service.spot.ActorJoinInfo;
import systems.zlink.contracts.service.spot.ActorJoinRequest;
import systems.zlink.contracts.service.spot.ActorReceived;
import systems.zlink.contracts.service.spot.ActorRef;
import systems.zlink.contracts.service.spot.SpotActorLifecycleEventKind;
import systems.zlink.contracts.service.spot.Spot;
import systems.zlink.contracts.sockets.SpotDispatchEvent;
import systems.zlink.contracts.sockets.SpotDispatchInfo;
import systems.zlink.contracts.sockets.SpotDispatchSubjectKind;
import systems.zlink.contracts.sockets.Socket;
import systems.zlink.contracts.sockets.SocketOptionKey;
import systems.zlink.contracts.sockets.SendFlags;
import java.nio.ByteBuffer;
import java.util.Objects;
import java.util.List;
import java.util.function.BiConsumer;
import java.util.function.BiFunction;

/**
 * Non-exported bridge for runtime code that must fill contract-owned state.
 */
public final class ContractAccess {
    private static volatile PollEventsAccess pollEventsAccess;
    private static volatile ActorJoinAccess actorJoinAccess;
    private static volatile SpotDispatchInfoAccess spotDispatchInfoAccess;
    private static volatile RoutingIdAccess routingIdAccess;
    private static volatile ContextOptionsAccess contextOptionsAccess;
    private static volatile SocketOptionsAccess socketOptionsAccess;
    private static volatile NativeErrorAccess nativeErrorAccess;
    private static volatile ReceivedAccess receivedAccess;
    private static volatile TopicMessageAccess topicMessageAccess;
    private static volatile RuntimeFactoryAccess runtimeFactoryAccess;
    private static volatile MessageAccess messageAccess;
    private static volatile NativeMessageAccess nativeMessageAccess;

    private ContractAccess() {
    }

    public interface PollEventsAccess {
        void markReadyCount(PollEvents events, int readyCount);

        void markEvent(PollEvents events, int index, int sourceKindValue,
                       long slot, int revents, int fd);
    }

    public interface ActorJoinAccess {
        ActorJoinRequest request(ActorJoinInfo info, Message message);

        ActorJoinInfo infoFromNative(ActorRef sourceActor,
                                     ActorRef targetActor,
                                     RoutingId sourceNodeRid,
                                     RoutingId sourceSpotRid,
                                     RoutingId targetNodeRid,
                                     RoutingId targetSpotRid,
                                     long joinEpoch,
                                     Object requestState,
                                     int flags);

        Object requestState(ActorJoinInfo info);

        RoutingId sourceNodeRidRaw(ActorJoinInfo info);

        RoutingId sourceSpotRidRaw(ActorJoinInfo info);

        RoutingId targetNodeRidRaw(ActorJoinInfo info);

        RoutingId targetSpotRidRaw(ActorJoinInfo info);

        SpotActorLifecycleEventKind lifecycleKindFromValue(int value);
    }

    public interface SpotDispatchInfoAccess {
        Object subjectState(SpotDispatchInfo info);

        SpotDispatchInfo create(SpotDispatchEvent event,
                                SpotDispatchSubjectKind subjectKind,
                                Object subject);

        SpotDispatchInfo create(SpotDispatchEvent event,
                                SpotDispatchSubjectKind subjectKind,
                                Object subject,
                                List<ActorReceived> actorMessages);

        SpotDispatchInfo create(SpotDispatchEvent event,
                                SpotDispatchSubjectKind subjectKind,
                                Object subject,
                                ZlinkTimer timer,
                                String channelName,
                                List<ActorReceived> actorMessages);
    }

    public interface RoutingIdAccess {
        RoutingId fromTrusted(byte[] value);

        RoutingId tryFromInlineCached(int size, long lo, long hi);

        byte[] trustedBytes(RoutingId routingId);
    }

    public interface ContextOptionsAccess {
        void setOption(Context context, ContextOption option, int value);

        void setOptionData(Context context, ContextOption option, String value);

        int getOption(Context context, ContextOption option);
    }

    public interface SocketOptionsAccess {
        void setOption(Socket socket, SocketOptionKey<Integer> option,
                       int value);

        void setOption(Socket socket, SocketOptionKey<Long> option,
                       long value);

        void setOption(Socket socket, SocketOptionKey<String> option,
                       String value);

        void setOption(Socket socket, SocketOptionKey<byte[]> option,
                       byte[] value);

        <T> T getOption(Socket socket, SocketOptionKey<T> option);

        void setDealerIntOption(Socket socket, int option, int value);

        int getDealerIntOption(Socket socket, int option);

        int getRouterIntOption(Socket socket, int option);

        void setRouterIntOption(Socket socket, int option, int value);
    }

    public interface NativeErrorAccess {
        int errno();

        String strerror(int errno);
    }

    public interface ReceivedPartCursor extends AutoCloseable {
        Message nextPartOrNull();

        @Override
        void close();
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

        void populateRoutedSinglePart(Received received,
                                      byte[] routingIdBytes,
                                      byte[] spotRidBytes,
                                      Message singlePart,
                                      long requestSequence,
                                      boolean hasRequestSequence,
                                      BiConsumer<List<Message>, SendFlags>
                                          replySender,
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

    public interface RuntimeFactoryAccess {
        Context createContext();

        AtomicCounter createAtomicCounter();

        ZlinkStopwatch createStopwatch();

        ZlinkThread createThread(Runnable task);

        Poller createPoller();

        ZlinkTimer createTimer();

        ZlinkTimer createTimer(Spot spot);

        int errno();

        String strerror(int errnum);

        boolean has(String capability);

        int[] version();

        void proxy(Socket frontend, Socket backend, Socket capture);

        void proxySteerable(Socket frontend, Socket backend, Socket capture,
                            Socket control);

        void sleep(int seconds);

        void sleep(java.time.Duration duration);

        void multipartClose(Message[] parts);
    }

    public interface MessageAccess {
        Object dataSegment(Message message);

        Object dataSegment(Message message, int knownSize);

        void copyTo(Message message, Object destination);

        void moveTo(Message message, Object destination);

        Object nativeHandle(Message message);

        void setMore(Message message, boolean more);

        boolean more(Message message);

        void finishReceive(Message message, boolean more);

        void transferTo(Message message, Object destination);

        void restoreFromNative(Message message, Object source,
                               boolean moreFlag);

        void markTransferred(Message message);

        int moveInto(Message source, Message target, boolean moreFlag);

        Message sharedCopyOf(Message message);

        Message prepareVectorTarget(Message message);

        void finishVectorMove(Message message, boolean moreFlag);

        void resetReusable(Message message);

        Message adoptOwnedNative(Object nativeMsg);

        Message fromSegment(Object segment, long offset, long length);
    }

    public interface NativeMessageAccess {
        long layoutSize();

        Object openSharedMessageScope();

        boolean messageScopeAlive(Object scope);

        void closeMessageScope(Object scope);

        Object allocate(Object scope);

        Object handleFromAddress(long address);

        Object segmentFromAddress(long address, long size);

        Object slice(Object segment, long offset, long size);

        long address(Object segment);

        int init(Object msg);

        int initSize(Object msg, int size);

        int close(Object msg);

        int move(Object destination, Object source);

        int copy(Object destination, Object source);

        long size(Object msg);

        long dataAddress(Object msg);

        int refCount(Object msg);

        String getProperty(Object msg, String property);

        void closeVector(Object parts, long count);

        Message[] materializeVector(Object parts, long count,
                                    Message[] reusable,
                                    boolean closeSourceVector);

        Message[] materializeVectorShared(Object parts, long count);

        Message adoptOwnedMessage(Object nativeMsg);

        void copyFromSegment(Object source, long sourceOffset, Object destination,
                             long destinationOffset, long length);

        void copyFromBuffer(ByteBuffer source, Object destination,
                            long destinationOffset, long length);

        void copyToBuffer(Object source, long sourceOffset,
                          ByteBuffer destination, long length);

        ByteBuffer asReadOnlyBuffer(Object source);

        ByteBuffer asMutableBuffer(Object source);
    }

    public static void register(PollEventsAccess access) {
        pollEventsAccess = Objects.requireNonNull(access, "access");
    }

    public static void register(ActorJoinAccess access) {
        actorJoinAccess = Objects.requireNonNull(access, "access");
    }

    public static void register(SpotDispatchInfoAccess access) {
        spotDispatchInfoAccess = Objects.requireNonNull(access, "access");
    }

    public static void register(RoutingIdAccess access) {
        routingIdAccess = Objects.requireNonNull(access, "access");
    }

    public static void register(ContextOptionsAccess access) {
        contextOptionsAccess = Objects.requireNonNull(access, "access");
    }

    public static void register(SocketOptionsAccess access) {
        socketOptionsAccess = Objects.requireNonNull(access, "access");
    }

    public static void register(NativeErrorAccess access) {
        nativeErrorAccess = Objects.requireNonNull(access, "access");
    }

    public static void register(ReceivedAccess access) {
        receivedAccess = Objects.requireNonNull(access, "access");
    }

    public static void register(TopicMessageAccess access) {
        topicMessageAccess = Objects.requireNonNull(access, "access");
    }

    public static void register(RuntimeFactoryAccess access) {
        runtimeFactoryAccess = Objects.requireNonNull(access, "access");
    }

    public static void register(MessageAccess access) {
        messageAccess = Objects.requireNonNull(access, "access");
    }

    public static void register(NativeMessageAccess access) {
        nativeMessageAccess = Objects.requireNonNull(access, "access");
    }

    public static void pollEventsMarkReadyCount(PollEvents events,
                                                int readyCount) {
        pollEventsAccess().markReadyCount(events, readyCount);
    }

    public static void pollEventsMarkEvent(PollEvents events, int index,
                                           int sourceKindValue, long slot,
                                           int revents, int fd) {
        pollEventsAccess().markEvent(events, index, sourceKindValue, slot,
            revents, fd);
    }

    public static ActorJoinRequest actorJoinRequest(ActorJoinInfo info,
                                                    Message message) {
        return actorJoinAccess().request(info, message);
    }

    public static ActorJoinInfo actorJoinInfoFromNative(
      ActorRef sourceActor,
      ActorRef targetActor,
      RoutingId sourceNodeRid,
      RoutingId sourceSpotRid,
      RoutingId targetNodeRid,
      RoutingId targetSpotRid,
      long joinEpoch,
      Object requestState,
      int flags) {
        return actorJoinAccess().infoFromNative(sourceActor, targetActor,
            sourceNodeRid, sourceSpotRid, targetNodeRid, targetSpotRid,
            joinEpoch, requestState, flags);
    }

    public static Object actorJoinInfoRequestState(ActorJoinInfo info) {
        return actorJoinAccess().requestState(info);
    }

    public static RoutingId actorJoinInfoSourceNodeRidRaw(ActorJoinInfo info) {
        return actorJoinAccess().sourceNodeRidRaw(info);
    }

    public static RoutingId actorJoinInfoSourceSpotRidRaw(ActorJoinInfo info) {
        return actorJoinAccess().sourceSpotRidRaw(info);
    }

    public static RoutingId actorJoinInfoTargetNodeRidRaw(ActorJoinInfo info) {
        return actorJoinAccess().targetNodeRidRaw(info);
    }

    public static RoutingId actorJoinInfoTargetSpotRidRaw(ActorJoinInfo info) {
        return actorJoinAccess().targetSpotRidRaw(info);
    }

    public static SpotActorLifecycleEventKind actorLifecycleKindFromValue(
      int value) {
        return actorJoinAccess().lifecycleKindFromValue(value);
    }

    public static Object spotDispatchSubjectState(SpotDispatchInfo info) {
        return spotDispatchInfoAccess().subjectState(info);
    }

    public static SpotDispatchInfo spotDispatchInfo(
      SpotDispatchEvent event,
      SpotDispatchSubjectKind subjectKind,
      Object subject) {
        return spotDispatchInfoAccess().create(event, subjectKind, subject);
    }

    public static SpotDispatchInfo spotDispatchInfo(
      SpotDispatchEvent event,
      SpotDispatchSubjectKind subjectKind,
      Object subject,
      List<ActorReceived> actorMessages) {
        return spotDispatchInfoAccess().create(event, subjectKind, subject,
            actorMessages);
    }

    public static SpotDispatchInfo spotDispatchInfo(
      SpotDispatchEvent event,
      SpotDispatchSubjectKind subjectKind,
      Object subject,
      ZlinkTimer timer,
      String channelName,
      List<ActorReceived> actorMessages) {
        return spotDispatchInfoAccess().create(event, subjectKind, subject,
            timer, channelName, actorMessages);
    }

    public static RoutingId routingIdFromTrusted(byte[] value) {
        return routingIdAccess().fromTrusted(value);
    }

    public static RoutingId routingIdTryFromInlineCached(int size, long lo,
                                                         long hi) {
        return routingIdAccess().tryFromInlineCached(size, lo, hi);
    }

    public static byte[] routingIdTrustedBytes(RoutingId routingId) {
        return routingIdAccess().trustedBytes(routingId);
    }

    public static void contextSetOption(Context context, ContextOption option,
                                        int value) {
        contextOptionsAccess().setOption(context, option, value);
    }

    public static void contextSetOptionData(Context context,
                                            ContextOption option,
                                            String value) {
        contextOptionsAccess().setOptionData(context, option, value);
    }

    public static int contextGetOption(Context context, ContextOption option) {
        return contextOptionsAccess().getOption(context, option);
    }

    public static void socketSetOption(Socket socket,
                                       SocketOptionKey<Integer> option,
                                       int value) {
        socketOptionsAccess().setOption(socket, option, value);
    }

    public static void socketSetOption(Socket socket,
                                       SocketOptionKey<Long> option,
                                       long value) {
        socketOptionsAccess().setOption(socket, option, value);
    }

    public static void socketSetOption(Socket socket,
                                       SocketOptionKey<String> option,
                                       String value) {
        socketOptionsAccess().setOption(socket, option, value);
    }

    public static void socketSetOption(Socket socket,
                                       SocketOptionKey<byte[]> option,
                                       byte[] value) {
        socketOptionsAccess().setOption(socket, option, value);
    }

    public static <T> T socketGetOption(Socket socket,
                                        SocketOptionKey<T> option) {
        return socketOptionsAccess().getOption(socket, option);
    }

    public static void socketSetDealerIntOption(Socket socket, int option,
                                                int value) {
        socketOptionsAccess().setDealerIntOption(socket, option, value);
    }

    public static int socketGetDealerIntOption(Socket socket, int option) {
        return socketOptionsAccess().getDealerIntOption(socket, option);
    }

    public static int socketGetRouterIntOption(Socket socket, int option) {
        return socketOptionsAccess().getRouterIntOption(socket, option);
    }

    public static void socketSetRouterIntOption(Socket socket, int option,
                                                int value) {
        socketOptionsAccess().setRouterIntOption(socket, option, value);
    }

    public static int nativeErrno() {
        return nativeErrorAccess().errno();
    }

    public static String nativeStrerror(int errno) {
        return nativeErrorAccess().strerror(errno);
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

    public static void receivedPopulateRoutedSinglePart(
      Received received,
      byte[] routingIdBytes,
      byte[] spotRidBytes,
      Message singlePart,
      long requestSequence,
      boolean hasRequestSequence,
      BiConsumer<List<Message>, SendFlags> replySender,
      Runnable onTerminalState) {
        receivedAccess().populateRoutedSinglePart(received, routingIdBytes,
            spotRidBytes, singlePart, requestSequence, hasRequestSequence,
            replySender, onTerminalState);
    }

    public static Context createContext() {
        return runtimeFactoryAccess().createContext();
    }

    public static AtomicCounter createAtomicCounter() {
        return runtimeFactoryAccess().createAtomicCounter();
    }

    public static ZlinkStopwatch createStopwatch() {
        return runtimeFactoryAccess().createStopwatch();
    }

    public static ZlinkThread createThread(Runnable task) {
        return runtimeFactoryAccess().createThread(task);
    }

    public static Poller createPoller() {
        return runtimeFactoryAccess().createPoller();
    }

    public static ZlinkTimer createTimer() {
        return runtimeFactoryAccess().createTimer();
    }

    public static ZlinkTimer createTimer(Spot spot) {
        return runtimeFactoryAccess().createTimer(spot);
    }

    public static int errno() {
        return runtimeFactoryAccess().errno();
    }

    public static String strerror(int errnum) {
        return runtimeFactoryAccess().strerror(errnum);
    }

    public static boolean has(String capability) {
        return runtimeFactoryAccess().has(capability);
    }

    public static int[] version() {
        return runtimeFactoryAccess().version();
    }

    public static void proxy(Socket frontend, Socket backend, Socket capture) {
        runtimeFactoryAccess().proxy(frontend, backend, capture);
    }

    public static void proxySteerable(Socket frontend, Socket backend,
                                      Socket capture, Socket control) {
        runtimeFactoryAccess().proxySteerable(frontend, backend, capture,
            control);
    }

    public static void sleep(int seconds) {
        runtimeFactoryAccess().sleep(seconds);
    }

    public static void sleep(java.time.Duration duration) {
        runtimeFactoryAccess().sleep(duration);
    }

    public static void multipartClose(Message[] parts) {
        runtimeFactoryAccess().multipartClose(parts);
    }

    public static Object messageDataSegment(Message message) {
        return messageAccess().dataSegment(message);
    }

    public static Object messageDataSegment(Message message, int knownSize) {
        return messageAccess().dataSegment(message, knownSize);
    }

    public static void messageCopyTo(Message message, Object destination) {
        messageAccess().copyTo(message, destination);
    }

    public static Message messageFromSegment(Object segment, long offset,
                                             long length) {
        return messageAccess().fromSegment(segment, offset, length);
    }

    public static void messageMoveTo(Message message, Object destination) {
        messageAccess().moveTo(message, destination);
    }

    public static Object messageNativeHandle(Message message) {
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

    public static void messageTransferTo(Message message, Object destination) {
        messageAccess().transferTo(message, destination);
    }

    public static void messageRestoreFromNative(Message message, Object source,
                                                boolean moreFlag) {
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

    public static Message messagePrepareVectorTarget(Message message) {
        return messageAccess().prepareVectorTarget(message);
    }

    public static void messageFinishVectorMove(Message message,
                                               boolean moreFlag) {
        messageAccess().finishVectorMove(message, moreFlag);
    }

    public static void messageResetReusable(Message message) {
        messageAccess().resetReusable(message);
    }

    public static Message messageAdoptOwnedNative(Object nativeMsg) {
        return messageAccess().adoptOwnedNative(nativeMsg);
    }

    public static long nativeMessageLayoutSize() {
        return nativeMessageAccess().layoutSize();
    }

    public static Object nativeMessageOpenSharedScope() {
        return nativeMessageAccess().openSharedMessageScope();
    }

    public static boolean nativeMessageScopeAlive(Object scope) {
        return nativeMessageAccess().messageScopeAlive(scope);
    }

    public static void nativeMessageCloseScope(Object scope) {
        nativeMessageAccess().closeMessageScope(scope);
    }

    public static Object nativeMessageAllocate(Object scope) {
        return nativeMessageAccess().allocate(scope);
    }

    public static Object nativeMessageHandleFromAddress(long address) {
        return nativeMessageAccess().handleFromAddress(address);
    }

    public static Object nativeMessageSegmentFromAddress(long address,
                                                         long size) {
        return nativeMessageAccess().segmentFromAddress(address, size);
    }

    public static Object nativeMessageSlice(Object segment, long offset,
                                            long size) {
        return nativeMessageAccess().slice(segment, offset, size);
    }

    public static long nativeMessageAddress(Object segment) {
        return nativeMessageAccess().address(segment);
    }

    public static int nativeMessageInit(Object msg) {
        return nativeMessageAccess().init(msg);
    }

    public static int nativeMessageInitSize(Object msg, int size) {
        return nativeMessageAccess().initSize(msg, size);
    }

    public static int nativeMessageClose(Object msg) {
        return nativeMessageAccess().close(msg);
    }

    public static int nativeMessageMove(Object destination, Object source) {
        return nativeMessageAccess().move(destination, source);
    }

    public static int nativeMessageCopy(Object destination, Object source) {
        return nativeMessageAccess().copy(destination, source);
    }

    public static long nativeMessageSize(Object msg) {
        return nativeMessageAccess().size(msg);
    }

    public static long nativeMessageDataAddress(Object msg) {
        return nativeMessageAccess().dataAddress(msg);
    }

    public static int nativeMessageRefCount(Object msg) {
        return nativeMessageAccess().refCount(msg);
    }

    public static String nativeMessageGetProperty(Object msg,
                                                  String property) {
        return nativeMessageAccess().getProperty(msg, property);
    }

    public static void nativeMessageCloseVector(Object parts, long count) {
        nativeMessageAccess().closeVector(parts, count);
    }

    public static Message[] nativeMessageMaterializeVector(
        Object parts, long count, Message[] reusable,
        boolean closeSourceVector) {
        return nativeMessageAccess().materializeVector(parts, count, reusable,
            closeSourceVector);
    }

    public static Message[] nativeMessageMaterializeVectorShared(
        Object parts, long count) {
        return nativeMessageAccess().materializeVectorShared(parts, count);
    }

    public static Message nativeMessageAdoptOwned(Object nativeMsg) {
        return nativeMessageAccess().adoptOwnedMessage(nativeMsg);
    }

    public static void nativeMessageCopyFromSegment(Object source,
                                                    long sourceOffset,
                                                    Object destination,
                                                    long destinationOffset,
                                                    long length) {
        nativeMessageAccess().copyFromSegment(source, sourceOffset,
            destination, destinationOffset, length);
    }

    public static void nativeMessageCopyFromBuffer(ByteBuffer source,
                                                   Object destination,
                                                   long destinationOffset,
                                                   long length) {
        nativeMessageAccess().copyFromBuffer(source, destination,
            destinationOffset, length);
    }

    public static void nativeMessageCopyToBuffer(Object source,
                                                 long sourceOffset,
                                                 ByteBuffer destination,
                                                 long length) {
        nativeMessageAccess().copyToBuffer(source, sourceOffset, destination,
            length);
    }

    public static ByteBuffer nativeMessageAsReadOnlyBuffer(Object source) {
        return nativeMessageAccess().asReadOnlyBuffer(source);
    }

    public static ByteBuffer nativeMessageAsMutableBuffer(Object source) {
        return nativeMessageAccess().asMutableBuffer(source);
    }

    private static PollEventsAccess pollEventsAccess() {
        if (pollEventsAccess == null) load(PollEvents.class);
        if (pollEventsAccess == null)
            throw new IllegalStateException(
                "missing contract access for " + PollEvents.class.getName());
        return pollEventsAccess;
    }

    private static ActorJoinAccess actorJoinAccess() {
        if (actorJoinAccess == null) load(ActorJoinInfo.class);
        if (actorJoinAccess == null)
            throw new IllegalStateException(
                "missing contract access for "
                    + ActorJoinInfo.class.getName());
        return actorJoinAccess;
    }

    private static SpotDispatchInfoAccess spotDispatchInfoAccess() {
        if (spotDispatchInfoAccess == null) load(SpotDispatchInfo.class);
        if (spotDispatchInfoAccess == null)
            throw new IllegalStateException(
                "missing contract access for "
                    + SpotDispatchInfo.class.getName());
        return spotDispatchInfoAccess;
    }

    private static RoutingIdAccess routingIdAccess() {
        if (routingIdAccess == null) load(RoutingId.class);
        if (routingIdAccess == null)
            throw new IllegalStateException(
                "missing contract access for " + RoutingId.class.getName());
        return routingIdAccess;
    }

    private static ContextOptionsAccess contextOptionsAccess() {
        if (contextOptionsAccess == null)
            throw new IllegalStateException(
                "missing contract access for context options");
        return contextOptionsAccess;
    }

    private static SocketOptionsAccess socketOptionsAccess() {
        if (socketOptionsAccess == null)
            throw new IllegalStateException(
                "missing contract access for socket options");
        return socketOptionsAccess;
    }

    private static NativeErrorAccess nativeErrorAccess() {
        if (nativeErrorAccess == null) load(
            "systems.zlink.runtime.errors.NativeErrorRuntime");
        if (nativeErrorAccess == null)
            throw new IllegalStateException(
                "missing contract access for native errors");
        return nativeErrorAccess;
    }

    private static ReceivedAccess receivedAccess() {
        if (receivedAccess == null) load(Received.class);
        if (receivedAccess == null)
            throw new IllegalStateException(
                "missing contract access for " + Received.class.getName());
        return receivedAccess;
    }

    private static TopicMessageAccess topicMessageAccess() {
        if (topicMessageAccess == null) load(TopicMessage.class);
        if (topicMessageAccess == null)
            throw new IllegalStateException(
                "missing contract access for "
                    + TopicMessage.class.getName());
        return topicMessageAccess;
    }

    private static RuntimeFactoryAccess runtimeFactoryAccess() {
        if (runtimeFactoryAccess == null) load(
            "systems.zlink.runtime.core.NativeRuntimeFactory");
        if (runtimeFactoryAccess == null)
            throw new IllegalStateException(
                "missing contract access for runtime factory");
        return runtimeFactoryAccess;
    }

    private static MessageAccess messageAccess() {
        if (messageAccess == null) load(Message.class);
        if (messageAccess == null)
            throw new IllegalStateException(
                "missing contract access for " + Message.class.getName());
        return messageAccess;
    }

    private static NativeMessageAccess nativeMessageAccess() {
        if (nativeMessageAccess == null) load(
            "systems.zlink.runtime.messaging.NativeMessage" + "Runtime");
        if (nativeMessageAccess == null)
            throw new IllegalStateException(
                "missing contract access for native message runtime");
        return nativeMessageAccess;
    }

    private static void load(Class<?> type) {
        try {
            Class.forName(type.getName(), true, type.getClassLoader());
        } catch (ClassNotFoundException ex) {
            throw new IllegalStateException("cannot load " + type.getName(),
                ex);
        }
    }

    private static void load(String className) {
        try {
            Class.forName(className, true, ContractAccess.class.getClassLoader());
        } catch (ClassNotFoundException ex) {
            throw new IllegalStateException("cannot load " + className, ex);
        }
    }

}

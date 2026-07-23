package systems.zlink.framework.runtime.service;

import java.io.ByteArrayOutputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.StandardCharsets;
import java.util.Objects;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorRef;
import systems.zlink.framework.runtime.protocol.ServiceWireConstants;

/**
 * Closed codec for the M6B Spot route fence. The codec validates the complete
 * header before the record can enter a Framework-owned Spot mailbox.
 */
public final class ZLinkServiceM6BWireCodec {
    private static final int PREFIX_BYTES = 5;

    public byte[] encodeSpotHeader(
        boolean request,
        int flags,
        Long correlation,
        RoutingId sourceSpotRid,
        SpotRouteFence target) {
        if ((flags & ~ServiceWireConstants.FLAG_METADATA) != 0
            || request != (correlation != null)
            || (correlation != null && correlation <= 0)) {
            throw protocol("invalid Spot message header");
        }
        Objects.requireNonNull(sourceSpotRid, "sourceSpotRid");
        Objects.requireNonNull(target, "target");
        Writer writer = prefix(
            request
                ? ServiceWireConstants.COMMAND_SPOT_REQUEST
                : ServiceWireConstants.COMMAND_SPOT_SEND,
            flags);
        if (correlation != null) {
            writer.u64(correlation);
        }
        writer.rid(sourceSpotRid, "sourceSpotRid");
        writer.rid(target.spotRid(), "targetSpotRid");
        writer.nonzero(target.spotGeneration(), "targetSpotGeneration");
        writer.rid(target.targetNodeRid(), "targetNodeRid");
        writer.nonzero(
            target.targetNodeGeneration(), "targetNodeGeneration");
        writer.nonzero(
            target.authorityOwnerGeneration(),
            "authorityOwnerGeneration");
        return writer.toByteArray();
    }

    public SpotMessage decodeSpotHeader(byte[] frame) {
        Reader reader = new Reader(frame);
        Header header = reader.prefix();
        boolean request;
        if (header.command() == ServiceWireConstants.COMMAND_SPOT_SEND) {
            request = false;
        } else if (
            header.command() == ServiceWireConstants.COMMAND_SPOT_REQUEST) {
            request = true;
        } else {
            throw protocol("command is not a Spot message");
        }
        if ((header.flags() & ~ServiceWireConstants.FLAG_METADATA) != 0) {
            throw protocol("Spot message contains an unknown flag");
        }
        Long correlation = request
            ? reader.nonzeroU64("correlation")
            : null;
        RoutingId sourceSpotRid = reader.rid("sourceSpotRid");
        SpotRouteFence target = new SpotRouteFence(
            reader.rid("targetSpotRid"),
            reader.nonzeroU64("targetSpotGeneration"),
            reader.rid("targetNodeRid"),
            reader.nonzeroU64("targetNodeGeneration"),
            reader.nonzeroU64("authorityOwnerGeneration"));
        reader.end();
        return new SpotMessage(
            request,
            header.flags(),
            correlation,
            sourceSpotRid,
            target);
    }

    public byte[] encodeActorHeader(
        boolean request,
        int flags,
        Long correlation,
        ZLinkBackendActorRef sourceActor,
        ActorRouteFence target) {
        return encodeActorHeader(
            request,
            flags,
            correlation,
            sourceActor,
            target,
            null);
    }

    public byte[] encodeActorHeader(
        boolean request,
        int flags,
        Long correlation,
        ZLinkBackendActorRef sourceActor,
        ActorRouteFence target,
        BoundSessionTail boundSession) {
        int boundFlags =
            ServiceWireConstants.FLAG_BOUND_SESSION
                | ServiceWireConstants.FLAG_SOURCE_SPOT_RID;
        if ((flags & ~(ServiceWireConstants.FLAG_METADATA | boundFlags)) != 0
            || request != (correlation != null)
            || (correlation != null && correlation <= 0)
            || ((flags & boundFlags) != 0
                && (flags & boundFlags) != boundFlags)
            || ((flags & boundFlags) == boundFlags)
                != (boundSession != null)) {
            throw protocol("invalid Actor message header");
        }
        Objects.requireNonNull(target, "target");
        Writer writer = prefix(
            request
                ? ServiceWireConstants.COMMAND_ACTOR_REQUEST
                : ServiceWireConstants.COMMAND_ACTOR_SEND,
            flags);
        if (correlation != null) {
            writer.u64(correlation);
        }
        if (sourceActor == null) {
            writer.u8(0);
        } else {
            writer.text8(sourceActor.actorId(), "sourceActorId");
            writer.nonzero(sourceActor.generation(), "sourceActorGeneration");
        }
        writer.text8(target.actor().actorId(), "targetActorId");
        writer.nonzero(
            target.actor().generation(), "targetActorGeneration");
        writer.rid(target.actor().nodeRid(), "targetNodeRid");
        writer.nonzero(
            target.targetNodeGeneration(), "targetNodeGeneration");
        writer.nonzero(
            target.authorityOwnerGeneration(),
            "authorityOwnerGeneration");
        if (boundSession != null) {
            writer.rid(
                boundSession.sourceSessionRid(), "sourceSessionRid");
            writer.nonzero(
                boundSession.sourceBindingGeneration(),
                "sourceBindingGeneration");
            writer.nonzero(
                boundSession.sourceSessionSequence(),
                "sourceSessionSequence");
        }
        return writer.toByteArray();
    }

    public ActorMessage decodeActorHeader(byte[] frame) {
        Reader reader = new Reader(frame);
        Header header = reader.prefix();
        boolean request;
        if (header.command() == ServiceWireConstants.COMMAND_ACTOR_SEND) {
            request = false;
        } else if (
            header.command() == ServiceWireConstants.COMMAND_ACTOR_REQUEST) {
            request = true;
        } else {
            throw protocol("command is not an Actor message");
        }
        int boundFlags =
            ServiceWireConstants.FLAG_BOUND_SESSION
                | ServiceWireConstants.FLAG_SOURCE_SPOT_RID;
        if ((header.flags()
                & ~(ServiceWireConstants.FLAG_METADATA | boundFlags)) != 0
            || ((header.flags() & boundFlags) != 0
                && (header.flags() & boundFlags) != boundFlags)) {
            throw protocol("unsupported Actor message flags");
        }
        Long correlation = request
            ? reader.nonzeroU64("correlation")
            : null;
        String sourceActorId = reader.optionalText8("sourceActorId");
        ActorIdentity sourceActor = sourceActorId == null
            ? null
            : new ActorIdentity(
                sourceActorId,
                reader.nonzeroU64("sourceActorGeneration"));
        String targetActorId = reader.text8("targetActorId");
        long targetActorGeneration =
            reader.nonzeroU64("targetActorGeneration");
        RoutingId targetNodeRid = reader.rid("targetNodeRid");
        ActorRouteFence target = new ActorRouteFence(
            new ZLinkBackendActorRef(
                targetNodeRid,
                targetActorId,
                targetActorGeneration),
            reader.nonzeroU64("targetNodeGeneration"),
            reader.nonzeroU64("authorityOwnerGeneration"));
        BoundSessionTail boundSession =
            (header.flags() & boundFlags) == boundFlags
                ? new BoundSessionTail(
                    reader.rid("sourceSessionRid"),
                    reader.nonzeroU64("sourceBindingGeneration"),
                    reader.nonzeroU64("sourceSessionSequence"))
                : null;
        reader.end();
        return new ActorMessage(
            request,
            header.flags(),
            correlation,
            sourceActor,
            target,
            boundSession);
    }

    public byte[] encodeBoundSessionSendHeader(
        ActorRouteFence actor,
        long expectedBindingGeneration) {
        Writer writer = prefix(
            ServiceWireConstants.COMMAND_BOUND_SESSION_SEND,
            0);
        writeActorRoute(writer, actor);
        writer.nonzero(
            expectedBindingGeneration, "expectedBindingGeneration");
        return writer.toByteArray();
    }

    public BoundSessionSend decodeBoundSessionSendHeader(byte[] frame) {
        Reader reader = new Reader(frame);
        Header header = reader.prefix();
        if (header.command()
                != ServiceWireConstants.COMMAND_BOUND_SESSION_SEND
            || header.flags() != 0) {
            throw protocol("command is not boundSessionSend");
        }
        BoundSessionSend result = new BoundSessionSend(
            readActorRoute(reader),
            reader.nonzeroU64("expectedBindingGeneration"));
        reader.end();
        return result;
    }

    public byte[] encodeBoundSessionBindHeader(BoundSessionBind binding) {
        Objects.requireNonNull(binding, "binding");
        Writer writer = prefix(
            ServiceWireConstants.COMMAND_BOUND_SESSION_BIND,
            0);
        writer.nonzero(binding.correlation(), "correlation");
        writeActorRoute(writer, binding.actor());
        writer.rid(binding.sessionRid(), "sessionRid");
        writer.u8(binding.active() ? 1 : 2);
        writer.u16(Long.BYTES);
        writer.nonzero(
            binding.bindingGeneration(), binding.active()
                ? "bindingGeneration"
                : "retiredBindingGeneration");
        return writer.toByteArray();
    }

    public BoundSessionBind decodeBoundSessionBindHeader(byte[] frame) {
        Reader reader = new Reader(frame);
        Header header = reader.prefix();
        if (header.command()
                != ServiceWireConstants.COMMAND_BOUND_SESSION_BIND
            || header.flags() != 0) {
            throw protocol("command is not boundSessionBind");
        }
        long correlation = reader.nonzeroU64("correlation");
        ActorRouteFence actor = readActorRoute(reader);
        RoutingId sessionRid = reader.rid("sessionRid");
        int state = reader.u8("bindingState");
        if (state != 1 && state != 2) {
            throw protocol("unknown bound session binding state");
        }
        int bodyLength = reader.u16("binding.length");
        int bodyEnd = reader.position() + bodyLength;
        long generation = reader.nonzeroU64(
            state == 1
                ? "bindingGeneration"
                : "retiredBindingGeneration");
        if (bodyLength != Long.BYTES || reader.position() != bodyEnd) {
            throw protocol("invalid bound session binding body length");
        }
        reader.end();
        return new BoundSessionBind(
            correlation,
            actor,
            sessionRid,
            state == 1,
            generation);
    }

    public byte[] encodeLogicalMulticastHeader(
        int flags,
        String channelName,
        String topic,
        RoutingId sourceSpotRid) {
        if ((flags & ~ServiceWireConstants.FLAG_METADATA) != 0) {
            throw protocol("logical multicast contains an unknown flag");
        }
        Writer writer = prefix(
            ServiceWireConstants.COMMAND_LOGICAL_MULTICAST,
            flags);
        writer.text8(channelName, "channelName");
        writer.text8(topic, "topic");
        writer.rid(sourceSpotRid, "sourceSpotRid");
        return writer.toByteArray();
    }

    public LogicalMulticast decodeLogicalMulticastHeader(byte[] frame) {
        Reader reader = new Reader(frame);
        Header header = reader.prefix();
        if (header.command()
                != ServiceWireConstants.COMMAND_LOGICAL_MULTICAST
            || (header.flags() & ~ServiceWireConstants.FLAG_METADATA) != 0) {
            throw protocol("command is not logical multicast");
        }
        LogicalMulticast result = new LogicalMulticast(
            header.flags(),
            reader.text8("channelName"),
            reader.text8("topic"),
            reader.rid("sourceSpotRid"));
        reader.end();
        return result;
    }

    public byte[] encodeInstanceSpotHeader(InstanceSpotMessage message) {
        Objects.requireNonNull(message, "message");
        if ((message.flags() & ~ServiceWireConstants.FLAG_METADATA) != 0
            || message.sourceNodeGeneration() <= 0
            || (message.request()
                != (message.operationHigh() != 0
                    || message.operationLow() != 0))
            || message.request() != (message.replyRouteId() != null)
            || (message.replyRouteId() != null
                && message.replyRouteId() <= 0)) {
            throw protocol("invalid Instance Spot message header");
        }
        Writer route = new Writer();
        route.rid(message.route().targetNodeRid(), "targetNodeRid");
        route.nonzero(
            message.route().targetNodeGeneration(),
            "targetNodeGeneration");
        route.rid(message.route().targetSpotRid(), "targetSpotRid");
        route.nonzero(
            message.route().objectGeneration(), "objectGeneration");
        route.text8(message.route().ownerId(), "ownerId");
        route.nonzero(
            message.route().authorityOwnerGeneration(),
            "authorityOwnerGeneration");
        route.nonzero(
            message.route().leaseGeneration(), "leaseGeneration");
        route.text16(message.route().storeVersion(), "storeVersion");
        byte[] routeBody = route.toByteArray();

        Writer writer = prefix(
            ServiceWireConstants.COMMAND_INSTANCE_SPOT,
            message.flags());
        writer.u8(1);
        writer.u16(routeBody.length);
        writer.bytes(routeBody);
        writer.nonzero(
            message.sourceNodeGeneration(), "sourceNodeGeneration");
        writer.rid(message.sourceNodeRid(), "sourceNodeRid");
        writer.optionalRid(message.sourceSpotRid(), "sourceSpotRid");
        writer.u8(message.request() ? 2 : 1);
        writer.u64(message.operationHigh());
        writer.u64(message.operationLow());
        if (message.replyRouteId() != null) {
            writer.nonzero(message.replyRouteId(), "replyRouteId");
        }
        return writer.toByteArray();
    }

    public InstanceSpotMessage decodeInstanceSpotHeader(byte[] frame) {
        Reader reader = new Reader(frame);
        Header header = reader.prefix();
        if (header.command() != ServiceWireConstants.COMMAND_INSTANCE_SPOT
            || (header.flags() & ~ServiceWireConstants.FLAG_METADATA) != 0
            || reader.u8("instanceRoute.version") != 1) {
            throw protocol("command is not Instance Spot");
        }
        int routeLength = reader.u16("instanceRoute.length");
        int routeEnd = reader.position() + routeLength;
        InstanceRouteFence route = new InstanceRouteFence(
            reader.rid("targetNodeRid"),
            reader.nonzeroU64("targetNodeGeneration"),
            reader.rid("targetSpotRid"),
            reader.nonzeroU64("objectGeneration"),
            reader.text8("ownerId"),
            reader.nonzeroU64("authorityOwnerGeneration"),
            reader.nonzeroU64("leaseGeneration"),
            reader.text16("storeVersion"));
        if (reader.position() != routeEnd) {
            throw protocol("invalid Instance route body length");
        }
        long sourceNodeGeneration =
            reader.nonzeroU64("sourceNodeGeneration");
        RoutingId sourceNodeRid = reader.rid("sourceNodeRid");
        RoutingId sourceSpotRid = reader.optionalRid("sourceSpotRid");
        int operationKind = reader.u8("operationKind");
        if (operationKind != 1 && operationKind != 2) {
            throw protocol("unknown Instance operation kind");
        }
        long operationHigh = reader.u64("operation.high");
        long operationLow = reader.u64("operation.low");
        boolean request = operationKind == 2;
        if ((!request && (operationHigh != 0 || operationLow != 0))
            || (request && operationHigh == 0 && operationLow == 0)) {
            throw protocol("invalid Instance operation identity");
        }
        Long replyRouteId = request
            ? reader.nonzeroU64("replyRouteId")
            : null;
        reader.end();
        return new InstanceSpotMessage(
            header.flags(),
            route,
            sourceNodeGeneration,
            sourceNodeRid,
            sourceSpotRid,
            request,
            operationHigh,
            operationLow,
            replyRouteId);
    }

    public record SpotRouteFence(
        RoutingId spotRid,
        long spotGeneration,
        RoutingId targetNodeRid,
        long targetNodeGeneration,
        long authorityOwnerGeneration) {
        public SpotRouteFence {
            Objects.requireNonNull(spotRid, "spotRid");
            Objects.requireNonNull(targetNodeRid, "targetNodeRid");
            if (spotGeneration <= 0
                || targetNodeGeneration <= 0
                || authorityOwnerGeneration <= 0) {
                throw protocol("Spot route fence generations must be nonzero");
            }
        }
    }

    public record SpotMessage(
        boolean request,
        int flags,
        Long correlation,
        RoutingId sourceSpotRid,
        SpotRouteFence target) {
    }

    public record ActorRouteFence(
        ZLinkBackendActorRef actor,
        long targetNodeGeneration,
        long authorityOwnerGeneration) {
        public ActorRouteFence {
            Objects.requireNonNull(actor, "actor");
            if (actor.generation() <= 0
                || targetNodeGeneration <= 0
                || authorityOwnerGeneration <= 0) {
                throw protocol("Actor route fence generations must be nonzero");
            }
        }
    }

    public record ActorMessage(
        boolean request,
        int flags,
        Long correlation,
        ActorIdentity sourceActor,
        ActorRouteFence target,
        BoundSessionTail boundSession) {
        public ActorMessage(
            boolean request,
            int flags,
            Long correlation,
            ActorIdentity sourceActor,
            ActorRouteFence target) {
            this(
                request,
                flags,
                correlation,
                sourceActor,
                target,
                null);
        }
    }

    public record BoundSessionTail(
        RoutingId sourceSessionRid,
        long sourceBindingGeneration,
        long sourceSessionSequence) {
        public BoundSessionTail {
            Objects.requireNonNull(sourceSessionRid, "sourceSessionRid");
            if (sourceBindingGeneration <= 0
                || sourceSessionSequence <= 0) {
                throw protocol(
                    "bound session tail generations must be nonzero");
            }
        }
    }

    public record ActorIdentity(String actorId, long generation) {
        public ActorIdentity {
            if (actorId == null || actorId.isBlank() || generation <= 0) {
                throw protocol("invalid Actor identity");
            }
        }
    }

    public record BoundSessionSend(
        ActorRouteFence actor,
        long expectedBindingGeneration) {
        public BoundSessionSend {
            Objects.requireNonNull(actor, "actor");
            if (expectedBindingGeneration <= 0) {
                throw protocol(
                    "expectedBindingGeneration must be nonzero");
            }
        }
    }

    public record BoundSessionBind(
        long correlation,
        ActorRouteFence actor,
        RoutingId sessionRid,
        boolean active,
        long bindingGeneration) {
        public BoundSessionBind {
            Objects.requireNonNull(actor, "actor");
            Objects.requireNonNull(sessionRid, "sessionRid");
            if (correlation <= 0 || bindingGeneration <= 0) {
                throw protocol(
                    "bound session generations must be nonzero");
            }
        }
    }

    public record LogicalMulticast(
        int flags,
        String channelName,
        String topic,
        RoutingId sourceSpotRid) {
    }

    public record InstanceRouteFence(
        RoutingId targetNodeRid,
        long targetNodeGeneration,
        RoutingId targetSpotRid,
        long objectGeneration,
        String ownerId,
        long authorityOwnerGeneration,
        long leaseGeneration,
        String storeVersion) {
        public InstanceRouteFence {
            Objects.requireNonNull(targetNodeRid, "targetNodeRid");
            Objects.requireNonNull(targetSpotRid, "targetSpotRid");
            if (targetNodeGeneration <= 0
                || objectGeneration <= 0
                || authorityOwnerGeneration <= 0
                || leaseGeneration <= 0) {
                throw protocol(
                    "Instance route generations must be nonzero");
            }
            if (ownerId == null || ownerId.isBlank()
                || storeVersion == null || storeVersion.isBlank()) {
                throw protocol(
                    "Instance route owner and store version are required");
            }
        }
    }

    public record InstanceSpotMessage(
        int flags,
        InstanceRouteFence route,
        long sourceNodeGeneration,
        RoutingId sourceNodeRid,
        RoutingId sourceSpotRid,
        boolean request,
        long operationHigh,
        long operationLow,
        Long replyRouteId) {
        public InstanceSpotMessage {
            Objects.requireNonNull(route, "route");
            Objects.requireNonNull(sourceNodeRid, "sourceNodeRid");
        }
    }

    private static Writer prefix(int command, int flags) {
        Writer result = new Writer();
        result.u8(ServiceWireConstants.MAGIC_0);
        result.u8(ServiceWireConstants.MAGIC_1);
        result.u8(ServiceWireConstants.WIRE_MAJOR);
        result.u8(command);
        result.u8(flags);
        return result;
    }

    private static void writeActorRoute(
        Writer writer,
        ActorRouteFence target) {
        Objects.requireNonNull(target, "target");
        writer.text8(target.actor().actorId(), "actorId");
        writer.nonzero(target.actor().generation(), "actorGeneration");
        writer.rid(target.actor().nodeRid(), "targetNodeRid");
        writer.nonzero(
            target.targetNodeGeneration(), "targetNodeGeneration");
        writer.nonzero(
            target.authorityOwnerGeneration(),
            "expectedAuthorityOwnerGeneration");
    }

    private static ActorRouteFence readActorRoute(Reader reader) {
        String actorId = reader.text8("actorId");
        long actorGeneration = reader.nonzeroU64("actorGeneration");
        RoutingId targetNodeRid = reader.rid("targetNodeRid");
        return new ActorRouteFence(
            new ZLinkBackendActorRef(
                targetNodeRid, actorId, actorGeneration),
            reader.nonzeroU64("targetNodeGeneration"),
            reader.nonzeroU64("expectedAuthorityOwnerGeneration"));
    }

    private static ZLinkServiceWireException protocol(String message) {
        return new ZLinkServiceWireException(message);
    }

    private record Header(int command, int flags) {
    }

    private static final class Writer {
        private final ByteArrayOutputStream output = new ByteArrayOutputStream();

        void u8(int value) {
            if (value < 0 || value > 0xff) {
                throw protocol("value exceeds u8");
            }
            output.write(value);
        }

        void u64(long value) {
            if (value < 0) {
                throw protocol("value exceeds supported u64 range");
            }
            output.writeBytes(ByteBuffer.allocate(Long.BYTES)
                .order(ByteOrder.BIG_ENDIAN)
                .putLong(value)
                .array());
        }

        void u16(int value) {
            if (value < 0 || value > 0xffff) {
                throw protocol("value exceeds u16");
            }
            output.write((value >>> 8) & 0xff);
            output.write(value & 0xff);
        }

        void nonzero(long value, String field) {
            if (value <= 0) {
                throw protocol(field + " must be nonzero");
            }
            u64(value);
        }

        void rid(RoutingId value, String field) {
            byte[] bytes = Objects.requireNonNull(value, field).toBytes();
            if (bytes.length == 0 || bytes.length > 0xff) {
                throw protocol(field + " exceeds rid bound");
            }
            u8(bytes.length);
            output.writeBytes(bytes);
        }

        void text8(String value, String field) {
            byte[] bytes = Objects.requireNonNull(value, field)
                .getBytes(StandardCharsets.UTF_8);
            if (bytes.length == 0
                || bytes.length > 0xff
                || value.indexOf('\0') >= 0) {
                throw protocol(field + " exceeds text8");
            }
            u8(bytes.length);
            output.writeBytes(bytes);
        }

        void text16(String value, String field) {
            byte[] bytes = Objects.requireNonNull(value, field)
                .getBytes(StandardCharsets.UTF_8);
            if (bytes.length == 0
                || bytes.length > 0xffff
                || value.indexOf('\0') >= 0) {
                throw protocol(field + " exceeds text16");
            }
            u16(bytes.length);
            output.writeBytes(bytes);
        }

        void optionalRid(RoutingId value, String field) {
            if (value == null) {
                u8(0);
                return;
            }
            rid(value, field);
        }

        void bytes(byte[] value) {
            output.writeBytes(value);
        }

        byte[] toByteArray() {
            return output.toByteArray();
        }
    }

    private static final class Reader {
        private final ByteBuffer input;

        Reader(byte[] value) {
            Objects.requireNonNull(value, "value");
            input = ByteBuffer.wrap(value).order(ByteOrder.BIG_ENDIAN);
        }

        Header prefix() {
            if (input.remaining() < PREFIX_BYTES
                || u8("magic0") != ServiceWireConstants.MAGIC_0
                || u8("magic1") != ServiceWireConstants.MAGIC_1
                || u8("major") != ServiceWireConstants.WIRE_MAJOR) {
                throw protocol("invalid service wire prefix");
            }
            return new Header(u8("command"), u8("flags"));
        }

        int u8(String field) {
            require(1, field);
            return Byte.toUnsignedInt(input.get());
        }

        long nonzeroU64(String field) {
            require(Long.BYTES, field);
            long value = input.getLong();
            if (value <= 0) {
                throw protocol(field + " must be nonzero");
            }
            return value;
        }

        long u64(String field) {
            require(Long.BYTES, field);
            long value = input.getLong();
            if (value < 0) {
                throw protocol(field + " exceeds supported u64 range");
            }
            return value;
        }

        int u16(String field) {
            require(Short.BYTES, field);
            return Short.toUnsignedInt(input.getShort());
        }

        RoutingId rid(String field) {
            int length = u8(field + ".length");
            if (length == 0) {
                throw protocol(field + " must not be empty");
            }
            byte[] bytes = new byte[length];
            require(length, field);
            input.get(bytes);
            return RoutingId.from(bytes);
        }

        RoutingId optionalRid(String field) {
            int length = u8(field + ".length");
            if (length == 0) {
                return null;
            }
            byte[] bytes = new byte[length];
            require(length, field);
            input.get(bytes);
            return RoutingId.from(bytes);
        }

        String optionalText8(String field) {
            int length = u8(field + ".length");
            return length == 0 ? null : text(length, field);
        }

        String text8(String field) {
            int length = u8(field + ".length");
            if (length == 0) {
                throw protocol(field + " must not be empty");
            }
            return text(length, field);
        }

        String text16(String field) {
            int length = u16(field + ".length");
            if (length == 0) {
                throw protocol(field + " must not be empty");
            }
            return text(length, field);
        }

        int position() {
            return input.position();
        }

        void end() {
            if (input.hasRemaining()) {
                throw protocol("trailing bytes are forbidden");
            }
        }

        private void require(int length, String field) {
            if (input.remaining() < length) {
                throw protocol("truncated " + field);
            }
        }

        private String text(int length, String field) {
            byte[] bytes = new byte[length];
            require(length, field);
            input.get(bytes);
            try {
                String value = StandardCharsets.UTF_8.newDecoder()
                    .onMalformedInput(CodingErrorAction.REPORT)
                    .onUnmappableCharacter(CodingErrorAction.REPORT)
                    .decode(ByteBuffer.wrap(bytes))
                    .toString();
                if (value.indexOf('\0') >= 0
                    || !java.util.Arrays.equals(
                        bytes,
                        value.getBytes(StandardCharsets.UTF_8))) {
                    throw protocol(field + " is not canonical UTF-8");
                }
                return value;
            } catch (CharacterCodingException failure) {
                throw protocol(field + " is not canonical UTF-8");
            }
        }
    }
}

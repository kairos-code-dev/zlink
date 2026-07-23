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
        if ((flags & ~ServiceWireConstants.FLAG_METADATA) != 0
            || request != (correlation != null)
            || (correlation != null && correlation <= 0)) {
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
        if ((header.flags() & ~ServiceWireConstants.FLAG_METADATA) != 0) {
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
        reader.end();
        return new ActorMessage(
            request,
            header.flags(),
            correlation,
            sourceActor,
            target);
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
        ActorRouteFence target) {
    }

    public record ActorIdentity(String actorId, long generation) {
        public ActorIdentity {
            if (actorId == null || actorId.isBlank() || generation <= 0) {
                throw protocol("invalid Actor identity");
            }
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

package systems.zlink.framework.locations.redis;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import java.time.Instant;
import java.time.ZoneOffset;
import java.time.format.DateTimeFormatter;
import java.time.format.DateTimeFormatterBuilder;
import java.time.temporal.ChronoField;
import java.util.ArrayList;
import java.util.Base64;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ActorRef;
import systems.zlink.framework.locations.ZLinkActorLocation;
import systems.zlink.framework.locations.ZLinkLocationAutoConnectType;
import systems.zlink.framework.locations.ZLinkLocationRole;
import systems.zlink.framework.locations.ZLinkPeerLocation;
import systems.zlink.framework.locations.ZLinkRouteKind;
import systems.zlink.framework.locations.ZLinkRouteLocation;
import systems.zlink.framework.locations.ZLinkSpotLocation;
import systems.zlink.framework.spots.ZLinkSpotKind;

final class ZLinkRedisLocationRowJson {
    private static final ObjectMapper JSON = new ObjectMapper();
    private static final DateTimeFormatter INSTANT_WIRE_FORMAT =
        new DateTimeFormatterBuilder()
            .appendPattern("yyyy-MM-dd'T'HH:mm:ss")
            .appendFraction(ChronoField.NANO_OF_SECOND, 0, 9, true)
            .appendOffset("+HH:MM", "+00:00")
            .toFormatter()
            .withZone(ZoneOffset.UTC);

    private ZLinkRedisLocationRowJson() {
    }

    static String serializePeer(ZLinkPeerLocation row) {
        ObjectNode node = JSON.createObjectNode();
        node.put("AutoConnectType", autoConnectTypeNumber(row.autoConnectType()));
        node.put("MeshName", row.meshName());
        putRid(node, "NodeRid", row.nodeRid());
        node.put("Role", row.role() == null ? 0 : row.role().value());
        node.put("Endpoint", row.endpoint());
        node.put("Weight", row.weight());
        node.put("Draining", row.draining());
        node.put("Value", row.value());
        putStringMap(node, "Metadata", row.metadata());
        putStringList(node, "Capabilities", row.capabilities());
        node.put("OwnerId", row.ownerId());
        node.put("Generation", row.generation());
        putInstant(node, "UpdatedAt", row.updatedAt());
        return write(node);
    }

    static ZLinkPeerLocation deserializePeer(String json, long generation, Instant updatedAt) {
        JsonNode node = read(json);
        return new ZLinkPeerLocation(
            autoConnectType(node.path("AutoConnectType").asInt()),
            text(node, "MeshName"),
            rid(node, "NodeRid"),
            role(node.path("Role").asInt()),
            text(node, "Endpoint"),
            node.path("Weight").asLong(),
            node.path("Draining").asBoolean(),
            node.path("Value").asLong(),
            stringMap(node.path("Metadata")),
            stringList(node.path("Capabilities")),
            text(node, "OwnerId"),
            generation,
            updatedAt);
    }

    static String serializeSpot(ZLinkSpotLocation row) {
        ObjectNode node = JSON.createObjectNode();
        node.put("MeshName", row.meshName());
        putRid(node, "SpotRid", row.spotRid());
        node.put("SpotGeneration", row.spotGeneration());
        putNullableText(node, "SpotType", row.spotType());
        putRid(node, "NodeRid", row.nodeRid());
        node.put("SpotKind", spotKindNumber(row.spotKind()));
        putNullableText(node, "RouteEndpoint", row.routeEndpoint());
        node.put("OwnerId", row.ownerId());
        node.put("Generation", row.generation());
        putInstant(node, "UpdatedAt", row.updatedAt());
        return write(node);
    }

    static ZLinkSpotLocation deserializeSpot(String json, long generation, Instant updatedAt) {
        JsonNode node = read(json);
        return new ZLinkSpotLocation(
            text(node, "MeshName"),
            rid(node, "SpotRid"),
            node.path("SpotGeneration").asLong(generation),
            nullableText(node, "SpotType"),
            rid(node, "NodeRid"),
            spotKind(node.path("SpotKind").asInt()),
            nullableText(node, "RouteEndpoint"),
            text(node, "OwnerId"),
            generation,
            updatedAt);
    }

    static String serializeActor(ZLinkActorLocation row) {
        ObjectNode node = JSON.createObjectNode();
        node.put("ActorId", row.actorId());
        putNullableText(node, "ActorType", row.actorType());
        putActorRef(node, "ActorRef", row.actorRef());
        putRid(node, "NodeRid", row.nodeRid());
        node.put("LocationKind", row.locationKind().value());
        node.put("SpotMeshName", row.spotMeshName());
        putRid(node, "SpotRid", row.spotRid());
        node.put("OwnerId", row.ownerId());
        node.put("Generation", row.generation());
        putInstant(node, "UpdatedAt", row.updatedAt());
        return write(node);
    }

    static ZLinkActorLocation deserializeActor(String json, long generation, Instant updatedAt) {
        JsonNode node = read(json);
        return new ZLinkActorLocation(
            text(node, "ActorId"),
            nullableText(node, "ActorType"),
            actorRef(node.get("ActorRef")),
            rid(node, "NodeRid"),
            spotKind(node.path("LocationKind").asInt()),
            text(node, "SpotMeshName"),
            rid(node, "SpotRid"),
            text(node, "OwnerId"),
            generation,
            updatedAt);
    }

    static String serializeRoute(ZLinkRouteLocation row) {
        ObjectNode node = JSON.createObjectNode();
        node.put("RouteKind", row.routeKind().value());
        node.put("RouteKey", row.routeKey());
        putRid(node, "OwnerNodeRid", row.ownerNodeRid());
        node.put("OwnerId", row.ownerId());
        node.put("Generation", row.generation());
        node.put("Value", Base64.getEncoder().encodeToString(row.value() == null ? new byte[0] : row.value()));
        putInstant(node, "UpdatedAt", row.updatedAt());
        return write(node);
    }

    static ZLinkRouteLocation deserializeRoute(String json, long generation, Instant updatedAt) {
        JsonNode node = read(json);
        String value = nullableText(node, "Value");
        return new ZLinkRouteLocation(
            routeKind(node.path("RouteKind").asInt()),
            text(node, "RouteKey"),
            rid(node, "OwnerNodeRid"),
            text(node, "OwnerId"),
            generation,
            value == null ? new byte[0] : Base64.getDecoder().decode(value),
            updatedAt);
    }

    private static JsonNode read(String json) {
        try {
            return JSON.readTree(json);
        } catch (JsonProcessingException ex) {
            throw new IllegalArgumentException("invalid Redis location row JSON", ex);
        }
    }

    private static String write(ObjectNode node) {
        try {
            return JSON.writeValueAsString(node);
        } catch (JsonProcessingException ex) {
            throw new IllegalStateException("failed to serialize Redis location row", ex);
        }
    }

    private static void putRid(ObjectNode node, String field, RoutingId rid) {
        if (rid == null) {
            node.putNull(field);
        } else {
            node.put(field, rid.size() == 0 ? "" : rid.toHex());
        }
    }

    private static RoutingId rid(JsonNode node, String field) {
        String value = nullableText(node, field);
        return value == null || value.isEmpty() ? null : RoutingId.fromHex(value);
    }

    private static void putInstant(ObjectNode node, String field, Instant value) {
        if (value == null) {
            node.put(field, Instant.EPOCH.toString());
        } else {
            node.put(field, INSTANT_WIRE_FORMAT.format(value));
        }
    }

    private static void putNullableText(ObjectNode node, String field, String value) {
        if (value == null) {
            node.putNull(field);
        } else {
            node.put(field, value);
        }
    }

    private static String text(JsonNode node, String field) {
        String value = nullableText(node, field);
        return value == null ? "" : value;
    }

    private static String nullableText(JsonNode node, String field) {
        JsonNode value = node.get(field);
        return value == null || value.isNull() ? null : value.asText();
    }

    private static void putStringMap(ObjectNode node, String field, Map<String, String> value) {
        if (value == null) {
            node.putNull(field);
            return;
        }
        ObjectNode map = JSON.createObjectNode();
        value.forEach(map::put);
        node.set(field, map);
    }

    private static Map<String, String> stringMap(JsonNode node) {
        if (node == null || node.isNull()) {
            return null;
        }
        Map<String, String> result = new HashMap<>();
        Iterator<Map.Entry<String, JsonNode>> fields = node.fields();
        while (fields.hasNext()) {
            Map.Entry<String, JsonNode> field = fields.next();
            result.put(field.getKey(), field.getValue().asText());
        }
        return Map.copyOf(result);
    }

    private static void putStringList(ObjectNode node, String field, List<String> value) {
        if (value == null) {
            node.putNull(field);
            return;
        }
        ArrayNode array = JSON.createArrayNode();
        value.forEach(array::add);
        node.set(field, array);
    }

    private static List<String> stringList(JsonNode node) {
        if (node == null || node.isNull()) {
            return null;
        }
        List<String> result = new ArrayList<>();
        node.forEach(item -> result.add(item.asText()));
        return List.copyOf(result);
    }

    private static ZLinkLocationAutoConnectType autoConnectType(int value) {
        return ZLinkLocationAutoConnectType.fromValue(value);
    }

    private static ZLinkLocationRole role(int value) {
        return ZLinkLocationRole.fromValue(value);
    }

    private static int spotKindNumber(ZLinkSpotKind value) {
        return value == null ? 0 : value.value();
    }

    private static ZLinkSpotKind spotKind(int value) {
        return ZLinkSpotKind.fromValue(value);
    }

    private static ZLinkRouteKind routeKind(int value) {
        return ZLinkRouteKind.fromValue(value);
    }

    private static int autoConnectTypeNumber(ZLinkLocationAutoConnectType value) {
        return value == null ? 0 : value.value();
    }

    private static void putActorRef(ObjectNode node, String field, ActorRef value) {
        if (value == null) {
            node.putNull(field);
            return;
        }
        ObjectNode actorRef = JSON.createObjectNode();
        putRid(actorRef, "nodeRid", value.nodeRid());
        actorRef.put("actorId", value.actorId());
        actorRef.put("generation", value.generation());
        node.set(field, actorRef);
    }

    private static ActorRef actorRef(JsonNode node) {
        if (node == null || node.isNull()) {
            return null;
        }
        if (node.isTextual()) {
            String legacy = node.asText();
            return legacy.isBlank() ? null : new ActorRef(null, legacy, 0);
        }
        return new ActorRef(
            rid(node, "nodeRid"),
            text(node, "actorId"),
            node.path("generation").asLong());
    }
}

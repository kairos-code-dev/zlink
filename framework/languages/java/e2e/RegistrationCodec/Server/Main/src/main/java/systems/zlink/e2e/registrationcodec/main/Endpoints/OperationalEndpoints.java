package systems.zlink.e2e.registrationcodec.main.Endpoints;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.sun.net.httpserver.HttpServer;
import com.google.protobuf.StringValue;
import java.net.InetSocketAddress;
import java.net.URI;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import systems.zlink.e2e.registrationcodec.shared.Contracts;
import systems.zlink.framework.channels.ZLinkClient;
import org.springframework.context.SmartLifecycle;
import systems.zlink.e2e.registrationcodec.main.Infrastructure.EvidenceStore;

public final class OperationalEndpoints implements SmartLifecycle {
    private final EvidenceStore state;
    private final ObjectMapper json;
    private final ZLinkClient client;
    private final String endpoint;
    private HttpServer server;
    private boolean running;

    public OperationalEndpoints(EvidenceStore state, ObjectMapper json, ZLinkClient client, String endpoint) {
        this.state = state;
        this.json = json;
        this.client = client;
        this.endpoint = endpoint;
    }

    @Override
    public void start() {
        if (endpoint == null || endpoint.isBlank()) {
            return;
        }
        try {
            URI uri = URI.create(endpoint);
            server = HttpServer.create(new InetSocketAddress(uri.getHost(), uri.getPort()), 0);
            server.createContext("/health", exchange -> {
                byte[] body = "ok\n".getBytes(StandardCharsets.UTF_8);
                exchange.sendResponseHeaders(200, body.length);
                exchange.getResponseBody().write(body);
                exchange.close();
            });
            server.createContext("/evidence", exchange -> {
                byte[] body = json.writeValueAsBytes(state.snapshot());
                exchange.getResponseHeaders().add("Content-Type", "application/json");
                exchange.sendResponseHeaders(200, body.length);
                exchange.getResponseBody().write(body);
                exchange.close();
            });
            server.createContext("/registration/auto", exchange -> writeJson(exchange, registrationAuto()));
            server.createContext("/registration/attribute", exchange -> writeJson(exchange, registrationAttribute()));
            server.createContext("/registration/manual", exchange -> writeJson(exchange, registrationManual()));
            server.createContext("/registration/di-filter-order", exchange -> writeJson(exchange, registrationDi()));
            server.createContext("/registration/filter-order", exchange -> writeJson(exchange, registrationFilterOrder()));
            server.createContext("/codec/roundtrip", exchange -> writeJson(exchange, codecRoundtrip()));
            server.start();
            running = true;
        } catch (Exception error) {
            throw new IllegalStateException("failed to start evidence endpoint " + endpoint, error);
        }
    }

    @Override
    public void stop() {
        if (server != null) {
            server.stop(0);
            server = null;
        }
        running = false;
    }

    @Override
    public boolean isRunning() {
        return running;
    }

    private Contracts.EchoRes registrationAuto() {
        Contracts.EchoRes reply = client.requestToChannel(
                Contracts.CHANNEL,
                new Contracts.EchoAutoReq("auto-request"))
            .packetName("EchoAuto")
            .timeout(Duration.ofSeconds(5))
            .await(Contracts.EchoRes.class);
        client.sendToChannel(Contracts.CHANNEL, new Contracts.EchoAutoMsg("auto-send"))
            .packetName("EchoAuto")
            .await();
        return reply;
    }

    private Contracts.EchoRes registrationAttribute() {
        Contracts.EchoRes reply = client.requestToChannel(
                Contracts.CHANNEL,
                new Contracts.EchoAttrReq("attr-request"))
            .packetName("EchoAttr")
            .timeout(Duration.ofSeconds(5))
            .await(Contracts.EchoRes.class);
        client.sendToChannel(Contracts.CHANNEL, new Contracts.EchoAttrMsg("attr-send"))
            .packetName("EchoAttr")
            .await();
        return reply;
    }

    private Contracts.EchoRes registrationManual() {
        Contracts.EchoRes reply = client.requestToChannel(
                Contracts.CHANNEL,
                new Contracts.EchoManualReq("manual-request"))
            .packetName("EchoManual")
            .timeout(Duration.ofSeconds(5))
            .await(Contracts.EchoRes.class);
        client.sendToChannel(Contracts.CHANNEL, new Contracts.EchoManualMsg("manual-send"))
            .packetName("EchoManual")
            .await();
        return reply;
    }

    private java.util.List<Contracts.DiLifecycleRes> registrationDi() {
        java.util.List<Contracts.DiLifecycleRes> replies = new java.util.ArrayList<>();
        for (int index = 0; index < 3; index++) {
            replies.add(client.requestToChannel(
                    Contracts.CHANNEL,
                    new Contracts.DiLifecycleReq("di-" + index))
                .packetName("DiLifecycle")
                .timeout(Duration.ofSeconds(5))
                .await(Contracts.DiLifecycleRes.class));
        }
        return replies;
    }

    private Contracts.EchoRes registrationFilterOrder() {
        return client.requestToChannel(
                Contracts.CHANNEL,
                new Contracts.EchoManualReq("filter-order-request"))
            .packetName("EchoManual")
            .timeout(Duration.ofSeconds(5))
            .await(Contracts.EchoRes.class);
    }

    private Contracts.CodecScenarioRes codecRoundtrip() {
        Contracts.EchoRes jsonReply = client.requestToChannel(
                Contracts.CHANNEL,
                new Contracts.JsonEchoReq("json-request"))
            .packetName("JsonEcho")
            .timeout(Duration.ofSeconds(5))
            .await(Contracts.EchoRes.class);
        client.sendToChannel(Contracts.CHANNEL, new Contracts.JsonEchoMsg("json-send"))
            .packetName("JsonEcho")
            .await();

        StringValue protobufReply = client.requestToChannel(
                Contracts.CHANNEL,
                StringValue.of("protobuf-request"))
            .packetName("ProtobufEcho")
            .timeout(Duration.ofSeconds(5))
            .await(StringValue.class);
        client.sendToChannel(Contracts.CHANNEL, StringValue.of("protobuf-send"))
            .packetName("ProtobufEcho")
            .await();

        Contracts.PackedEchoRes packedReply = client.requestToChannel(
                Contracts.CHANNEL,
                new Contracts.PackedEchoReq("msgpack-request"))
            .packetName("MsgpackEcho")
            .timeout(Duration.ofSeconds(5))
            .await(Contracts.PackedEchoRes.class);
        client.sendToChannel(Contracts.CHANNEL, new Contracts.PackedEchoMsg("msgpack-send"))
            .packetName("MsgpackEcho")
            .await();

        return new Contracts.CodecScenarioRes(jsonReply, protobufReply.getValue(), packedReply.value());
    }

    private void writeJson(com.sun.net.httpserver.HttpExchange exchange, Object value) throws java.io.IOException {
        if (!"POST".equals(exchange.getRequestMethod()) && !"/health".equals(exchange.getRequestURI().getPath())) {
            byte[] body = "method not allowed\n".getBytes(StandardCharsets.UTF_8);
            exchange.sendResponseHeaders(405, body.length);
            exchange.getResponseBody().write(body);
            exchange.close();
            return;
        }
        try {
            byte[] body = json.writeValueAsBytes(value);
            exchange.getResponseHeaders().add("Content-Type", "application/json");
            exchange.sendResponseHeaders(200, body.length);
            exchange.getResponseBody().write(body);
        } catch (RuntimeException error) {
            byte[] body = error.toString().getBytes(StandardCharsets.UTF_8);
            exchange.sendResponseHeaders(500, body.length);
            exchange.getResponseBody().write(body);
        } finally {
            exchange.close();
        }
    }
}

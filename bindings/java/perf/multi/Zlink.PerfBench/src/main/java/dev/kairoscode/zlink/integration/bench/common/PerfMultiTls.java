/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.integration.bench.common;

import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.options.SocketOptions;
import dev.kairoscode.zlink.service.gateway.Gateway;
import dev.kairoscode.zlink.service.receiver.Receiver;
import dev.kairoscode.zlink.service.spot.SpotNode;
import java.net.URISyntaxException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

public final class PerfMultiTls {
    private PerfMultiTls() {
    }

    public static void configureTlsServerIfNeeded(Socket socket,
                                                  String transport) {
        if (!isSecureTransport(transport)) {
            return;
        }
        TlsPaths paths = requireTlsPaths();
        socket.setOption(SocketOptions.TLS_CERT, paths.serverCert().toString());
        socket.setOption(SocketOptions.TLS_KEY, paths.serverKey().toString());
    }

    public static void configureTlsClientIfNeeded(Socket socket,
                                                  String transport) {
        if (!isSecureTransport(transport)) {
            return;
        }
        TlsPaths paths = requireTlsPaths();
        socket.setOption(SocketOptions.TLS_CA, paths.caCert().toString());
        socket.setOption(SocketOptions.TLS_TRUST_SYSTEM, 0);
        socket.setOption(SocketOptions.TLS_HOSTNAME, "localhost");
    }

    public static void configureGatewayTlsClientIfNeeded(Gateway gateway,
                                                         String transport) {
        if (!isSecureTransport(transport)) {
            return;
        }
        TlsPaths paths = requireTlsPaths();
        gateway.setTlsClient(paths.caCert().toString(), "localhost", 0);
    }

    public static void configureReceiverTlsServerIfNeeded(Receiver receiver,
                                                          String transport) {
        if (!isSecureTransport(transport)) {
            return;
        }
        TlsPaths paths = requireTlsPaths();
        receiver.setTlsServer(paths.serverCert().toString(),
            paths.serverKey().toString());
    }

    public static void configureSpotPublisherTlsIfNeeded(SpotNode spotNode,
                                                         String transport) {
        if (!isSecureTransport(transport)) {
            return;
        }
        TlsPaths paths = requireTlsPaths();
        spotNode.setTlsServer(paths.serverCert().toString(),
            paths.serverKey().toString());
    }

    public static void configureSpotSubscriberTlsIfNeeded(SpotNode spotNode,
                                                          String transport) {
        if (!isSecureTransport(transport)) {
            return;
        }
        TlsPaths paths = requireTlsPaths();
        spotNode.setTlsClient(paths.caCert().toString(), "localhost", 0);
    }

    public static TlsPaths tryResolvePerfTlsPaths() {
        List<Path> starts = new ArrayList<>();
        starts.add(Path.of("").toAbsolutePath().normalize());

        try {
            Path codePath = Path.of(PerfMultiTls.class.getProtectionDomain()
                .getCodeSource().getLocation().toURI()).toAbsolutePath();
            starts.add(Files.isDirectory(codePath) ? codePath
                : codePath.getParent());
        } catch (URISyntaxException | RuntimeException ignored) {
        }

        for (Path start : starts) {
            if (start == null) {
                continue;
            }
            Path current = start;
            while (current != null) {
                TlsPaths found = scanCandidate(current.resolve("bindings")
                    .resolve("java").resolve("tests").resolve("certs"));
                if (found != null) {
                    return found;
                }

                found = scanCandidate(current.resolve("tests").resolve("certs"));
                if (found != null) {
                    return found;
                }

                current = current.getParent();
            }
        }

        return null;
    }

    public static boolean isSecureTransport(String transport) {
        if (transport == null) {
            return false;
        }
        String tr = transport.trim().toLowerCase();
        return "tls".equals(tr) || "wss".equals(tr);
    }

    private static TlsPaths requireTlsPaths() {
        TlsPaths paths = tryResolvePerfTlsPaths();
        if (paths == null) {
            throw new IllegalStateException(
                "TLS certs not found under bindings/java/tests/certs");
        }
        return paths;
    }

    private static TlsPaths scanCandidate(Path certDir) {
        Path cert = certDir.resolve("server.crt");
        Path key = certDir.resolve("server.key");
        Path ca = certDir.resolve("ca.crt");
        if (Files.isRegularFile(cert) && Files.isRegularFile(key)
            && Files.isRegularFile(ca)) {
            return new TlsPaths(cert.toAbsolutePath().normalize(),
                key.toAbsolutePath().normalize(),
                ca.toAbsolutePath().normalize());
        }
        return null;
    }

    public record TlsPaths(Path serverCert, Path serverKey, Path caCert) {
    }
}

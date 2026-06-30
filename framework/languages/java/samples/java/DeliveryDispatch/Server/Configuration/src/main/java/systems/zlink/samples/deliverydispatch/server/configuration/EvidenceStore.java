package systems.zlink.samples.deliverydispatch.server.configuration;

import java.io.IOException;
import java.io.UncheckedIOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.StandardOpenOption;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;

public final class EvidenceStore {
    private final Path path;
    private final Object gate = new Object();

    public EvidenceStore() {
        String directory = System.getProperty(
            "zlink.samples.deliverydispatch.workDir",
            System.getenv().getOrDefault(
                "DELIVERYDISPATCH_WORK_DIR",
                Paths.get(System.getProperty("java.io.tmpdir"), "zlink-deliverydispatch").toString()));
        Path dir = Paths.get(directory);
        try {
            Files.createDirectories(dir);
        } catch (IOException ex) {
            throw new UncheckedIOException("Failed to create evidence directory.", ex);
        }
        this.path = dir.resolve("events.log");
    }

    public void append(Messages.DeliveryStatusReq status) {
        String line = String.join(
            "|",
            status.deliveryId(),
            status.status(),
            status.courierId() == null ? "" : status.courierId(),
            Long.toString(status.occurredAtUnixMs()));
        synchronized (gate) {
            try {
                Files.write(
                    path,
                    List.of(line),
                    StandardCharsets.UTF_8,
                    StandardOpenOption.CREATE,
                    StandardOpenOption.APPEND);
            } catch (IOException ex) {
                throw new UncheckedIOException("Failed to append delivery evidence.", ex);
            }
        }
    }

    public List<String> readLines() {
        synchronized (gate) {
            if (!Files.exists(path)) {
                return List.of();
            }
            try {
                return Files.readAllLines(path, StandardCharsets.UTF_8);
            } catch (IOException ex) {
                throw new UncheckedIOException("Failed to read delivery evidence.", ex);
            }
        }
    }

    public boolean hasSequence(String deliveryId, String... expected) {
        List<String> statuses = new ArrayList<>();
        for (String raw : readLines()) {
            String[] parts = raw.split("\\|");
            if (parts.length >= 2 && deliveryId.equals(parts[0])) {
                statuses.add(parts[1]);
            }
        }
        return statuses.equals(Arrays.asList(expected));
    }
}

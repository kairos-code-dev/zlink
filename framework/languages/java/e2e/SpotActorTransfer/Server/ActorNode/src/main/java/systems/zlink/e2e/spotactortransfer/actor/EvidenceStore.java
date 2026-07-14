package systems.zlink.e2e.spotactortransfer.actor;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.util.List;
import java.util.concurrent.CopyOnWriteArrayList;
import systems.zlink.e2e.spotactortransfer.shared.Contracts;

public final class EvidenceStore {
    private final String nodeRid;
    private final Path file;
    private final CopyOnWriteArrayList<Contracts.Evidence> entries = new CopyOnWriteArrayList<>();

    public EvidenceStore(String nodeRid, String file) {
        this.nodeRid = nodeRid;
        this.file = Path.of(file);
        try {
            Files.createDirectories(this.file.getParent());
        } catch (IOException error) {
            throw new IllegalStateException("failed to prepare evidence directory", error);
        }
    }

    public String nodeRid() {
        return nodeRid;
    }

    public void add(String scenario, String actorId, String kind, String value) {
        Contracts.Evidence evidence = new Contracts.Evidence(
            scenario == null ? "" : scenario,
            actorId == null ? "" : actorId,
            kind,
            value == null ? "" : value,
            nodeRid);
        entries.add(evidence);
        try {
            Files.writeString(
                file,
                evidence.text() + System.lineSeparator(),
                StandardOpenOption.CREATE,
                StandardOpenOption.APPEND);
        } catch (IOException error) {
            throw new IllegalStateException("failed to write transfer evidence", error);
        }
    }

    public Contracts.EvidenceSnapshot snapshot() {
        return new Contracts.EvidenceSnapshot(List.copyOf(entries));
    }
}

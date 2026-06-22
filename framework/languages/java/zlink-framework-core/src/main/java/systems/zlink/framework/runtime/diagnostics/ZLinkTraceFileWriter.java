package systems.zlink.framework.runtime.diagnostics;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;

// Minimal thread-safe append writer for the dedicated tracing file, mirroring the
// C++ file sink (open-append per line). Creates parent directories.
final class ZLinkTraceFileWriter {
    private static final Object GATE = new Object();

    private ZLinkTraceFileWriter() {
    }

    static void write(String path, String line) {
        try {
            Path file = Path.of(path);
            synchronized (GATE) {
                Path parent = file.getParent();
                if (parent != null) {
                    Files.createDirectories(parent);
                }
                Files.writeString(
                    file,
                    line + System.lineSeparator(),
                    StandardCharsets.UTF_8,
                    StandardOpenOption.CREATE,
                    StandardOpenOption.APPEND);
            }
        } catch (IOException ignored) {
            // best-effort: tracing must never break dispatch.
        }
    }
}

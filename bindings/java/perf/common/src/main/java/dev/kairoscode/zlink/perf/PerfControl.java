/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.atomic.AtomicBoolean;

public final class PerfControl {
    private PerfControl() {
    }

    public static void emitReady(String endpoint) {
        emitLine("READY," + endpoint);
    }

    public static void emitClientReady(int size) {
        emitLine("CLIENT_READY," + size);
    }

    public static void awaitStart(int size, String label) {
        String expected = "START," + size;
        try {
            BufferedReader reader = new BufferedReader(
                new InputStreamReader(System.in, StandardCharsets.UTF_8));
            String line;
            while ((line = reader.readLine()) != null) {
                if (expected.equals(line) || ("PHASE_ACTIVE," + size).equals(line)) {
                    return;
                }
            }
        } catch (java.io.IOException ex) {
            throw new IllegalStateException(label + " control read failed", ex);
        }
        throw new IllegalStateException(label + " missing " + expected);
    }

    public static AtomicBoolean watchStopSignal(String label) {
        AtomicBoolean stopRequested = new AtomicBoolean(false);
        Thread watcher = new Thread(() -> {
            try {
                BufferedReader reader = new BufferedReader(
                    new InputStreamReader(System.in, StandardCharsets.UTF_8));
                String line;
                while ((line = reader.readLine()) != null) {
                    if ("STOP".equals(line) || "QUIT".equals(line)) {
                        stopRequested.set(true);
                        return;
                    }
                }
            } catch (java.io.IOException ex) {
                throw new IllegalStateException(label + " control read failed", ex);
            }
        }, label + "-control");
        watcher.setDaemon(true);
        watcher.start();
        return stopRequested;
    }

    private static void emitLine(String line) {
        System.out.println(line);
        System.out.flush();
    }
}

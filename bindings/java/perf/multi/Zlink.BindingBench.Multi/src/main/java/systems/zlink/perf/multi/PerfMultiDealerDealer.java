/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.perf.multi;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;

import systems.zlink.contracts.Context;
import systems.zlink.contracts.DealerSocket;
import systems.zlink.contracts.Message;
import systems.zlink.contracts.MonitorEventType;
import systems.zlink.contracts.MonitorSocket;
import systems.zlink.contracts.PollEventFlag;
import systems.zlink.contracts.SendFlags;
import systems.zlink.contracts.SocketType;
import systems.zlink.contracts.SubmitException;
import systems.zlink.contracts.SubmitResult;
import systems.zlink.contracts.ZlinkException;
import systems.zlink.perf.PerfControl;
import systems.zlink.perf.PerfSocketPollSet;
import systems.zlink.perf.PerfStopToken;
import systems.zlink.perf.PerfUtil;
import java.util.ArrayList;
import java.time.Duration;
import java.util.List;

final class PerfMultiDealerDealer {
    private static final MonitorEventType READY_EVENT = MonitorEventType.CONNECTION_READY;

    private PerfMultiDealerDealer() {
    }

    static PerfUtil.Result runServer(PerfUtil.Config config) {
        try (Context ctx = PerfUtil.newContext(config);
             DealerSocket server = new DealerSocket(ctx);
             PerfSocketPollSet pollSet = PerfSocketPollSet.fromSockets(
                 List.of(server), PollEventFlag.POLLIN)) {
            PerfUtil.applySocketOptions(server, config);
            PerfUtil.configureServerTls(server, config.transport());
            server.bind(config.endpoint());
            PerfControl.emitReady(config.endpoint());
            PerfControl.awaitStart(config.size(), "dealer/dealer server");
            PerfUtil.applyAutoHwmMsgUnit(server, config.size());
            PerfUtil.recalculateAutoHwm(ctx);
            PerfUtil.printMultiSocketAutoHwm(config, server, "server",
                "server", SocketType.DEALER);
            PerfUtil.Metrics metrics = new PerfUtil.Metrics(config);
            // C parity (perf_multi_dealer_dealer_server.cpp run_receive_window):
            // the counted throughput window is closed by a server-side
            // measure-seconds deadline, not by the stop token. The signal-driven
            // (-1) poll only wakes the loop; the stop token wakes the poll too
            // but does not anchor the aggregation end.
            long measureDeadline = System.nanoTime()
                + config.durationSeconds() * 1_000_000_000L;
            while (System.nanoTime() < measureDeadline) {
                pollSet.setEvents(0, PollEventFlag.POLLIN);
                pollSet.poll(-1);
                if (!pollSet.isReady(0, PollEventFlag.POLLIN)) {
                    continue;
                }
                drainCounted(server, config, metrics);
            }
            // Tail drain after the measure deadline: receive any remaining
            // in-flight messages WITHOUT counting them so stale traffic does
            // not spill into the next size case (C drain_phase_until_idle,
            // count_message=false / collect_latency=false).
            drainTailUncounted(server, config, pollSet);
            return metrics.finishMulti(config);
        }
    }

    private static void drainCounted(DealerSocket server, PerfUtil.Config config,
                                      PerfUtil.Metrics metrics) {
        while (true) {
            systems.zlink.contracts.Received received = PerfUtil.recvNoWait(server);
            if (received == null) {
                return;
            }
            try (received) {
                if (PerfStopToken.isStopTokenMessage(received.firstPart())) {
                    // Stop token only wakes the poll; it never ends the
                    // counted window (matches C, which ignores stop_count).
                    continue;
                }
                PerfUtil.Header header = PerfUtil.decodeHeader(
                    received.firstPart(), config.size());
                if (header == null) {
                    continue;
                }
                if (header.phase() == PerfUtil.PHASE_ACTIVE) {
                    metrics.recordNanos(header.latencyNanos());
                }
            }
        }
    }

    private static void drainTailUncounted(DealerSocket server,
                                            PerfUtil.Config config,
                                            PerfSocketPollSet pollSet) {
        // Bounded idle-based tail drain (C drain_phase_until_idle: 2s max,
        // 50ms idle window) so a quiet socket does not block teardown.
        long maxDeadline = System.nanoTime() + 2_000_000_000L;
        long idleDeadline = System.nanoTime() + 50_000_000L;
        while (System.nanoTime() < maxDeadline
            && System.nanoTime() < idleDeadline) {
            boolean progressed = false;
            while (true) {
                systems.zlink.contracts.Received received =
                    PerfUtil.recvNoWait(server);
                if (received == null) {
                    break;
                }
                try (received) {
                    progressed = true;
                }
            }
            if (progressed) {
                idleDeadline = System.nanoTime() + 50_000_000L;
                continue;
            }
            pollSet.setEvents(0, PollEventFlag.POLLIN);
            pollSet.poll(50);
        }
    }

    static PerfUtil.Result runClient(PerfUtil.Config config) {
        List<MonitorSocket> monitors = new ArrayList<>(config.clients());
        List<DealerSocket> clients = new ArrayList<>(config.clients());
        Context ctx = PerfUtil.newContext(config);
        try {
            for (int i = 0; i < config.clients(); i++) {
                DealerSocket client = new DealerSocket(ctx);
                MonitorSocket monitor = client.monitorOpen(MonitorEventType.CONNECTION_READY);
                PerfUtil.applyMonitorOptions(monitor, config);
                PerfUtil.applySocketOptions(client, config);
                PerfUtil.configureClientTls(client, config.transport());
                client.connect(config.endpoint());
                clients.add(client);
                monitors.add(monitor);
            }
            Duration readyTimeout = Duration.ofMillis(config.connectReadyTimeoutMs());
            for (int i = 0; i < monitors.size(); i++) {
                PerfUtil.waitForMonitorEvent(monitors.get(i), READY_EVENT, 1,
                    readyTimeout, "dealer/dealer client ready");
            }
            for (DealerSocket client : clients) {
                PerfUtil.applyAutoHwmMsgUnit(client, config.size());
            }
            PerfUtil.recalculateAutoHwm(ctx);
            for (int i = 0; i < monitors.size(); i++) {
                PerfUtil.printMultiMonitorAutoHwm(config, monitors.get(i),
                    "client", "client[" + i + "]", SocketType.DEALER);
            }
            PerfControl.emitClientReady(config.size());
            PerfControl.awaitStart(config.size(), "dealer/dealer client");
            List<systems.zlink.contracts.Socket> pollSockets = new ArrayList<>(clients.size());
            pollSockets.addAll(clients);
            Message[] payloads = new Message[clients.size()];
            for (int i = 0; i < payloads.length; i++) {
                payloads[i] = PerfUtil.payloadTemplate(config.size());
            }
            try (PerfSocketPollSet pollSet = PerfSocketPollSet.fromSockets(
                     pollSockets, PollEventFlag.POLLOUT)) {
                for (int i = 0; i < clients.size(); i++) {
                    pollSet.setEvents(i);
                }
                boolean[] pending = new boolean[clients.size()];
                long activeEnd = System.nanoTime() + config.durationSeconds() * 1_000_000_000L;
                int nextIndex = 0;
                while (System.nanoTime() < activeEnd) {
                    boolean progress = false;
                    for (int offset = 0; offset < clients.size(); offset++) {
                        int index = (nextIndex + offset) % clients.size();
                        if (pending[index]) {
                            continue;
                        }
                        if (!trySend(clients.get(index), pollSet, pending, index,
                            payloads[index], config.size(), (byte) PerfUtil.PHASE_ACTIVE)) {
                            throw new IllegalStateException("dealer/dealer send failed");
                        }
                        progress = true;
                    }
                    nextIndex = (nextIndex + 1) % clients.size();
                    boolean hasPending = false;
                    for (boolean value : pending) {
                        if (value) {
                            hasPending = true;
                            break;
                        }
                    }
                    if (progress || !hasPending) {
                        continue;
                    }
                    // PERF_MULTI_TEST_POLICY § 1.3.1: signal-driven wait for
                    // POLLOUT readiness; no timer-based fallback.
                    pollWritable(pollSet, pending, -1);
                }
            } finally {
                Message.closeAll(List.of(payloads));
            }
            // C parity: send one wire-level stop token on every client socket
            // so the server's signal-driven receive loop is guaranteed to wake.
            for (DealerSocket client : clients) {
                sendStopTokenBlocking(client);
            }
            PerfControl.emitClientDone(config.size());
            return PerfUtil.Result.silent(config);
        } finally {
            for (MonitorSocket monitor : monitors) {
                try {
                    monitor.close();
                } catch (Exception e) {
                    System.err.println("[multi-dealer-dealer] cleanup failed: " + e);
                }
            }
            for (DealerSocket client : clients) {
                try {
                    client.disconnect(config.endpoint());
                } catch (Exception e) {
                    System.err.println("[multi-dealer-dealer] cleanup failed: " + e);
                }
                try {
                    client.close();
                } catch (Exception e) {
                    System.err.println("[multi-dealer-dealer] cleanup failed: " + e);
                }
            }
            try {
                ctx.close();
            } catch (Exception e) {
                System.err.println("[multi-dealer-dealer] cleanup failed: " + e);
            }
        }
    }

    private static boolean trySend(DealerSocket socket, PerfSocketPollSet pollSet,
                                   boolean[] pending, int index, Message payload,
                                   int size, byte phase) {
        PerfUtil.resetAndWritePayload(payload, size, phase, System.nanoTime());
        try {
            try (Message outbound = Message.copyOf(payload)) {
                if (socket.send().message(outbound).flags(SendFlags.DONT_WAIT).submit()) {
                    pending[index] = false;
                    pollSet.setEvents(index);
                    return true;
                }
            }
        } catch (SubmitException ex) {
            if (!isTransient(ex)) {
                throw ex;
            }
            pending[index] = true;
            pollSet.setEvents(index, PollEventFlag.POLLOUT);
            return true;
        } catch (ZlinkException ex) {
            if (!isTransient(ex)) {
                throw ex;
            }
            pending[index] = true;
            pollSet.setEvents(index, PollEventFlag.POLLOUT);
            return true;
        }
        pending[index] = true;
        pollSet.setEvents(index, PollEventFlag.POLLOUT);
        return true;
    }

    private static void sendStopTokenBlocking(DealerSocket socket) {
        try (PerfSocketPollSet pollSet = PerfSocketPollSet.fromSockets(
                 List.of((systems.zlink.contracts.Socket) socket),
                 PollEventFlag.POLLOUT)) {
            pollSet.setEvents(0);
            while (true) {
                try (Message stop = PerfStopToken.newMessage()) {
                    if (socket.send().message(stop).flags(SendFlags.DONT_WAIT).submit()) {
                        return;
                    }
                } catch (SubmitException ex) {
                    if (!isTransient(ex)) {
                        throw ex;
                    }
                } catch (ZlinkException ex) {
                    if (!isTransient(ex)) {
                        throw ex;
                    }
                }
                pollSet.setEvents(0, PollEventFlag.POLLOUT);
                pollSet.poll(-1);
            }
        }
    }

    private static void pollWritable(PerfSocketPollSet pollSet, boolean[] pending,
                                     int timeoutMs) {
        int readyCount = pollSet.poll(timeoutMs);
        if (readyCount <= 0) {
            return;
        }
        for (int readyOffset = 0; readyOffset < readyCount; readyOffset++) {
            int i = pollSet.readyIndexAt(readyOffset);
            if (!pending[i]) {
                continue;
            }
            if (!pollSet.isReady(i, PollEventFlag.POLLOUT)) {
                continue;
            }
            pending[i] = false;
            pollSet.setEvents(i);
        }
    }

    private static boolean isTransient(ZlinkException ex) {
        if (ex instanceof SubmitException submit) {
            return submit.getResult() == SubmitResult.BACKPRESSURED;
        }
        return ex.getInternalErrno() == 11 || ex.getInternalErrno() == 4;
    }
}

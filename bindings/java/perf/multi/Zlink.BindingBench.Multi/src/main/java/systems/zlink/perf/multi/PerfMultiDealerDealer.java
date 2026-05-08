/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.perf.multi;

import systems.zlink.Context;
import systems.zlink.DealerSocket;
import systems.zlink.Message;
import systems.zlink.MonitorEventType;
import systems.zlink.MonitorSocket;
import systems.zlink.PollEventFlag;
import systems.zlink.SendFlags;
import systems.zlink.SubmitException;
import systems.zlink.SubmitResult;
import systems.zlink.ZlinkException;
import systems.zlink.perf.PerfControl;
import systems.zlink.perf.PerfSocketPollSet;
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
            PerfUtil.Metrics metrics = new PerfUtil.Metrics(config);
            long finishDeadline = System.nanoTime()
                + Duration.ofSeconds(config.durationSeconds() + 20L).toNanos();
            boolean idleDrain = false;
            int idleDrainTimeoutMs = Math.max(1, config.recvTimeoutMs());
            while (true) {
                if (!idleDrain && System.nanoTime() >= finishDeadline) {
                    throw new IllegalStateException("dealer/dealer server cooldown timed out");
                }
                long remainingNs = Math.max(1L, finishDeadline - System.nanoTime());
                pollSet.setEvents(0, PollEventFlag.POLLIN);
                int timeoutMs = idleDrain
                    ? idleDrainTimeoutMs
                    : Math.max(1, (int) Math.min(Integer.MAX_VALUE,
                        Duration.ofNanos(remainingNs).toMillis()));
                if (pollSet.poll(timeoutMs) <= 0
                    || !pollSet.isReady(0, PollEventFlag.POLLIN)) {
                    if (idleDrain) {
                        return metrics.finishMulti(config);
                    }
                    continue;
                }
                while (true) {
                    systems.zlink.Received received = PerfUtil.recvNoWait(server);
                    if (received == null) {
                        break;
                    }
                    try (received) {
                        PerfUtil.Header header = PerfUtil.decodeHeader(
                            received.firstPart(), config.size());
                        if (header == null) {
                            continue;
                        }
                        if (header.phase() == PerfUtil.PHASE_COOLDOWN) {
                            idleDrain = true;
                            continue;
                        }
                        if (header.phase() == PerfUtil.PHASE_ACTIVE) {
                            metrics.recordNanos(header.latencyNanos());
                        }
                    }
                }
            }
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
            PerfControl.emitClientReady(config.size());
            PerfControl.awaitStart(config.size(), "dealer/dealer client");
            List<systems.zlink.Socket> pollSockets = new ArrayList<>(clients.size());
            pollSockets.addAll(clients);
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
                        if (!trySend(clients.get(index), pollSet, pending, index, config.size(),
                            (byte) PerfUtil.PHASE_ACTIVE)) {
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
                    long remainingNs = Math.max(1L, activeEnd - System.nanoTime());
                    pollWritable(pollSet, pending, Math.max(1, (int) Math.min(Integer.MAX_VALUE,
                        Duration.ofNanos(remainingNs).toMillis())));
                }
            }
            DealerSocket cooldownClient = clients.get(0);
            try (PerfSocketPollSet pollSet = PerfSocketPollSet.fromSockets(
                     List.of((systems.zlink.Socket) cooldownClient),
                     PollEventFlag.POLLOUT)) {
                pollSet.setEvents(0);
                long cooldownDeadline = System.nanoTime() + Duration.ofSeconds(5).toNanos();
                while (System.nanoTime() < cooldownDeadline) {
                    try (Message cooldown = PerfUtil.payload(config.size(),
                             (byte) PerfUtil.PHASE_COOLDOWN, System.nanoTime())) {
                        if (cooldownClient.send(cooldown, SendFlags.DONT_WAIT)) {
                            return PerfUtil.Result.silent(config);
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
                    long remainingNs = Math.max(1L, cooldownDeadline - System.nanoTime());
                    pollSet.setEvents(0, PollEventFlag.POLLOUT);
                    pollSet.poll(Math.max(1, (int) Math.min(Integer.MAX_VALUE,
                        Duration.ofNanos(remainingNs).toMillis())));
                }
            }
            throw new IllegalStateException("dealer/dealer cooldown send timed out");
        } finally {
            for (MonitorSocket monitor : monitors) {
                try {
                    monitor.close();
                } catch (Exception ignored) {
                }
            }
            for (DealerSocket client : clients) {
                try {
                    client.disconnect(config.endpoint());
                } catch (Exception ignored) {
                }
                try {
                    client.close();
                } catch (Exception ignored) {
                }
            }
            try {
                ctx.close();
            } catch (Exception ignored) {
            }
        }
    }

    private static boolean trySend(DealerSocket socket, PerfSocketPollSet pollSet,
                                   boolean[] pending, int index, int size, byte phase) {
        try (Message payload = PerfUtil.payload(size, phase, System.nanoTime())) {
            if (socket.send(payload, SendFlags.DONT_WAIT)) {
                pending[index] = false;
                pollSet.setEvents(index);
                return true;
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

    private static void pollWritable(PerfSocketPollSet pollSet, boolean[] pending,
                                     int timeoutMs) {
        if (pollSet.poll(timeoutMs) <= 0) {
            return;
        }
        for (int i = 0; i < pending.length; i++) {
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

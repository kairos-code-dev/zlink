/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.perf.multi;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;

import systems.zlink.contracts.Context;
import systems.zlink.contracts.Message;
import systems.zlink.contracts.MonitorEventType;
import systems.zlink.contracts.MonitorSocket;
import systems.zlink.contracts.PollEventFlag;
import systems.zlink.contracts.PubSocket;
import systems.zlink.contracts.RecvFlags;
import systems.zlink.contracts.SendFlags;
import systems.zlink.contracts.Socket;
import systems.zlink.contracts.SocketType;
import systems.zlink.contracts.SubSocket;
import systems.zlink.contracts.SubmitException;
import systems.zlink.contracts.SubmitResult;
import systems.zlink.contracts.TopicMessage;
import systems.zlink.perf.PerfControl;
import systems.zlink.perf.PerfSocketPollSet;
import systems.zlink.perf.PerfStopToken;
import systems.zlink.perf.PerfUtil;
import java.time.Duration;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

final class PerfMultiPubSub {
    private static final String TOPIC = "perf.topic";
    private static final MonitorEventType READY_EVENT =
        MonitorEventType.CONNECTION_READY;

    private PerfMultiPubSub() {
    }

    static PerfUtil.Result runServer(PerfUtil.Config config) {
        try (Context ctx = PerfUtil.newContext(config);
             PubSocket pub = new PubSocket(ctx)) {
            PerfUtil.applySocketOptions(pub, config);
            PerfUtil.applyAutoHwmMsgUnit(pub, config.size());
            PerfUtil.recalculateAutoHwm(ctx);
            PerfUtil.printMultiSocketAutoHwm(config, pub, "server",
                "server", SocketType.PUB);
            PerfUtil.configureServerTls(pub, config.transport());
            pub.bind(config.endpoint());
            PerfControl.emitReady(config.endpoint());
            PerfControl.awaitStart(config.size(), "pubsub server");
            long activeEnd = System.nanoTime() + config.durationSeconds() * 1_000_000_000L;
            try (Message active = PerfUtil.payloadTemplate(config.size());
                 PerfSocketPollSet writable = PerfSocketPollSet.fromSockets(
                     Collections.singletonList((Socket) pub),
                     PollEventFlag.POLLOUT)) {
                while (System.nanoTime() < activeEnd) {
                    PerfUtil.resetAndWritePayload(active, config.size(),
                        (byte) PerfUtil.PHASE_ACTIVE, System.nanoTime());
                    if (!publishWhenWritable(pub, writable, active, activeEnd)) {
                        break;
                    }
                }
            }
            // C parity (perf_multi_pubsub_server.cpp publish_stop_token): after
            // the active window publish a wire-level stop token on the topic so
            // the subscriber wakes from its signal-driven (-1) poll and exits.
            publishStopToken(pub);
            return PerfUtil.Result.silent(config);
        }
    }

    static PerfUtil.Result runClient(PerfUtil.Config config) {
        PerfUtil.Metrics metrics = new PerfUtil.Metrics(config);
        List<SubSocket> subscribers = new ArrayList<>(config.clients());
        List<MonitorSocket> monitors = new ArrayList<>(config.clients());
        Context ctx = PerfUtil.newContext(config);
        try {
            for (int i = 0; i < config.clients(); i++) {
                SubSocket sub = new SubSocket(ctx);
                MonitorSocket monitor = sub.monitorOpen(READY_EVENT);
                PerfUtil.applyMonitorOptions(monitor, config);
                PerfUtil.applySocketOptions(sub, config);
                PerfUtil.applyAutoHwmMsgUnit(sub, config.size());
                PerfUtil.configureClientTls(sub, config.transport());
                sub.setSubscription(TOPIC);
                sub.connect(config.endpoint());
                subscribers.add(sub);
                monitors.add(monitor);
            }
            PerfUtil.recalculateAutoHwm(ctx);
            for (int i = 0; i < monitors.size(); i++) {
                PerfUtil.printMultiMonitorAutoHwm(config, monitors.get(i),
                    "client", "client[" + i + "]", SocketType.SUB);
            }
            Duration readyTimeout = Duration.ofMillis(config.connectReadyTimeoutMs());
            for (int i = 0; i < monitors.size(); i++) {
                PerfUtil.waitForMonitorEvent(monitors.get(i), READY_EVENT, 1,
                    readyTimeout, "pubsub client ready[" + i + "]");
                monitors.get(i).close();
            }
            monitors.clear();
            PerfControl.emitClientReady(config.size());
            PerfControl.awaitStart(config.size(), "pubsub client");

            List<Socket> pollSockets = new ArrayList<>(subscribers.size());
            pollSockets.addAll(subscribers);
            try (PerfSocketPollSet pollSet = PerfSocketPollSet.fromSockets(
                pollSockets, PollEventFlag.POLLIN)) {
                long activeEnd = System.nanoTime()
                    + config.durationSeconds() * 1_000_000_000L;
                // C parity (perf_multi_pubsub_client.cpp run_recv_duration):
                // signal-driven (-1) poll; the loop ends only on the wire-level
                // stop token (or cooldown). The active deadline gates which
                // messages are counted, not the poll timeout.
                boolean phaseDone = false;
                while (!phaseDone) {
                    int readyCount = pollSet.poll(-1);
                    for (int readyOffset = 0; readyOffset < readyCount; readyOffset++) {
                        int index = pollSet.readyIndexAt(readyOffset);
                        if (!pollSet.isReady(index, PollEventFlag.POLLIN)) {
                            continue;
                        }
                        if (drainSubscriber(subscribers.get(index), config,
                            metrics, activeEnd)) {
                            phaseDone = true;
                        }
                    }
                }
            }
            return metrics.finishMulti(config);
        } finally {
            for (MonitorSocket monitor : monitors) {
                try {
                    monitor.close();
                } catch (RuntimeException ignored) {
                }
            }
            for (SubSocket sub : subscribers) {
                try {
                    sub.disconnect(config.endpoint());
                } catch (RuntimeException ignored) {
                }
                try {
                    sub.close();
                } catch (RuntimeException ignored) {
                }
            }
            // This benchmark process exits after one size case. Closing the
            // native context here can wait behind one-way PUB/SUB teardown and
            // hide the already-finished RESULT from the runner.
        }
    }

    private static boolean publishWhenWritable(PubSocket pub,
                                               PerfSocketPollSet writable,
                                               Message message,
                                               long deadlineNs) {
        while (System.nanoTime() < deadlineNs) {
            try (Message outbound = Message.copyOf(message)) {
                if (pub.publish(TOPIC)
                        .message(outbound)
                        .flags(SendFlags.DONT_WAIT)
                        .submit()) {
                    return true;
                }
            } catch (SubmitException ex) {
                if (!isTransientSubmit(ex)) {
                    throw ex;
                }
            }
            long remainingNs = deadlineNs - System.nanoTime();
            if (remainingNs <= 0) {
                return false;
            }
            int waitMs = (int) Math.max(1L,
                Math.min(Integer.MAX_VALUE, remainingNs / 1_000_000L));
            writable.poll(waitMs);
        }
        return false;
    }

    private static boolean isTransientSubmit(SubmitException ex) {
        return ex.getResult() == SubmitResult.BACKPRESSURED
            || ex.getResult() == SubmitResult.NOT_CONNECTED;
    }

    // Returns true when the wire-level stop token (or cooldown phase) is seen,
    // mirroring C perf_multi_pubsub_client.cpp recv_one_pubsub_message: the
    // stop token is checked before header decode and ends the phase; counting
    // remains bounded by the active deadline.
    private static boolean drainSubscriber(SubSocket sub, PerfUtil.Config config,
                                           PerfUtil.Metrics metrics,
                                           long activeEnd) {
        while (true) {
            try (TopicMessage received = new TopicMessage()) {
                if (!sub.subscribe(received, RecvFlags.DONT_WAIT)) {
                    return false;
                }
                if (PerfStopToken.isStopTokenMessage(received.firstPart())) {
                    return true;
                }
                PerfUtil.Header header = PerfUtil.decodeHeader(
                    received.firstPart(), config.size());
                if (header == null) {
                    continue;
                }
                if (header.phase() == PerfUtil.PHASE_COOLDOWN) {
                    return true;
                }
                if (header.phase() == PerfUtil.PHASE_ACTIVE
                    && System.nanoTime() < activeEnd) {
                    metrics.recordNanos(header.latencyNanos());
                }
            }
        }
    }

    // C parity (perf_multi_pubsub_server.cpp publish_stop_token): publish the
    // stop token on the topic with blocking semantics, retrying through
    // transient backpressure so the subscriber always observes the terminator.
    private static void publishStopToken(PubSocket pub) {
        while (true) {
            try (Message stop = PerfStopToken.newMessage()) {
                if (pub.publish(TOPIC)
                        .message(stop)
                        .flags(SendFlags.NONE)
                        .submit()) {
                    return;
                }
            } catch (SubmitException ex) {
                if (!isTransientSubmit(ex)) {
                    throw ex;
                }
            }
        }
    }
}

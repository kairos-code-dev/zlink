/* SPDX-License-Identifier: MPL-2.0 */

package io.ulalax.zlink;

import io.ulalax.zlink.internal.Native;
import java.util.ArrayList;
import java.util.List;

public final class Poller {
    private final List<PollItem> items = new ArrayList<>();

    public void add(Socket socket, int events) {
        items.add(new PollItem(socket, 0, events));
    }

    public void add(Socket socket, PollEventType... events) {
        items.add(new PollItem(socket, 0, PollEventType.combine(events)));
    }

    public void addFd(int fd, int events) {
        items.add(new PollItem(null, fd, events));
    }

    public void addFd(int fd, PollEventType... events) {
        items.add(new PollItem(null, fd, PollEventType.combine(events)));
    }

    public List<PollEvent> poll(int timeoutMs) {
        return Native.poll(items, timeoutMs);
    }

    public static final class PollItem {
        public final Socket socket;
        public final int fd;
        public final int events;

        PollItem(Socket socket, int fd, int events) {
            this.socket = socket;
            this.fd = fd;
            this.events = events;
        }
    }

    public record PollEvent(Socket socket, int revents) {}
}

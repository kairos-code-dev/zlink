/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.eventing;

import systems.zlink.contracts.service.spot.Spot;
import systems.zlink.contracts.sockets.Socket;
import java.time.Duration;

public interface Poller extends AutoCloseable {

    public abstract void add(Socket socket, long slot, PollEventFlag... events);

    public abstract void add(Spot spot, long slot, PollEventFlag... events);

    public abstract void addFd(int fd, long slot, PollEventFlag... events);

    public abstract void add(Timer timer, long slot);

    public abstract void modify(Socket socket, PollEventFlag... events);

    public abstract void modify(Spot spot, PollEventFlag... events);

    public abstract void modifyFd(int fd, PollEventFlag... events);

    public abstract boolean remove(Socket socket);

    public abstract boolean remove(Spot spot);

    public abstract boolean removeFd(int fd);

    public abstract boolean remove(Timer timer);

    public abstract void clear();

    public abstract int size();

    public abstract int wait(PollEvents events, Duration timeout);

    @Override
    public abstract void close();
}

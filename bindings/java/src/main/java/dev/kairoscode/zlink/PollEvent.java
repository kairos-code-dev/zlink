/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

public record PollEvent(Socket socket, int revents, int fd, Object tag,
                        int events) {
    public PollEvent(Socket socket, int revents) {
        this(socket, revents, 0, null, 0);
    }
}

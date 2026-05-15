/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts;


import java.util.EnumSet;

public record PollEvent(Socket socket, Integer fd, Timer timer, Object tag,
                        EnumSet<PollEventFlag> events,
                        EnumSet<PollEventFlag> revents) {
}

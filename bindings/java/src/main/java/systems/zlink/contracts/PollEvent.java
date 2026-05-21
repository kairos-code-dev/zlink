/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts;

public record PollEvent(PollSourceKind sourceKind, long slot, int revents,
                        int fd) {
}

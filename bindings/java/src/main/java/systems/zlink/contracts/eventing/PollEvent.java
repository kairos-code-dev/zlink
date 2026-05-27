/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.eventing;

public record PollEvent(PollSourceKind sourceKind, long slot, int revents,
                        int fd) {
}

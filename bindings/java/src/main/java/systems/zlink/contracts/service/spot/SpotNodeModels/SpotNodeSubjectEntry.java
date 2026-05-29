/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.service.registry.SubjectKind;

public record SpotNodeSubjectEntry(SpotRole role, String subject,
                                   SubjectKind subjectKind,
                                   int readyPeerCount, int activePeerCount,
                                   long lastChangedMs) {
}

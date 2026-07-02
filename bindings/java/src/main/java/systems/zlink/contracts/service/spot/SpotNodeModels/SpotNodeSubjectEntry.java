/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

/**
 * One subject (topic or pattern) served by a spot node.
 * @param role the subject's pub/sub role
 * @param subject the subject topic or pattern string
 * @param subjectKind how the subject is matched
 * @param readyPeerCount the number of peers ready on this subject
 * @param activePeerCount the number of peers active on this subject
 * @param lastChangedMs when the subject last changed, in milliseconds
 */
public record SpotNodeSubjectEntry(SpotRole role, String subject,
                                   SubjectKind subjectKind,
                                   int readyPeerCount, int activePeerCount,
                                   long lastChangedMs) {
}

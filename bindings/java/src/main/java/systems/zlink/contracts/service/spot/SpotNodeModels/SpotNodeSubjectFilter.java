/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

/**
 * Filters a spot node subject query; null fields match anything.
 * @param role restrict to subjects with this pub/sub role, or null for any
 * @param subject restrict to this subject topic or pattern, or null for any
 * @param subjectKind restrict to this subject match kind, or null for any
 */
public record SpotNodeSubjectFilter(SpotRole role, String subject,
                                    SubjectKind subjectKind) {
}

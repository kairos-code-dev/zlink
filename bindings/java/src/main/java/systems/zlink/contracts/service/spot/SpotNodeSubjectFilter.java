/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.service.registry.ServiceEventSubjectKind;

public record SpotNodeSubjectFilter(SpotRole role, String subject,
                                    ServiceEventSubjectKind subjectKind) {
}

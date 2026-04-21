/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import java.lang.foreign.MemorySegment;

public record SpotDispatchInfo(SpotDispatchEvent event,
                               SpotDispatchSubjectKind subjectKind,
                               MemorySegment subject) {
}

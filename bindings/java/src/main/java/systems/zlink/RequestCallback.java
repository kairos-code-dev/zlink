/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink;

import java.util.List;

@FunctionalInterface
public interface RequestCallback {
    void onComplete(RequestResult result, List<Message> parts);
}

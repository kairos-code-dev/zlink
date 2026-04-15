/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.spot;

public record SpotServiceAttachmentStats(String serviceName, int routerCount,
                                         int pubCount, int subCount,
                                         int autoRouterCount,
                                         int autoPubCount, int autoSubCount) {
}

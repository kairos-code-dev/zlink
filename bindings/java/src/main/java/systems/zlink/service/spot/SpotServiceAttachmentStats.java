/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.service.spot;

record SpotServiceAttachmentStats(String channelName, int routerCount,
                                  int pubCount, int subCount,
                                  int autoRouterCount,
                                  int autoPubCount, int autoSubCount) {
}

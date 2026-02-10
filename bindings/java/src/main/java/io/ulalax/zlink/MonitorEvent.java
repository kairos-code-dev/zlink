/* SPDX-License-Identifier: MPL-2.0 */

package io.ulalax.zlink;

public record MonitorEvent(long event, long value, byte[] routingId,
                           String localAddress, String remoteAddress) {}

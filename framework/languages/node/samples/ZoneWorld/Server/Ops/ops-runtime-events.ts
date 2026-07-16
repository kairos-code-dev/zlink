import { Injectable } from '@nestjs/common';
import {
  ZLinkLocationRuntimeEventKind,
  ZLinkSocketEventKind
} from '@zlink-systems/framework';
import { zlinkRuntimeEventHandler } from '@zlink-systems/nestjs';
import type {
  ZLinkLocationRuntimeEvent,
  ZLinkRuntimeEventHandler,
  ZLinkSocketEvent
} from '@zlink-systems/framework';
import { NodeRegistry } from './node-registry';
import { OpsConsoleRegistry } from './ops-console-registry';

const OPS_LOCATION_SOURCE = 'zoneworld.location';
const OPS_REPORT_SOCKET_SOURCE = 'zoneworld.report.server';

@Injectable()
@zlinkRuntimeEventHandler()
class OpsLocationEventHandler implements ZLinkRuntimeEventHandler<ZLinkLocationRuntimeEvent> {
  constructor(private readonly nodes: NodeRegistry, private readonly consoles: OpsConsoleRegistry) {}

  async handle(event: ZLinkLocationRuntimeEvent): Promise<void> {
    if (event.sourceName !== OPS_LOCATION_SOURCE || event.event !== ZLinkLocationRuntimeEventKind.TopologyChanged) return;
    const live = new Set(event.topology.flatMap((entry) => entry.nodeRid === undefined ? [] : [String(entry.nodeRid)]));
    console.log(`location topology observed live=${[...live].sort().join(',')}`);
    for (const node of this.nodes.applyLiveRoutingIds(live)) this.consoles.publish(node);
  }
}

@Injectable()
@zlinkRuntimeEventHandler()
class OpsSocketEventHandler implements ZLinkRuntimeEventHandler<ZLinkSocketEvent> {
  private readonly routingByRemote = new Map<string, string>();

  constructor(private readonly nodes: NodeRegistry, private readonly consoles: OpsConsoleRegistry) {}

  async handle(event: ZLinkSocketEvent): Promise<void> {
    if (event.sourceName !== OPS_REPORT_SOCKET_SOURCE) return;
    const connected = event.event === ZLinkSocketEventKind.Connected
      || event.event === ZLinkSocketEventKind.ConnectionReady;
    const disconnected = event.event === ZLinkSocketEventKind.Disconnected
      || event.event === ZLinkSocketEventKind.Closed;
    if (!connected && !disconnected) return;
    if (connected && event.routingId !== undefined) {
      this.routingByRemote.set(event.remoteAddr, nodeRoutingId(String(event.routingId)));
    }
    const routingId = event.routingId === undefined
      ? this.routingByRemote.get(event.remoteAddr)
      : nodeRoutingId(String(event.routingId));
    console.log(`report socket observed event=${event.event} rid=${routingId ?? '-'} remote=${event.remoteAddr}`);
    if (routingId === undefined) return;
    const node = this.nodes.applyConnection(routingId, connected);
    if (node !== undefined) this.consoles.publish(node);
    if (disconnected) this.routingByRemote.delete(event.remoteAddr);
  }
}

function nodeRoutingId(value: string): string {
  if (!/^(?:[0-9a-f]{2})+$/i.test(value)) return value.split('\0', 1)[0];
  return Buffer.from(value, 'hex').toString('utf8').split('\0', 1)[0];
}

export {
  OPS_LOCATION_SOURCE,
  OPS_REPORT_SOCKET_SOURCE,
  OpsLocationEventHandler,
  OpsSocketEventHandler
};

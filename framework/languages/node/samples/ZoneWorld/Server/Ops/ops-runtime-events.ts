import { Injectable } from '@nestjs/common';
import {
  ZLinkLocationRuntimeEventKind,
  ZLinkSpotEventKind
} from '@zlink-systems/framework';
import { zlinkRuntimeEventHandler } from '@zlink-systems/nestjs';
import type {
  ZLinkLocationRuntimeEvent,
  ZLinkRuntimeEventHandler,
  ZLinkSpotEvent
} from '@zlink-systems/framework';
import { ZoneWorldNames } from '../../Shared/spec';
import { NodeRegistry } from './node-registry';
import { OpsConsoleRegistry } from './ops-console-registry';

const OPS_LOCATION_SOURCE = 'zoneworld.location';

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
class OpsReportMeshEventHandler implements ZLinkRuntimeEventHandler<ZLinkSpotEvent> {
  private readyRoutingIds = new Set<string>();

  constructor(private readonly nodes: NodeRegistry, private readonly consoles: OpsConsoleRegistry) {}

  async handle(event: ZLinkSpotEvent): Promise<void> {
    if (event.sourceName !== ZoneWorldNames.zoneMesh
      || event.event !== ZLinkSpotEventKind.PeersChanged) return;
    const current = new Set(event.peers
      .filter((peer) => peer.ready)
      .map((peer) => nodeRoutingId(String(peer.rid))));
    for (const routingId of new Set([...this.readyRoutingIds, ...current])) {
      const connected = current.has(routingId);
      if (connected === this.readyRoutingIds.has(routingId)) continue;
      console.log(`report mesh observed ready=${connected} rid=${routingId}`);
      const node = this.nodes.applyConnection(routingId, connected);
      if (node !== undefined) this.consoles.publish(node);
    }
    this.readyRoutingIds = current;
  }
}

function nodeRoutingId(value: string): string {
  if (!/^(?:[0-9a-f]{2})+$/i.test(value)) return value.split('\0', 1)[0];
  return Buffer.from(value, 'hex').toString('utf8').split('\0', 1)[0];
}

export {
  OPS_LOCATION_SOURCE,
  OpsLocationEventHandler,
  OpsReportMeshEventHandler
};

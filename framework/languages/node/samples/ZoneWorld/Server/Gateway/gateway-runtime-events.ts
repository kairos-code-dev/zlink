import { Injectable } from '@nestjs/common';
import { ZLinkSpotEventKind } from '@zlink-systems/framework';
import { zlinkRuntimeEventHandler } from '@zlink-systems/nestjs';
import type { ZLinkRuntimeEventHandler, ZLinkSpotEvent } from '@zlink-systems/framework';

@Injectable()
@zlinkRuntimeEventHandler()
class GatewaySpotEventHandler implements ZLinkRuntimeEventHandler<ZLinkSpotEvent> {
  async handle(event: ZLinkSpotEvent): Promise<void> {
    if (event.event !== ZLinkSpotEventKind.PeersChanged) return;
    for (const peer of event.peers.filter((candidate) => candidate.ready)) {
      console.log(`gateway mesh peer ready mesh=${event.sourceName} remote=${peer.endpoint}`);
    }
  }
}

export { GatewaySpotEventHandler };

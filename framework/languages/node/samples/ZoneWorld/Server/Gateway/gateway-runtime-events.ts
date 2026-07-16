import { Injectable } from '@nestjs/common';
import { ZLinkSpotEventKind, ZLinkSpotPeerState } from '@zlink-systems/framework';
import { zlinkRuntimeEventHandler } from '@zlink-systems/nestjs';
import type { ZLinkRuntimeEventHandler, ZLinkSpotEvent } from '@zlink-systems/framework';
import { ZoneWorldNames } from '../../Shared/spec';

@Injectable()
@zlinkRuntimeEventHandler()
class GatewaySpotEventHandler implements ZLinkRuntimeEventHandler<ZLinkSpotEvent> {
  async handle(event: ZLinkSpotEvent): Promise<void> {
    if (event.sourceName !== ZoneWorldNames.zoneMesh || event.event !== ZLinkSpotEventKind.PeersChanged) return;
    for (const peer of event.peers.filter((candidate) => candidate.state === ZLinkSpotPeerState.Connected)) {
      console.log(`gateway spot peer ready remote=${peer.peerEndpoint}`);
    }
  }
}

export { GatewaySpotEventHandler };

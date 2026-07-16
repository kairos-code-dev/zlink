import { Injectable } from '@nestjs/common';
import {
  ZLinkSpotEventKind,
  ZLinkSpotPeerState,
  type ZLinkRuntimeEventHandler,
  type ZLinkSpotEvent
} from '@zlink-systems/framework';
import { zlinkRuntimeEventHandler } from '@zlink-systems/nestjs';
import { SampleNames } from './sample-names';

@Injectable()
@zlinkRuntimeEventHandler()
class RoomRouterReadinessHandler implements ZLinkRuntimeEventHandler<ZLinkSpotEvent> {
  async handle(event: ZLinkSpotEvent): Promise<void> {
    if (event.sourceName !== SampleNames.roomSpotNode) return;
    if (event.event !== ZLinkSpotEventKind.PeersChanged) return;
    for (const peer of event.peers.filter((candidate) => candidate.state === ZLinkSpotPeerState.Connected)) {
      console.log(`bingo-room-peer ConnectionReady remote=${peer.peerEndpoint}`);
    }
  }
}

export { RoomRouterReadinessHandler };

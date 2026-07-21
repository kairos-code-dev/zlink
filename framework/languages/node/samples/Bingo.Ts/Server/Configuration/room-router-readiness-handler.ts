import { Injectable } from '@nestjs/common';
import {
  ZLinkLocationPeerEventKind,
  ZLinkSpotEventKind,
  type ZLinkLocationPeerEvent,
  type ZLinkRuntimeEventHandler,
  type ZLinkSpotEvent
} from '@zlink-systems/framework';
import { zlinkRuntimeEventHandler } from '@zlink-systems/nestjs';
import { SampleNames } from './sample-names';

@Injectable()
@zlinkRuntimeEventHandler()
class RoomRouterReadinessHandler implements ZLinkRuntimeEventHandler<ZLinkSpotEvent | ZLinkLocationPeerEvent> {
  async handle(event: ZLinkSpotEvent | ZLinkLocationPeerEvent): Promise<void> {
    if (event.sourceName === SampleNames.roomLocationPeerMonitor
      && event.event === ZLinkLocationPeerEventKind.DesiredSetChanged) {
      const change = event.desiredSetChange;
      if (change.meshName === SampleNames.roomSpotNode) {
        const connected = change.connectedEndpoints.join(',');
        const disconnected = change.disconnectedEndpoints.join(',');
        console.log(
          `bingo-room-desired connected=${connected.length === 0 ? '-' : connected} `
          + `disconnected=${disconnected.length === 0 ? '-' : disconnected}`
        );
      }
      return;
    }
    if (event.event !== ZLinkSpotEventKind.PeersChanged) return;
    if (event.sourceName !== SampleNames.roomSpotNode) return;
    for (const peer of event.peers) {
      console.log(
        `bingo-room-peer-state remote=${peer.endpoint} rid=${peer.rid} `
        + `generation=${peer.lifecycleGeneration} admission=${peer.admissionState} ready=${peer.ready}`
      );
      if (peer.ready) {
        console.log(`bingo-room-peer ConnectionReady remote=${peer.endpoint}`);
      }
    }
  }
}

export { RoomRouterReadinessHandler };

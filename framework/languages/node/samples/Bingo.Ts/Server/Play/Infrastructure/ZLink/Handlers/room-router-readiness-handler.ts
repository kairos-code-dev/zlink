import { Injectable } from '@nestjs/common';
import {
  ZLinkSocketEventKind,
  type ZLinkRuntimeEventHandler,
  type ZLinkSocketEvent
} from '@zlink-systems/framework';
import { zlinkRuntimeEventHandler } from '@zlink-systems/nestjs';
import { SampleNames } from '../../../../Configuration/sample-names';

@Injectable()
@zlinkRuntimeEventHandler()
class PlayRouterReadinessHandler implements ZLinkRuntimeEventHandler<ZLinkSocketEvent> {
  private readonly readyRemotes = new Set<string>();

  async handle(event: ZLinkSocketEvent): Promise<void> {
    if (event.sourceName !== `${SampleNames.playChannel}.server`
      || event.event !== ZLinkSocketEventKind.ConnectionReady) return;
    this.readyRemotes.add(event.remoteAddr);
    console.log(`bingo-play-router ConnectionReady clients=${this.readyRemotes.size}`);
  }
}

export { PlayRouterReadinessHandler };

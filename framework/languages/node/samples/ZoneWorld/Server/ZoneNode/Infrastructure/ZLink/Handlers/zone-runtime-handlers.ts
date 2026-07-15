import { Injectable } from '@nestjs/common';
import type {
  ZLinkPublishContext,
  ZLinkSpotSubscriptionHandler,
  ZLinkSpotTimerHandler,
  ZLinkTimerTick
} from '@zlink-systems/framework';
import type { ZoneBorderEvent } from '../../../../../Shared/contracts';
import type { ZoneSpot } from '../Spots/zone-spot';

@Injectable()
class ZoneTickHandler implements ZLinkSpotTimerHandler<ZoneSpot> {
  async handle(spot: ZoneSpot, _tick: ZLinkTimerTick): Promise<void> {
    await spot.tick();
  }
}

@Injectable()
class FirstBorderSubscriptionHandler implements ZLinkSpotSubscriptionHandler<ZoneSpot, ZoneBorderEvent> {
  async handle(spot: ZoneSpot, event: ZoneBorderEvent, _context: ZLinkPublishContext): Promise<void> {
    spot.applyBorder(event);
  }
}

@Injectable()
class SecondBorderSubscriptionHandler implements ZLinkSpotSubscriptionHandler<ZoneSpot, ZoneBorderEvent> {
  async handle(spot: ZoneSpot, event: ZoneBorderEvent, _context: ZLinkPublishContext): Promise<void> {
    spot.applyBorder(event);
  }
}

export { FirstBorderSubscriptionHandler, SecondBorderSubscriptionHandler, ZoneTickHandler };

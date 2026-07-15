import { Injectable } from '@nestjs/common';
import { ZLinkPacket } from '@zlink-systems/framework';
import type {
  ZLinkPublishContext,
  ZLinkSpotSubscriptionHandler,
  ZLinkSpotPacketHandler,
  ZLinkHandlerContext,
  ZLinkSpotTimerHandler,
  ZLinkTimerTick
} from '@zlink-systems/framework';
import { PacketNames, WorldAnnounceNotify } from '../../../../../Shared/contracts';
import type { DeliverAnnounceMsg, ZoneBorderEvent } from '../../../../../Shared/contracts';
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

@Injectable()
@ZLinkPacket(PacketNames.deliverAnnounceMsg)
class DeliverAnnounceHandler implements ZLinkSpotPacketHandler<ZoneSpot, DeliverAnnounceMsg> {
  async handle(spot: ZoneSpot, message: DeliverAnnounceMsg, _context: ZLinkHandlerContext): Promise<void> {
    await spot.pushHumans(new WorldAnnounceNotify(message.announcementId, message.text));
    console.log(`zone spot announcement delivered zone=${String(spot.context.spotRid)} id=${message.announcementId}`);
  }
}

export {
  DeliverAnnounceHandler,
  FirstBorderSubscriptionHandler,
  SecondBorderSubscriptionHandler,
  ZoneTickHandler
};

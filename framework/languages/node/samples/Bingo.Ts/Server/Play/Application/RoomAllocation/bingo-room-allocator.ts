import { Inject } from '@nestjs/common';
import { Message } from '@zlink-systems/zlink';
import { ZLINK_SPOT_MANAGER } from '@zlink-systems/nestjs';
import { BingoNotificationPublisher } from '../../Adapters/ZLink/Notifications/bingo-notification-publisher';
import { createRoomSettings } from '../../Domain/Bingo/bingo-room-models';
import { BingoRoomSpot } from '../../Adapters/ZLink/Spots/bingo-room-spot';
import { bingoRoomSettingsPayload } from '../../../../Shared/Contracts/messages';
import type {
  ZLinkSpotManager
} from '@zlink-systems/framework';
import { ZLinkSpotCreateState } from '@zlink-systems/framework';
import type { BingoNotificationPublisher as BingoNotificationPublisherType } from '../../Adapters/ZLink/Notifications/bingo-notification-publisher';
import type { BingoRoomSpot as BingoRoomSpotType } from '../../Adapters/ZLink/Spots/bingo-room-spot';
import type { BingoRoomSettings } from '../../Domain/Bingo/bingo-room-models';
import type {
  BingoMode
} from '../../../../Shared/Contracts/messages';

class BingoRoomAllocator {
  private currentRoomId: string | null;
  private currentRoomSettings: BingoRoomSettings | null;
  private reservedSeats: number;
  private roomSeq: number;

  constructor(
    private readonly notifications: BingoNotificationPublisherType,
    private readonly spotManager: ZLinkSpotManager
  ) {
    this.notifications = notifications;
    BingoRoomSpot.useNotifications(notifications);
    this.currentRoomId = null;
    this.currentRoomSettings = null;
    this.reservedSeats = 0;
    this.roomSeq = 0;
  }

  async allocate(mode: BingoMode | undefined): Promise<string> {
    let settings = createRoomSettings(mode, this.roomSeq + 1);
    if (
      this.currentRoomId === null ||
      this.currentRoomSettings === null ||
      this.currentRoomSettings.mode !== settings.mode ||
      this.reservedSeats >= this.currentRoomSettings.requiredPlayers
    ) {
      this.roomSeq += 1;
      settings = createRoomSettings(mode, this.roomSeq);
      const request = Message.from(bingoRoomSettingsPayload(settings));
      const created = await this.spotManager.create(BingoRoomSpot, request);
      request.close();
      if (created.state !== ZLinkSpotCreateState.Created) {
        throw new Error('Bingo room creation was rejected.');
      }
      this.currentRoomId = created.spotRid;
      this.currentRoomSettings = settings;
      this.reservedSeats = 0;
    }

    this.reservedSeats += 1;
    return this.currentRoomId;
  }

  async executeInRoom<TResult>(
    roomId: string,
    operation: (room: BingoRoomSpotType) => TResult | Promise<TResult>
  ): Promise<TResult> {
    return await this.spotManager.executeOnSpot<BingoRoomSpotType, TResult>(BingoRoomSpot, roomId, operation);
  }
}

Inject(BingoNotificationPublisher)(BingoRoomAllocator, undefined, 0);
Inject(ZLINK_SPOT_MANAGER)(BingoRoomAllocator, undefined, 1);

export { BingoRoomAllocator };

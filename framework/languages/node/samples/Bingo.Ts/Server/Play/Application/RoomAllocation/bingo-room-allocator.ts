import { Inject } from '@nestjs/common';
import { ZLINK_SPOT_MANAGER } from '@zlink-systems/nestjs';
import { BingoNotificationPublisher } from '../../Adapters/ZLink/Notifications/bingo-notification-publisher';
import { createRoomSettings } from '../../Domain/Bingo/bingo-room-models';
import { BingoRoomSpot } from '../../Adapters/ZLink/Spots/bingo-room-spot';
import type {
  ZLinkSpotManager
} from '@zlink-systems/framework';
import { ZLinkSpotCreateState } from '@zlink-systems/framework';
import { BingoModes } from '../../../../Shared/Contracts/messages';
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

  async allocate(mode: BingoMode = BingoModes.twoPlayer): Promise<string> {
    let settings = createRoomSettings(this.roomSeq + 1, mode);
    if (
      this.currentRoomId === null ||
      this.currentRoomSettings === null ||
      this.reservedSeats >= this.currentRoomSettings.requiredPlayers
    ) {
      this.roomSeq += 1;
      settings = createRoomSettings(this.roomSeq, mode);
      const created = await this.spotManager.create(BingoRoomSpot);
      if (created.state !== ZLinkSpotCreateState.Created) {
        throw new Error('Bingo room creation was rejected.');
      }
      await this.executeInRoom(created.spotRid, (room) => {
        room.initializeRoom(settings);
      });
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

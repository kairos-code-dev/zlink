const { Inject } = require('@nestjs/common');
const { BingoRoomDirectory } = require('./bingo-room-directory');
const { allocateBingoRoomRes } = require('../../../Shared/Contracts/messages');
import type {
  AllocateBingoRoomReq,
  AllocateBingoRoomRes
} from '../../../Shared/Contracts/messages';

class AllocateBingoRoomHandler {
  [key: string]: any;
  constructor(rooms: any) {
    this.rooms = rooms;
  }
  async handle(request: AllocateBingoRoomReq): Promise<AllocateBingoRoomRes> {
    const allocated = await this.rooms.allocate(request.mode ?? 'four-player');
    return allocateBingoRoomRes(allocated.roomId);
  }
}

Inject(BingoRoomDirectory)(AllocateBingoRoomHandler, undefined, 0);

export { AllocateBingoRoomHandler };

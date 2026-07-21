import { Injectable } from '@nestjs/common';
import { ZLinkPacket } from '@zlink-systems/framework';
import type {
  ZLinkHandlerContext,
  ZLinkSpotRequestHandler
} from '@zlink-systems/framework';
import type { PlaceMarkRes } from '../../../../../../../Shared/Contracts/messages';
import type { TicTacToeGameSpot } from '../tictactoe-game-spot';

class PlaceMarkAtGameSpotReq {
  constructor(readonly actorId: string, readonly cell: number) {}
}

class VerifyLeaveGameAtSpotReq {
  constructor(readonly actorId: string, readonly roomId: string) {}
}

@Injectable()
@ZLinkPacket('PlaceMarkAtGameSpotReq')
class PlaceMarkAtGameSpotHandler
  implements ZLinkSpotRequestHandler<TicTacToeGameSpot, PlaceMarkAtGameSpotReq, PlaceMarkRes> {
  async handle(
    spot: TicTacToeGameSpot,
    request: PlaceMarkAtGameSpotReq,
    _context: ZLinkHandlerContext
  ): Promise<PlaceMarkRes> {
    return spot.placeMark(request.actorId, request.cell);
  }
}

@Injectable()
@ZLinkPacket('VerifyLeaveGameAtSpotReq')
class VerifyLeaveGameAtSpotHandler
  implements ZLinkSpotRequestHandler<TicTacToeGameSpot, VerifyLeaveGameAtSpotReq, { readonly allowed: true }> {
  async handle(
    spot: TicTacToeGameSpot,
    request: VerifyLeaveGameAtSpotReq,
    _context: ZLinkHandlerContext
  ): Promise<{ readonly allowed: true }> {
    spot.verifyLeave(request.actorId, request.roomId);
    return { allowed: true };
  }
}

export {
  PlaceMarkAtGameSpotHandler,
  PlaceMarkAtGameSpotReq,
  VerifyLeaveGameAtSpotHandler,
  VerifyLeaveGameAtSpotReq
};

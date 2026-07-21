import { Injectable } from '@nestjs/common';
import { ZLinkPacket } from '@zlink-systems/framework';
import { SubmitBingoCardReq } from '../../../../../../../Shared/Contracts/bingo-messages.generated';
import type {
  ZLinkHandlerContext,
  ZLinkSpotRequestHandler
} from '@zlink-systems/framework';
import type { SubmitBingoCardRes } from '../../../../../../../Shared/Contracts/messages';
import type { BingoRoomSpot } from '../bingo-room-spot';

class SubmitBingoCardAtSpotReq {
  constructor(readonly actorId: string, readonly request: SubmitBingoCardReq) {}
}

class VerifyStopObservingAtSpotReq {
  constructor(readonly actorId: string, readonly roomId: string) {}
}

@Injectable()
@ZLinkPacket('SubmitBingoCardAtSpotReq')
class SubmitBingoCardAtSpotHandler
  implements ZLinkSpotRequestHandler<BingoRoomSpot, SubmitBingoCardAtSpotReq, SubmitBingoCardRes> {
  async handle(
    spot: BingoRoomSpot,
    message: SubmitBingoCardAtSpotReq,
    _context: ZLinkHandlerContext
  ): Promise<SubmitBingoCardRes> {
    return spot.submitCard(message.actorId, message.request);
  }
}

@Injectable()
@ZLinkPacket('VerifyStopObservingAtSpotReq')
class VerifyStopObservingAtSpotHandler
  implements ZLinkSpotRequestHandler<BingoRoomSpot, VerifyStopObservingAtSpotReq, {
    readonly stopped: boolean;
    readonly observerNodeRid: string;
  }> {
  async handle(
    spot: BingoRoomSpot,
    message: VerifyStopObservingAtSpotReq,
    _context: ZLinkHandlerContext
  ): Promise<{ readonly stopped: boolean; readonly observerNodeRid: string }> {
    return {
      stopped: spot.verifyStopObserving(message.actorId, { roomId: message.roomId }),
      observerNodeRid: spot.ownerNodeRid()
    };
  }
}

export {
  SubmitBingoCardAtSpotHandler,
  SubmitBingoCardAtSpotReq,
  VerifyStopObservingAtSpotHandler,
  VerifyStopObservingAtSpotReq
};

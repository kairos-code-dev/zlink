import type {
  SubmitBingoCardReq,
  SubmitBingoCardRes
} from '../../../../../../Shared/Contracts/messages';

type BingoActor = {
  actorId: string;
};

type BingoRoomLike = {
  submitCard(actor: BingoActor, request: SubmitBingoCardReq): Promise<SubmitBingoCardRes>;
};

class SubmitBingoCardHandler {
  [key: string]: any;
  async handle(room: BingoRoomLike, actor: BingoActor, request: SubmitBingoCardReq): Promise<SubmitBingoCardRes> {
    return await room.submitCard(actor, request);
  }
}

export { SubmitBingoCardHandler };

import type {
  StartBingoGameReq,
  StateEnvelope
} from '../../../../Shared/Contracts/messages';

type BingoActor = {
  actorId: string;
};

type BingoRoomLike = {
  start(actor: BingoActor, request: StartBingoGameReq): Promise<StateEnvelope>;
};

class StartBingoGameHandler {
  [key: string]: any;
  async handle(room: BingoRoomLike, actor: BingoActor, request: StartBingoGameReq): Promise<StateEnvelope> {
    return await room.start(actor, request);
  }
}

export { StartBingoGameHandler };

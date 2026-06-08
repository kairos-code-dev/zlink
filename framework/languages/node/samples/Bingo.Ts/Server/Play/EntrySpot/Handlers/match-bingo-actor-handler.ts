import type {
  MatchBingoReq,
  MatchBingoRes
} from '../../../../Shared/Contracts/messages';

type BingoActor = {
  actorId: string;
  displayName: string;
};

type BingoEntrySpotLike = {
  match(actor: BingoActor, request: MatchBingoReq): Promise<MatchBingoRes>;
};

class MatchBingoActorHandler {
  [key: string]: any;
  async handle(entrySpot: BingoEntrySpotLike, actor: BingoActor, request: MatchBingoReq): Promise<MatchBingoRes> {
    return await entrySpot.match(actor, request);
  }
}

export { MatchBingoActorHandler };

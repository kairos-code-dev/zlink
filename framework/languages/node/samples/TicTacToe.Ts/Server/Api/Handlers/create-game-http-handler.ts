import { Inject, Injectable } from '@nestjs/common';
import { ZLINK_SPOT_MANAGER } from '@zlink-systems/nestjs';
import { SampleNames } from '../../Configuration/sample-settings';
import { TICTACTOE_SAMPLE_CONFIG } from '../../Configuration/sample-config';
import { TicTacToeGameCreateReq } from '../../../Shared/Contracts/messages';
import type { ZLinkSpotManager } from '@zlink-systems/framework';
import type { TicTacToeSampleConfig } from '../../Configuration/sample-config';
import type {
  CreateGameHttpRes,
  CreateGameHttpReq
} from '../../../Shared/Contracts/messages';

@Injectable()
class CreateGameEndpoint {
  constructor(
    @Inject(ZLINK_SPOT_MANAGER) private readonly spots: ZLinkSpotManager,
    @Inject(TICTACTOE_SAMPLE_CONFIG) private readonly config: TicTacToeSampleConfig
  ) {}

  async handle(request: CreateGameHttpReq): Promise<CreateGameHttpRes> {
    const gameName = request.gameName ?? 'match';
    const created = await this.spots
      .create(SampleNames.gameSpotType)
      .inMesh(SampleNames.playSpotNode)
      .request(new TicTacToeGameCreateReq(gameName, 3))
      .submit();
    return {
      roomId: String(created.spot.spotId),
      gameName,
      playEndpoints: this.config.playEndpoints,
      playNodes: this.config.playEndpoints.map((streamEndpoint) => ({ streamEndpoint })),
      requiredLevel: 3
    };
  }
}

export { CreateGameEndpoint };

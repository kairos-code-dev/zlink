import { Inject } from '@nestjs/common';
import { ZLINK_CHANNEL_CLIENT } from '@zlink-systems/nestjs';
import { SampleNames } from '../../Shared/Configuration/sample-names';
import { bindCourier, PacketNames } from '../../Shared/Contracts/messages';
import type {
  ZLinkChannelClient,
  ZLinkMessage,
  ZLinkSession,
  ZLinkSessionContext,
  ZLinkSessionDispatchContext,
  ZLinkSessionFactory
} from '@zlink-systems/framework';
import type {
  BindCourierRes,
  BindCourierSessionReq,
  BindCourierSessionRes
} from '../../Shared/Contracts/messages';

class CourierSession implements ZLinkSession {
  constructor(
    readonly context: ZLinkSessionContext,
    private readonly channels: ZLinkChannelClient
  ) {}

  async onDispatch(dispatch: ZLinkSessionDispatchContext, payload: ZLinkMessage): Promise<void> {
    if (dispatch.packetName !== PacketNames.bindCourierSession) {
      throw new Error(`Unsupported courier session packet '${dispatch.packetName}'.`);
    }

    const request = payload.decode<BindCourierSessionReq>(Object as never);
    const sessionRoute = this.context.sessionId;
    const bound = await this.channels
      .requestToChannel(SampleNames.courierRouteChannel, bindCourier(request.courierId, sessionRoute))
      .timeout(500)
      .submit<BindCourierRes>();
    console.error(`deliverydispatch courier-session: bound courier=${bound.courierId} actor=${bound.actor.actorId}`);
    await this.context.client.reply({
      courierId: bound.courierId,
      actor: bound.actor,
      sessionRoute: bound.sessionRoute
    } satisfies BindCourierSessionRes).submit();
  }
}

class CourierSessionFactory implements ZLinkSessionFactory<CourierSession> {
  constructor(@Inject(ZLINK_CHANNEL_CLIENT) private readonly channels: ZLinkChannelClient) {}

  async create(context: ZLinkSessionContext): Promise<CourierSession> {
    return new CourierSession(context, this.channels);
  }
}

export {
  CourierSession,
  CourierSessionFactory
};

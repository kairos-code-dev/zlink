import { ZLinkModule, zlinkFramework, zlinkModule } from '@zlink-systems/nestjs';
import { SupportNotificationDeliveryLog } from './notification-delivery-log';
import { SupportUserActorFactory } from './Adapters/ZLink/Actors/support-user-actor-factory';
import { ConversationEventMapper } from './Adapters/ZLink/Notifications/conversation-event-mapper';
import { SupportNotificationPublisher } from './Adapters/ZLink/Notifications/support-notification-publisher';
import { SupportEntrySpot } from './Adapters/ZLink/Spots/support-entry-spot';
import { ConversationSpot } from './Adapters/ZLink/Spots/conversation-spot';
import { SupportConversationAllocator } from './Application/ConversationAssignment/support-conversation-allocator';
import { AgentAvailabilityDirectory } from './Application/ConversationAssignment/agent-availability-directory';
import { AgentAssignmentService } from './Application/ConversationAssignment/agent-assignment-service';
import { SampleNames } from '../Configuration/sample-names';
function createSupportChatSupportModule(config: {
  registryRouterEndpoint: string;
  notificationEndpoint: string;
  supportEndpoint: string;
  apiEndpoint: string;
}) {
  class SupportChatSupportModule {}

  zlinkModule(__dirname, {
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => zlinkFramework()
          .codecs()
            .addJson()
          .useDiscovery()
            .addRegistryEndpoint(config.registryRouterEndpoint)
          .addClientServerChannel(SampleNames.supportChannel)
            .enableServer(config.supportEndpoint)
            .addHandlerGroup('support')
          .addClientServerChannel(SampleNames.apiChannel)
            .enableClient(config.apiEndpoint)
          .addClientServerChannel(SampleNames.notificationChannel)
            .enableServer(config.notificationEndpoint)
            .addHandlerGroup('notifications')
          .actorFactory(SampleNames.supportActorType, SupportUserActorFactory)
          .addSpotNode(SampleNames.conversationSpotType)
            .addEntrySpot(SupportEntrySpot)
            .addSpotFactory(ConversationSpot)
          .build()
      })
    ],
    providers: [
      SupportNotificationDeliveryLog,
      SupportUserActorFactory,
      ConversationEventMapper,
      SupportNotificationPublisher,
      SupportConversationAllocator,
      AgentAvailabilityDirectory,
      AgentAssignmentService,
      SupportEntrySpot
    ]
  })(SupportChatSupportModule);

  return SupportChatSupportModule;
}

export { createSupportChatSupportModule };

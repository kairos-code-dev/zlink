plugins {
    base
    idea
    id("org.jetbrains.kotlin.jvm") version "2.1.0" apply false
    id("org.jetbrains.kotlin.plugin.spring") version "2.1.0" apply false
}

idea {
    module {
        name = "zlink-framework-java-samples"
    }
}

val sampleProjectPaths = listOf(
    ":java:GameQuest:Shared",
    ":java:GameQuest:Server:Configuration",
    ":java:GameQuest:Server:Registry",
    ":java:GameQuest:Server:GameApi",
    ":java:GameQuest:Server:QuestMission",
    ":java:GameQuest:Client",
    ":java:DeliveryDispatch:Shared",
    ":java:DeliveryDispatch:Server:Configuration",
    ":java:DeliveryDispatch:Server:Registry",
    ":java:DeliveryDispatch:Server:DispatchApi",
    ":java:DeliveryDispatch:Server:DispatchCenter",
    ":java:DeliveryDispatch:Server:Courier",
    ":java:DeliveryDispatch:Server:Tracking",
    ":java:DeliveryDispatch:Server:Session",
    ":java:DeliveryDispatch:Probe",
    ":java:DeliveryDispatch:Client",
    ":java:ShoppingMallCheckout:Shared",
    ":java:ShoppingMallCheckout:Server:Configuration",
    ":java:ShoppingMallCheckout:Server:Registry",
    ":java:ShoppingMallCheckout:Server:CommerceApi",
    ":java:ShoppingMallCheckout:Server:OrderWorkflow",
    ":java:ShoppingMallCheckout:Client",
    ":java:ShoppingMall:Shared",
    ":java:ShoppingMall:Server:Configuration",
    ":java:ShoppingMall:Server:Registry",
    ":java:ShoppingMall:Server:CommerceApi",
    ":java:ShoppingMall:Server:OrderWorkflow",
    ":java:ShoppingMall:Client",
    ":java:Bingo:Client",
    ":java:Bingo:Server:Api",
    ":java:Bingo:Server:Configuration",
    ":java:Bingo:Server:Play",
    ":java:Bingo:Server:Registry",
    ":java:Bingo:Server:Session",
    ":java:Bingo:Shared",
    ":java:TicTacToe:Client",
    ":java:TicTacToe:Server",
    ":java:TicTacToe:Shared",
    ":kotlin:GameQuest:Shared",
    ":kotlin:GameQuest:Server:Configuration",
    ":kotlin:GameQuest:Server:Registry",
    ":kotlin:GameQuest:Server:GameApi",
    ":kotlin:GameQuest:Server:QuestMission",
    ":kotlin:GameQuest:Client",
    ":kotlin:DeliveryDispatch:Shared",
    ":kotlin:DeliveryDispatch:Server:Configuration",
    ":kotlin:DeliveryDispatch:Server:Registry",
    ":kotlin:DeliveryDispatch:Server:DispatchApi",
    ":kotlin:DeliveryDispatch:Server:DispatchCenter",
    ":kotlin:DeliveryDispatch:Server:Courier",
    ":kotlin:DeliveryDispatch:Server:Tracking",
    ":kotlin:DeliveryDispatch:Server:Session",
    ":kotlin:DeliveryDispatch:Probe",
    ":kotlin:DeliveryDispatch:Client",
    ":kotlin:ShoppingMallCheckout:Shared",
    ":kotlin:ShoppingMallCheckout:Server:Configuration",
    ":kotlin:ShoppingMallCheckout:Server:Registry",
    ":kotlin:ShoppingMallCheckout:Server:CommerceApi",
    ":kotlin:ShoppingMallCheckout:Server:OrderWorkflow",
    ":kotlin:ShoppingMallCheckout:Client",
    ":kotlin:ShoppingMall:Shared",
    ":kotlin:ShoppingMall:Server:Configuration",
    ":kotlin:ShoppingMall:Server:Registry",
    ":kotlin:ShoppingMall:Server:CommerceApi",
    ":kotlin:ShoppingMall:Server:OrderWorkflow",
    ":kotlin:ShoppingMall:Client",
    ":kotlin:Bingo:Client",
    ":kotlin:Bingo:Server:Api",
    ":kotlin:Bingo:Server:Configuration",
    ":kotlin:Bingo:Server:Play",
    ":kotlin:Bingo:Server:Registry",
    ":kotlin:Bingo:Server:Session",
    ":kotlin:Bingo:Shared",
    ":kotlin:TicTacToe:Client",
    ":kotlin:TicTacToe:Server",
    ":kotlin:TicTacToe:Shared",
)

tasks.register("buildAllSamples") {
    group = LifecycleBasePlugin.BUILD_GROUP
    description = "Builds every Java and Kotlin ZLink sample included in this IDE project."
    dependsOn(sampleProjectPaths.map { "$it:build" })
}

tasks.register("cleanAllSamples") {
    group = LifecycleBasePlugin.BUILD_GROUP
    description = "Cleans every Java and Kotlin ZLink sample included in this IDE project."
    dependsOn(sampleProjectPaths.map { "$it:clean" })
}

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
    ":java:Bingo:Client",
    ":java:Bingo:Server:Api",
    ":java:Bingo:Server:Configuration",
    ":java:Bingo:Server:Play",
    ":java:Bingo:Server:Session",
    ":java:Bingo:Shared",
    ":java:GameQuest:Client",
    ":java:GameQuest:Server:Configuration",
    ":java:GameQuest:Server:GameApi",
    ":java:GameQuest:Server:QuestMission",
    ":java:GameQuest:Shared",
    ":java:TicTacToe:Client",
    ":java:TicTacToe:Server",
    ":java:TicTacToe:Shared",
    ":kotlin:Bingo:Client",
    ":kotlin:Bingo:Server:Api",
    ":kotlin:Bingo:Server:Configuration",
    ":kotlin:Bingo:Server:Play",
    ":kotlin:Bingo:Server:Session",
    ":kotlin:Bingo:Shared",
    ":kotlin:GameQuest:Client",
    ":kotlin:GameQuest:Server:Configuration",
    ":kotlin:GameQuest:Server:GameApi",
    ":kotlin:GameQuest:Server:QuestMission",
    ":kotlin:GameQuest:Shared",
    ":kotlin:ShoppingMall:Client",
    ":kotlin:ShoppingMall:Server:CommerceApi",
    ":kotlin:ShoppingMall:Server:Configuration",
    ":kotlin:ShoppingMall:Server:OrderWorkflow",
    ":kotlin:ShoppingMall:Shared",
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

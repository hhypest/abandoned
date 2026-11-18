#pragma once

#if WITH_DEV_AUTOMATION_TESTS
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "Player/PuzzleCharacter.h"

namespace Abandoned::Verification
{
inline void PlacePlayer(APuzzleCharacter& Player, const FTransform& Start)
{
    const FRotator View = Start.Rotator();
    Player.StopJumping();
    Player.ConsumeMovementInputVector();
    Player.SetBase(nullptr);
    Player.SetActorLocationAndRotation(Start.GetLocation(), FRotator(0, View.Yaw, 0), false, nullptr, ETeleportType::TeleportPhysics);
    Player.GetController()->SetControlRotation(View);
    auto* Movement = Player.GetCharacterMovement();
    Movement->StopMovementImmediately();
    Movement->SetMovementMode(MOVE_Falling);
}
}
#endif

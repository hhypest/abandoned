#include "TrainingChamber.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "TrainingChamberLayout.h"
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Player/MovementProfile.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogWeightBalanceTest, Log, All);

void ATrainingChamberGameMode::VerifyBalanceChamber(ATrainingChamberCharacter& Player, float Delta)
{
    CourseTestTime += Delta;
    const auto Fail = [this](const TCHAR* Reason)
    {
        UE_LOG(LogWeightBalanceTest, Error, TEXT("Balance test failed at step %d: %s"), CourseTestStep, Reason);
        bVerifyBalance = false;
        FPlatformMisc::RequestExitWithStatus(false, 1);
    };
    const auto Next = [this](const TCHAR* Message)
    {
        UE_LOG(LogWeightBalanceTest, Display, TEXT("%s"), Message);
        ++CourseTestStep;
        CourseTestTime = 0.0f;
    };
    if (Plates.Num() != 3 || Cubes.Num() != 6 ||
        Cubes.ContainsByPredicate([](const APuzzleCube* Cube) { return !IsValid(Cube); }) || CourseTestTime > 8.0f)
    {
        Fail(TEXT("Invalid balance fixture or timed out"));
        return;
    }

    const auto HasLoads = [this](float First, float Second, float Third)
    {
        return FMath::IsNearlyEqual(PlateLoads[0], First, 0.01f) &&
            FMath::IsNearlyEqual(PlateLoads[1], Second, 0.01f) &&
            FMath::IsNearlyEqual(PlateLoads[2], Third, 0.01f);
    };
    const auto Snap = [](APuzzleCube& Cube, UBoxComponent& Surface)
    {
        const double Z = Surface.GetScaledBoxExtent().Z + Cube.GetHalfSize() + 0.5;
        return Cube.SnapTo(Surface, Surface.GetComponentLocation() + FVector(0, 0, Z), FRotator::ZeroRotator);
    };
    const auto AimFrom = [this, &Player](double X, double Y, FVector Target)
    {
        const FVector Feet(X, Y, Layout->Spawn.Z);
        const FVector Eye = Feet + FVector(0, 0, Abandoned::MovementProfile::CameraHeight);
        Player.PlaceAt(FTransform((Target - Eye).Rotation(), Feet));
    };
    const auto Pickup = [&](APuzzleCube& Cube)
    {
        const FVector Target = Cube.GetActorLocation();
        AimFrom(Target.X - 140.0, Target.Y, Target);
        Player.Interact();
        return Player.GetHeldCube() == &Cube;
    };

    switch (CourseTestStep)
    {
    case 0:
        if (CourseTestTime >= 0.35f)
        {
            if (!HasLoads(0, 0, 0) || IsExitEnabled())
            {
                Fail(TEXT("An empty chamber must stay closed"));
                return;
            }
            for (int32 Index = 0; Index < Plates.Num(); ++Index)
            {
                APuzzleCube& Small = *Cubes[Index + 2];
                const UBoxComponent& Plate = *Plates[Index].Body;
                Small.ResetPosition(Plate.GetComponentLocation() +
                    FVector(0, 0, Plate.GetScaledBoxExtent().Z + Small.GetHalfSize() + 1.0));
            }
            Next(TEXT("Loaded three equal 5 kg masses."));
        }
        break;
    case 1:
        if (CourseTestTime >= 0.35f)
        {
            if (!HasLoads(5, 5, 5) || IsExitEnabled())
            {
                Fail(TEXT("15 kg distributed evenly must not open the door"));
                return;
            }
            Player.ResetChamber();
            if (!Snap(*Cubes[0], *Plates[0].Body) || !Snap(*Cubes[2], *Cubes[0]->GetBody()) ||
                !Snap(*Cubes[3], *Cubes[2]->GetBody()) || !Snap(*Cubes[1], *Plates[1].Body))
            {
                Fail(TEXT("Could not set up the uneven 20/10/0 kg stack"));
                return;
            }
            Next(TEXT("Equal underload rejected. Checking the correct total on uneven plates."));
        }
        break;
    case 2:
        if (CourseTestTime >= 0.35f)
        {
            if (!HasLoads(20, 10, 0) || IsExitEnabled())
            {
                Fail(TEXT("30 kg distributed unevenly must not open the door"));
                return;
            }
            Player.ResetChamber();
            const FVector Center = Plates[2].Body->GetComponentLocation();
            Cubes[2]->ResetPosition(FVector(Center.X - 230.0, Center.Y, 29.0));
            if (!Snap(*Cubes[0], *Plates[0].Body) || !Snap(*Cubes[1], *Plates[1].Body) || !Pickup(*Cubes[2]))
            {
                Fail(TEXT("Could not pick up the cube for the third plate"));
                return;
            }
            const FVector Target = Center + FVector(0, 0, Plates[2].Body->GetScaledBoxExtent().Z + 67.5);
            const double Height = Target.Z - Layout->Spawn.Z - Abandoned::MovementProfile::CameraHeight;
            AimFrom(Target.X - FMath::Sqrt(FMath::Square(165.0) - FMath::Square(Height)), Target.Y, Target);
            Next(TEXT("Uneven 30 kg rejected. Previewing a snap onto the third plate."));
        }
        break;
    case 3:
        if (Player.PreviewSupport.Get() == Plates[2].Body && !Player.bPreviewBlocked &&
            Cubes[2]->GetBody()->GetPhysicsLinearVelocity().SizeSquared() < FMath::Square(28.0))
        {
            Player.Interact();
            if (Cubes[2]->GetSupport() != Plates[2].Body || !Snap(*Cubes[3], *Cubes[2]->GetBody()))
            {
                Fail(TEXT("The real placement interaction did not snap onto plate 3"));
                return;
            }
            Next(TEXT("Snapped through player input. Each plate now supports 10 kg."));
        }
        break;
    case 4:
        if (IsDoorOpen())
        {
            if (!HasLoads(10, 10, 10))
            {
                Fail(TEXT("The opened door must have a balanced load"));
                return;
            }
            const FVector Base = Cubes[1]->GetActorLocation();
            Cubes[4]->ResetPosition(FVector(Base.X - 200, Base.Y, 29.0));
            if (!Pickup(*Cubes[4]))
            {
                Fail(TEXT("Could not pick up the 5 kg cube for the overload test"));
                return;
            }
            Player.PlaceAt(FTransform(FRotator(-14, 0, 0), Base + FVector(0, 0,
                Cubes[1]->GetHalfSize() + Abandoned::MovementProfile::StandingHalfHeight + 1.0)));
            Next(TEXT("First solution opened the door. Testing player weight plus the held cube."));
        }
        break;
    case 5:
        if (HasLoads(10, 10 + Layout->PlayerMassKg + 5, 10))
        {
            if (IsExitEnabled())
            {
                Fail(TEXT("Player weight and carried mass must break the balance immediately"));
                return;
            }
            if (!Door->GetComponentLocation().Equals(Layout->Door.Center, 1.0))
            {
                return;
            }
            Player.ReleaseHeldObject();
            ReturnCube(Player, 4);
            Player.PlaceAt(FTransform(FRotator::ZeroRotator, Layout->Spawn));
            Next(TEXT("Standing on the middle load with a held cube registered 100 kg and closed the door."));
        }
        break;
    case 6:
        if (IsDoorOpen())
        {
            Player.ResetChamber();
            if (!HasLoads(0, 0, 0) || IsExitEnabled() ||
                !Door->GetComponentLocation().Equals(Layout->Door.Center) ||
                Plates.ContainsByPredicate([this](const FTrainingPressurePlate& Plate)
                {
                    return Plate.bPressed || !Plate.Visual->GetRelativeLocation().IsNearlyZero() ||
                        (Plate.Indicator && !FMath::IsNearlyEqual(Plate.Indicator->Intensity, Layout->PlateLight.Intensity));
                }))
            {
                Fail(TEXT("Reset left a pressure plate, indicator or door active"));
                return;
            }
            if (!Snap(*Cubes[0], *Plates[0].Body) || !Snap(*Cubes[2], *Plates[1].Body) ||
                !Snap(*Cubes[3], *Cubes[2]->GetBody()) || !Snap(*Cubes[4], *Plates[2].Body) ||
                !Snap(*Cubes[5], *Cubes[4]->GetBody()))
            {
                Fail(TEXT("Could not set up the alternate solution"));
                return;
            }
            Next(TEXT("Reset restored all plates. Trying one large cube and two pairs of small ones."));
        }
        break;
    case 7:
        if (IsDoorOpen())
        {
            const FVector CameraStart = Layout->Spawn + FVector(195.0, 0.0, 0.0);
            Player.PlaceAt(FTransform(FRotator(-6, 0, 0), CameraStart));
            Next(TEXT("Alternate arrangement opened the door."));
        }
        break;
    case 8:
        if (CourseTestTime >= 0.15f)
        {
            if (FParse::Param(FCommandLine::Get(), TEXT("CaptureBalanceChamber")))
            {
                FScreenshotRequest::RequestScreenshot(TEXT("WeightBalance"), false, false);
            }
            Next(TEXT("Walking through the exit."));
        }
        break;
    case 9:
        if (CourseTestTime >= 0.15f)
        {
            // Walk from the far side of the plates, past the fixtures used for the last solution.
            if (CourseTestTime - Delta < 0.15f)
            {
                Player.PlaceAt(FTransform(FRotator::ZeroRotator,
                    FVector(Layout->Door.Center.X - 180.0, Layout->Door.Center.Y, Layout->Spawn.Z)));
            }
            Player.AddMovementInput(FVector::ForwardVector);
        }
        break;
    }
}
#endif

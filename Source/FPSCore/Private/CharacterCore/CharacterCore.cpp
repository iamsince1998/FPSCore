// Copyright 2022 Ellie Kelemen. All Rights Reserved.

#include "CharacterCore/CharacterCore.h"
#include "DrawDebugHelpers.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "FPSCharacterController.h"
#include "WeaponCore/Weapon.h"
#include "Components/CapsuleComponent.h"
#include "Components/InteractionComponent.h"
#include "Components/InventoryComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/TimelineComponent.h"
#include "Engine/Engine.h"
#include "TimerManager.h"
#include "Engine/LocalPlayer.h"
#include "Math/UnrealMathUtility.h"
// ReSharper disable once CppUnusedIncludeDirective
#include "EnhancedInputSubsystems.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/KismetMathLibrary.h"

// Sets default values
ACharacterCore::ACharacterCore()
{
 	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
    
}

// Called when the game starts or when spawned
void ACharacterCore::BeginPlay()
{
    Super::BeginPlay();

    if (MovementDataMap.Contains(EMovementState::State_Walk))
    {
        GetCharacterMovement()->MaxWalkSpeed = MovementDataMap[EMovementState::State_Walk].MaxWalkSpeed;
    }
    else
    {
        UE_LOG(LogProfilingDebugging, Error, TEXT("Set up data in MovementDataMap!"))
    }
    

    // Binding a timeline to our vaulting curve
    if (VaultTimelineCurve)
    {
        FOnTimelineFloat TimelineProgress;
        TimelineProgress.BindUFunction(this, FName("TimelineProgress"));
        VaultTimeline.AddInterpFloat(VaultTimelineCurve, TimelineProgress);
    }

    // Obtaining our inventory component and reserving space in memory for our set of weapons
    if (UInventoryComponent* InventoryComp = FindComponentByClass<UInventoryComponent>())
    {
        InventoryComponent = InventoryComp;
        InventoryComponent->GetEquippedWeapons().Reserve(InventoryComponent->GetNumberOfWeaponSlots());
    }

    // Updating the crouched spring arm height based on the crouched Acapsule half height
    DefaultCapsuleHalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight(); // setting the default height of the capsule
    CrouchedSpringArmHeightDelta = CrouchedCapsuleHalfHeight - DefaultCapsuleHalfHeight;
}

void ACharacterCore::PawnClientRestart()
{
    Super::PawnClientRestart();

    // Make sure that we have a valid PlayerController.
    if (const AFPSCharacterController* PlayerController = Cast<AFPSCharacterController>(GetController()))
    {
        // Get the Enhanced Input Local Player Subsystem from the Local Player related to our Player Controller.
        if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
        {
            // PawnClientRestart can run more than once in an Actor's lifetime, so start by clearing out any leftover mappings.
            Subsystem->ClearAllMappings();

            // Add each mapping context, along with their priority values. Higher values outprioritize lower values.
            Subsystem->AddMappingContext(BaseMappingContext, BaseMappingPriority);
        }
    }
}


void ACharacterCore::Move(const FInputActionValue& Value)
{
    // Storing movement vectors for animation manipulation
    ForwardMovement = Value[1];
    RightMovement = Value[0];

    // Moving the player
    if (Value.GetMagnitude() != 0.0f)
    {
        AddMovementInput(GetActorForwardVector(), Value[1]);
        AddMovementInput(GetActorRightVector(), Value[0]);
    }
}

void ACharacterCore::Look(const FInputActionValue& Value)
{
    // Storing look vectors for animation manipulation
    MouseX = Value[1];
    MouseY = Value[0];

    // Looking around
    AddControllerPitchInput(Value[1] * -1);
    AddControllerYawInput(Value[0]);
    
    if (InventoryComponent)
    {
        if (Value.GetMagnitude() != 0.0f && InventoryComponent->GetCurrentWeapon())
        {
            // If movement is detected and we have a current weapon, make sure we don't recover the recoil
            InventoryComponent->GetCurrentWeapon()->SetShouldRecover(false);
            InventoryComponent->GetCurrentWeapon()->GetRecoilRecoveryTimeline()->Stop();
        }
    }
}

void ACharacterCore::ToggleCrouch()
{
    bHoldingCrouch = true;
    if (GetCharacterMovement()->IsMovingOnGround())
    {
        if (MovementState == EMovementState::State_Crouch)
        {
            StopCrouch(false);
        }
        else if (MovementState == EMovementState::State_Sprint && !bPerformedSlide && bCanSlide)
        {
            StartSlide();
        }
        else
        {
            UpdateMovementState(EMovementState::State_Crouch);
            if (bWantsToSprint)
            {
                bWantsToSprint = false;
            }
        }
    }
    else if (!bPerformedSlide)
    {
        // If we are in the air and have not performed a slide yet
        bWantsToSlide = true;
    }
}

void ACharacterCore::ReleaseCrouch()
{
    bHoldingCrouch = false;
    bPerformedSlide = false;
    if (MovementState == EMovementState::State_Slide)
    {
        StopSlide();
    }
    else if (!bCrouchIsToggle)
    {
        if (MovementState == EMovementState::State_Sprint)
        {
            return;
        }
        UpdateMovementState(EMovementState::State_Walk);
    }
}

void ACharacterCore::StopCrouch(const bool bToSprint)
{
    if ((MovementState == EMovementState::State_Crouch || MovementState == EMovementState::State_Slide) && HasSpaceToStandUp())
    {
        if (bToSprint)
        {
            UpdateMovementState(EMovementState::State_Sprint);
        }
        else
        {
            UpdateMovementState(EMovementState::State_Walk);
        }
    }
}

void ACharacterCore::StartSprint()
{
    if (!HasSpaceToStandUp() && (MovementState == EMovementState::State_Crouch || MovementState ==
        EMovementState::State_Slide))
    {
        return;
    }
    bPerformedSlide = false;
    UpdateMovementState(EMovementState::State_Sprint);
    bWantsToSprint = true;
}

void ACharacterCore::StopSprint()
{
    if (MovementState == EMovementState::State_Slide && bHoldingCrouch)
    {
        UpdateMovementState(EMovementState::State_Crouch);
    }
    else if (MovementState == EMovementState::State_Sprint)
    {
        UpdateMovementState(EMovementState::State_Walk);
    }
    bWantsToSprint = false;
}

void ACharacterCore::StartSlide()
{
    bPerformedSlide = true;
    UpdateMovementState(EMovementState::State_Slide);
    GetWorldTimerManager().SetTimer(SlideStop, this, &ACharacterCore::ReleaseCrouch, SlideTime, false, SlideTime);
}

void ACharacterCore::StopSlide()
{
    if (MovementState == EMovementState::State_Slide && FloorAngle > SlideContinueAngle)
    {
        if (!HasSpaceToStandUp())
        {
            UpdateMovementState(EMovementState::State_Crouch);
        }
        else if (bWantsToSprint)
        {
            StopCrouch(true);
        }
        else if (bHoldingCrouch)
        {
            UpdateMovementState(EMovementState::State_Crouch);
        }
        else
        {
            UpdateMovementState(EMovementState::State_Walk);
        }
        bPerformedSlide = false;
        GetWorldTimerManager().ClearTimer(SlideStop);
    }
    else if (FloorAngle < -SlideContinueAngle)
    {
        GetWorldTimerManager().SetTimer(SlideStop, this, &ACharacterCore::ReleaseCrouch, 0.1f, false, 0.1f);
    }
}

void ACharacterCore::StartAds()
{
    bWantsToAim = true;
}

void ACharacterCore::StopAds()
{
    bWantsToAim = false;
}

void ACharacterCore::CheckVault()
{
    if (!bCanVault) return;
    
    float ForwardVelocity = FVector::DotProduct(GetVelocity(), GetActorForwardVector());
    if (!(ForwardVelocity > 0 && !bIsVaulting && GetCharacterMovement()->IsFalling())) return;

    // Store these for future use.
    FVector ColliderLocation = GetCapsuleComponent()->GetComponentLocation();
    FRotator ColliderRotation = GetCapsuleComponent()->GetComponentRotation();
    FVector StartLocation = ColliderLocation;
    FVector EndLocation = ColliderLocation + UKismetMathLibrary::GetForwardVector(ColliderRotation) * 75;
    if (bDrawDebug)
    {
        DrawDebugCapsule(GetWorld(), StartLocation, 50, 30, FQuat::Identity, FColor::Red);
    }

    FCollisionQueryParams TraceParams;
    TraceParams.bTraceComplex = true;
    TraceParams.AddIgnoredActor(this);

    // Checking if we are near a wall
    if (!GetWorld()->SweepSingleByChannel(MantleHit, StartLocation, EndLocation, FQuat::Identity, ECC_WorldStatic,
                                          FCollisionShape::MakeCapsule(30, 50), TraceParams)) return;
    if (!MantleHit.bBlockingHit) return;
    
    FVector ForwardImpactPoint = MantleHit.ImpactPoint;
    FVector ForwardImpactNormal = MantleHit.ImpactNormal;
    FVector CapsuleLocation = ForwardImpactPoint;
    CapsuleLocation.Z = ColliderLocation.Z;
    CapsuleLocation += ForwardImpactNormal * -15;
    StartLocation = CapsuleLocation;
    StartLocation.Z += 100;
    EndLocation = CapsuleLocation;

    // Checking if we can stand up on the wall that we've hit
    if (!GetWorld()->SweepSingleByChannel(MantleHit, StartLocation, EndLocation, FQuat::Identity, ECC_WorldStatic, FCollisionShape::MakeSphere(1), TraceParams)) return;
    if (!GetCharacterMovement()->IsWalkable(MantleHit)) return;
    
    FVector SecondaryVaultStartLocation = MantleHit.ImpactPoint;
    SecondaryVaultStartLocation.Z += 5;
    FVector SecondaryVaultEndLocation = SecondaryVaultStartLocation;
    SecondaryVaultEndLocation.Z = 0;
    FVector SecondaryVaultHeightCheckLocation = SecondaryVaultStartLocation;
    SecondaryVaultHeightCheckLocation.Z += VaultSpaceHeight;

    if (bDrawDebug)
    {
        DrawDebugSphere(GetWorld(), SecondaryVaultStartLocation, 10, 8, FColor::Orange);
    }

    float InitialTraceHeight = 0;
    float PreviousTraceHeight = 0;
    float CurrentTraceHeight = 0;
    bool bInitialSwitch = false;
    bool bVaultFailed = true;

    int i;

    FVector ForwardAddition = UKismetMathLibrary::GetForwardVector(ColliderRotation) * 5;
    float CalculationHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2;
    float ScaledCapsuleWithoutHemisphere = GetCapsuleComponent()->GetScaledCapsuleHalfHeight_WithoutHemisphere();

    // Tracing downwards VaultTraceAmount times and looking for a significant change in height followed by a space large enough to stand
    for (i = 0; i <= VaultTraceAmount; i++)
    {
        SecondaryVaultStartLocation += ForwardAddition;
        SecondaryVaultEndLocation += ForwardAddition;
        SecondaryVaultHeightCheckLocation += ForwardAddition;
        bVaultFailed = true;
        if (!GetWorld()->LineTraceSingleByChannel(VaultHit, SecondaryVaultStartLocation, SecondaryVaultEndLocation, ECC_WorldStatic, TraceParams)) continue;
        if (bDrawDebug)
        {
            DrawDebugLine(GetWorld(), SecondaryVaultStartLocation, VaultHit.ImpactPoint, FColor::Red, false, 10.0f, 0.0f, 2.0f);
        }

        
        if (bDrawDebug)
        {
            DrawDebugLine(GetWorld(), SecondaryVaultStartLocation, SecondaryVaultHeightCheckLocation, FColor::Green, false, 10.0f, 0.0f, 2.0f);
        }
        if (GetWorld()->LineTraceSingleByChannel(VaultHeightHit, SecondaryVaultStartLocation, SecondaryVaultHeightCheckLocation, ECC_WorldStatic, TraceParams)) break;
        
        float TraceLength = SecondaryVaultStartLocation.Z - VaultHit.ImpactPoint.Z;
        if (!bInitialSwitch)
        {
            InitialTraceHeight = TraceLength;
            bInitialSwitch = true;
        }

        PreviousTraceHeight = CurrentTraceHeight;
        CurrentTraceHeight = TraceLength;
        if (!(!(FMath::IsNearlyEqual(CurrentTraceHeight, InitialTraceHeight, 20.0f)) && CurrentTraceHeight < MaxMantleHeight)) continue;

        if (!FMath::IsNearlyEqual(PreviousTraceHeight, CurrentTraceHeight, 3.0f)) continue;

        FVector DownTracePoint = VaultHit.Location;
        DownTracePoint.Z = VaultHit.ImpactPoint.Z;
 
        FVector CalculationVector = FVector::ZeroVector;
        CalculationVector.Z = CalculationHeight;
        DownTracePoint += CalculationVector;
        StartLocation = DownTracePoint;
        StartLocation.Z += ScaledCapsuleWithoutHemisphere;
        EndLocation = DownTracePoint;
        EndLocation.Z -= ScaledCapsuleWithoutHemisphere;

        if (bDrawDebug)
        {
           DrawDebugCapsule(GetWorld(), StartLocation, GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(), GetCapsuleComponent()->GetUnscaledCapsuleRadius(), FQuat::Identity , FColor::Green, false, 10.0f);
        }
        if (GetWorld()->SweepSingleByChannel(VaultHit, StartLocation, EndLocation, FQuat::Identity, ECC_WorldStatic, FCollisionShape::MakeSphere(GetCapsuleComponent()->GetUnscaledCapsuleRadius()), TraceParams)) continue;

        // If we find such a location, break the loop and vault
        ForwardImpactNormal.X -= 1;
        ForwardImpactNormal.Y -= 1;
        VaultTargetLocation = FTransform(UKismetMathLibrary::MakeRotFromX(ForwardImpactNormal), DownTracePoint);
        bIsVaulting = true;
        Vault(VaultTargetLocation);
        bVaultFailed = false;
        break;
    }

    if (!bVaultFailed) return;

    // If the vault has failed (there is no space or the surface is too high), we proceed to perform the mantle logic
    
    FVector DownTracePoint = MantleHit.Location;
    DownTracePoint.Z = MantleHit.ImpactPoint.Z;
 
    FVector CalculationVector = FVector::ZeroVector;
    CalculationVector.Z = GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2;
    DownTracePoint += CalculationVector;
    StartLocation = DownTracePoint;
    StartLocation.Z += GetCapsuleComponent()->GetScaledCapsuleHalfHeight_WithoutHemisphere();
    EndLocation = DownTracePoint;
    EndLocation.Z -= GetCapsuleComponent()->GetScaledCapsuleHalfHeight_WithoutHemisphere();

    // Looking for a safe place to mantle to
     if (GetWorld()->SweepSingleByChannel(MantleHit, StartLocation, EndLocation, FQuat::Identity, ECC_WorldStatic,
                                         FCollisionShape::MakeSphere(GetCapsuleComponent()->GetUnscaledCapsuleRadius()),
                                         TraceParams)) return;

    ForwardImpactNormal.X -= 1;
    ForwardImpactNormal.Y -= 1;
    VaultTargetLocation = FTransform(UKismetMathLibrary::MakeRotFromX(ForwardImpactNormal), DownTracePoint);
    bIsVaulting = true;

    // Calling vault with our mantle target point
    Vault(VaultTargetLocation);
}

// Progresses the timeline that is used to vault the character
void ACharacterCore::TimelineProgress(const float Value)
{
    const FVector NewLocation = FMath::Lerp(VaultStartLocation.GetLocation(), VaultEndLocation.GetLocation(), Value);
    SetActorLocation(NewLocation);
    if (Value == 1)
    {
        bIsVaulting = false;
        if (bWantsToSprint)
        {
            UpdateMovementState(EMovementState::State_Sprint);
        }
    }
}

void ACharacterCore::CheckGroundAngle(float DeltaTime)
{
    FCollisionQueryParams TraceParams;
    TraceParams.bTraceComplex = true;
    TraceParams.AddIgnoredActor(this);

    // Determines the angle of the floor from the vector of a hit line trace
    FVector CapsuleHeight = GetCapsuleComponent()->GetComponentLocation();
    CapsuleHeight.Z -= GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    const FVector AngleStartTrace = CapsuleHeight;
    FVector AngleEndTrace = AngleStartTrace;
    AngleEndTrace.Z -= 50;
    if (GetWorld()->LineTraceSingleByChannel(AngleHit, AngleStartTrace, AngleEndTrace, ECC_WorldStatic, TraceParams))
    {
        const FVector FloorVector = AngleHit.ImpactNormal;
        const FRotator FinalRotation = UKismetMathLibrary::MakeRotFromZX(FloorVector, GetActorForwardVector());
        FloorAngle = FinalRotation.Pitch;
        if (bDrawDebug)
        {
            GEngine->AddOnScreenDebugMessage(-1, DeltaTime, FColor::Red,
                                             FString::Printf(TEXT("Current floor angle = %f"), FloorAngle), true);
        }
    }
}

float ACharacterCore::CheckRelativeMovementAngle(const float DeltaTime) const
{
    const FVector MovementVector = GetVelocity();
    const FRotator MovementRotator = GetActorRotation();
    const FVector RelativeMovementVector = MovementRotator.UnrotateVector(MovementVector);

    if (bDrawDebug)
    {
        GEngine->AddOnScreenDebugMessage(-1, DeltaTime, FColor::Orange, FString::SanitizeFloat(FMath::Abs(RelativeMovementVector.HeadingAngle() * (180/PI))));    
    }
    
    return FMath::Abs(RelativeMovementVector.HeadingAngle());
}

bool ACharacterCore::HasSpaceToStandUp()
{
    FVector CenterVector = GetActorLocation();
    CenterVector.Z += 44;

    const float CollisionCapsuleHeight = DefaultCapsuleHalfHeight - 17.0f;

    // Check to see if a capsule collision collides with the environment, if yes, we don't have space to stand up
    const FCollisionShape CollisionCapsule = FCollisionShape::MakeCapsule(30.0f, CollisionCapsuleHeight);

    if (bDrawDebug)
    {
        DrawDebugCapsule(GetWorld(), CenterVector, CollisionCapsuleHeight, 30.0f, FQuat::Identity, FColor::Red, false, 5.0f, 0, 3);
    }

    FCollisionQueryParams QueryParams;
    QueryParams.AddIgnoredActor(this);
    
    if (GetWorld()->SweepSingleByChannel(StandUpHit, CenterVector, CenterVector, FQuat::Identity, ECC_WorldStatic, CollisionCapsule, QueryParams))
    {
        /* confetti or smth idk */
        if (bDrawDebug)
        {
            GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Red, "Stand up trace returned hit", true);
        }
        return false;
    }

    return true;
}

void ACharacterCore::Vault(const FTransform TargetTransform)
{
    // Updating our target location and playing the vault timeline from start
    VaultStartLocation = GetActorTransform();
    VaultEndLocation = TargetTransform;
    UpdateMovementState(EMovementState::State_Vault);
    VaultTimeline.PlayFromStart();
}

// Function that determines the player's maximum speed and other related variables based on movement state
void ACharacterCore::UpdateMovementState(const EMovementState NewMovementState)
{
    // Clearing sprinting and Acrouching flags
    bIsSprinting = false;
    bIsCrouching = false;

    // Updating the movement state
    MovementState = NewMovementState;

    if (MovementDataMap.Contains(MovementState))
    {
        // Updating CharacterMovementComponent variables based on movement state
        if (InventoryComponent)
        {
            if (InventoryComponent->GetCurrentWeapon())
            {
                InventoryComponent->GetCurrentWeapon()->SetCanFire(MovementDataMap[MovementState].bCanFire);
                InventoryComponent->GetCurrentWeapon()->SetCanReload(MovementDataMap[MovementState].bCanReload);
            }
        }
        GetCharacterMovement()->MaxAcceleration = MovementDataMap[MovementState].MaxAcceleration;
        GetCharacterMovement()->BrakingDecelerationWalking = MovementDataMap[MovementState].BreakingDecelerationWalking;
        GetCharacterMovement()->GroundFriction = MovementDataMap[MovementState].GroundFriction;
        GetCharacterMovement()->MaxWalkSpeed = MovementDataMap[MovementState].MaxWalkSpeed;
    }
    else
    {
        UE_LOG(LogProfilingDebugging, Error, TEXT("Set up data in MovementDataMap!"))
    }

    // Updating sprinting and Acrouching flags
    if (MovementState == EMovementState::State_Crouch)
    {
        bIsCrouching = true;
    }
    if (MovementState == EMovementState::State_Sprint)
    {
        bIsSprinting = true;
    }
}

// Called every frame
void ACharacterCore::Tick(const float DeltaTime)
{
	Super::Tick(DeltaTime);

    // Timeline tick
    VaultTimeline.TickTimeline(DeltaTime);

	// Crouching
	// Sets the new Target Half Height based on whether the player is crouching or standing
	const float TargetHalfHeight = (MovementState == EMovementState::State_Crouch || MovementState == EMovementState::State_Slide)? CrouchedCapsuleHalfHeight : DefaultCapsuleHalfHeight;
    // Interpolates between the current height and the target height
	const float NewHalfHeight = FMath::FInterpTo(GetCapsuleComponent()->GetScaledCapsuleHalfHeight(), TargetHalfHeight, DeltaTime, CrouchSpeed);
	// Sets the half height of the capsule component to the new interpolated half height
	GetCapsuleComponent()->SetCapsuleHalfHeight(NewHalfHeight);

    if (bRestrictSprintAngle)
    {
        const float CurrentRelativeMovementAngle = CheckRelativeMovementAngle(DeltaTime);
        
        // Sprinting
        if (CurrentRelativeMovementAngle > (SprintAngleLimit * (PI/180)) && MovementState == EMovementState::State_Sprint)
        {
            UpdateMovementState(EMovementState::State_Walk);
            bRestrictingSprint = true;
        }
        else if (CurrentRelativeMovementAngle < (SprintAngleLimit * (PI/180)) && bRestrictingSprint && bWantsToSprint && MovementState != EMovementState::State_Sprint)
        {
            UpdateMovementState(EMovementState::State_Sprint);
            bRestrictingSprint = false;
        }
    }
    
    // Continuous aiming check (so that you don't have to re-press the ADS button every time you jump/sprint/reload/etc)
    if (bWantsToAim == true && MovementState != EMovementState::State_Sprint && MovementState != EMovementState::State_Slide)
    {
        bIsAiming = true;
    }
    else
    {
        bIsAiming = false;
    }

    // Slide performed Acheck, so that if the player is in the air and presses the slide key, they slide when they land
    if (GetCharacterMovement()->IsMovingOnGround() && !bPerformedSlide && bWantsToSlide)
    {
        StartSlide();
        bWantsToSlide = false;
    }

    // Checks whether we can vault every frame
    CheckVault();

    // Checks the floor angle to determine whether we should keep sliding or not
    CheckGroundAngle(DeltaTime);

    if (bDrawDebug)
    {
        if (InventoryComponent)
        {
            for ( int Index = 0; Index < InventoryComponent->GetNumberOfWeaponSlots(); Index++ )
            {
                if (InventoryComponent->GetEquippedWeapons().Contains(Index))
                {
                    GEngine->AddOnScreenDebugMessage(-1, DeltaTime, FColor::Red, FString::SanitizeFloat(InventoryComponent->GetEquippedWeapons()[Index]->GetRuntimeWeaponData()->ClipSize));
                    GEngine->AddOnScreenDebugMessage(-1, DeltaTime, FColor::Red, FString::SanitizeFloat(InventoryComponent->GetEquippedWeapons()[Index]->GetRuntimeWeaponData()->ClipCapacity));
                    GEngine->AddOnScreenDebugMessage(-1, DeltaTime, FColor::Red, FString::SanitizeFloat(InventoryComponent->GetEquippedWeapons()[Index]->GetRuntimeWeaponData()->WeaponHealth));
                }
                else
                {
                    GEngine->AddOnScreenDebugMessage(-1, DeltaTime, FColor::Red, TEXT("No Weapon Found"));
                    GEngine->AddOnScreenDebugMessage(-1, DeltaTime, FColor::Red, TEXT("No Weapon Found"));
                    GEngine->AddOnScreenDebugMessage(-1, DeltaTime, FColor::Red, TEXT("No Weapon Found"));
                }
                GEngine->AddOnScreenDebugMessage(-1, DeltaTime, FColor::Red, FString::FromInt(Index));
            }
        }
    }
}

// Called to bind functionality to input
void ACharacterCore::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

    // Make sure that we are using a UEnhancedInputComponent; if not, the project is not configured correctly.
    if (UEnhancedInputComponent* PlayerEnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
    {        
        if (UInteractionComponent* InteractionComponent = FindComponentByClass<UInteractionComponent>())
        {
            InteractionComponent->InteractAction = InteractAction;
            InteractionComponent->SetupInputComponent(PlayerEnhancedInputComponent);
        }

        if (UInventoryComponent* InventoryComp = FindComponentByClass<UInventoryComponent>())
        {            
            InventoryComp->FiringAction = FiringAction;
            InventoryComp->PrimaryWeaponAction = PrimaryWeaponAction;
            InventoryComp->SecondaryWeaponAction = SecondaryWeaponAction;
            InventoryComp->ReloadAction = ReloadAction;
            InventoryComp->ScrollAction = ScrollAction;
            InventoryComp->InspectWeaponAction = InspectWeaponAction;
            
            InventoryComp->SetupInputComponent(PlayerEnhancedInputComponent);
        }
        
        if (JumpAction)
        {
            // Jumping
            PlayerEnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacterCore::Jump);
        }
        
        if (SprintAction)
        {
            // Sprinting
            PlayerEnhancedInputComponent->BindAction(SprintAction, ETriggerEvent::Started, this, &ACharacterCore::StartSprint);
            PlayerEnhancedInputComponent->BindAction(SprintAction, ETriggerEvent::Completed, this, &ACharacterCore::StopSprint);
        }

        if (MovementAction)
        {
            // Move forward/back + left/right inputs
            PlayerEnhancedInputComponent->BindAction(MovementAction, ETriggerEvent::Triggered, this, &ACharacterCore::Move);
        }

        if (LookAction)
        {
            // Look up/down + left/right
            PlayerEnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &ACharacterCore::Look);
        }

        if (AimAction)
        {
            // Aiming
            PlayerEnhancedInputComponent->BindAction(AimAction, ETriggerEvent::Started, this, &ACharacterCore::StartAds);
            PlayerEnhancedInputComponent->BindAction(AimAction, ETriggerEvent::Completed, this, &ACharacterCore::StopAds);
        }

        if (CrouchAction)
        {
            // Crouching
            PlayerEnhancedInputComponent->BindAction(CrouchAction, ETriggerEvent::Started, this, &ACharacterCore::ToggleCrouch);
            PlayerEnhancedInputComponent->BindAction(CrouchAction, ETriggerEvent::Completed, this, &ACharacterCore::ReleaseCrouch);
        }
    }
}

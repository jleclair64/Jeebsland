#include "KnightCharacter.h"
#include "ChoppableTree.h"
#include "MiningRock.h"
#include "Terrain.h"
#include "OpenWorld.h"
#include "Components/DecalComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "KnightAnimator.h"
#include "SkillComponent.h"
#include "Inventory.h"
#include "Components/CapsuleComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ProceduralMeshComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "JeebProps.h"
#include "Materials/MaterialInstanceDynamic.h"

// The knight's pose, every frame (the last big part of Tick): no game rules here. The eased blends a few things read, what he is
// doing as the animator sees it, then FKnightAnimator makes the pose (it picks and blends the clips) and his gear goes where the pose puts it.
// It runs on every machine, the server too: hit boxes and hurt boxes are read from this pose.

// What he is doing, from what every machine knows: replicated state and each machine's own timers, never a held key or the camera
void AKnightCharacter::FillAnimInput(JeebAnim::FInput& In) const
{
	using JeebAnim::EMove;
	const UCharacterMovementComponent* M = GetCharacterMovement();
	In.Vel = GetVelocity();
	In.Rot = GetActorRotation();
	In.bFalling = M->IsFalling();
	In.StunLeft = StunLeft;
	In.bKnockedDown = bKnockedDown;
	In.KnockPhase = KnockPhase;
	In.bRolling = bRolling;
	In.RollU = RollT / FMath::Max(RollDur, 0.01f);
	In.RollYaw = FMath::FindDeltaAngleDegrees(In.Rot.Yaw, RollDir.Rotation().Yaw);
	In.LegYaw = LegYaw;
	In.CrouchAlpha = FMath::Max(CrouchAlpha, LeapCrouch);   // (the troll crouches as he charges a jump)
	In.BlockAlpha = BlockAlpha;
	In.LeapFly = (bAirLaunch && LeapAt > 0.0 && LeapShare >= 0.f) ? LeapShare : -1.f;   // (a charged jump in flight, not a hop)
	In.BowAimAlpha = BowAimAlpha;
	In.BowMode = Bow == EBow::Drawing ? 1 : Bow == EBow::Recover ? 2 : Bow == EBow::Reload ? 3 : 0;
	In.BowT = Bow == EBow::Reload ? JeebTune::V().CrossReload - CrossReloadLeft : BowT;
	In.BowDrawFor = DrawTimeNow();
	In.Kind = InRockMode() ? -1 : SelectedKind();   // (the troll with rocks in hand: the club is put away)
	In.RockMode = !bIsTroll ? 0 : Rock == ERock::Charging ? 1 : Rock == ERock::Throw ? 2 : 0;   // (KnightAnimatorTrollThrow.cpp)
	In.RockU = Rock == ERock::Charging ? RockCharge : RockThrowCharge;
	In.RockT = RockT;
	In.bRockSack = InRockMode() && !bStowed;
	In.AimPitch = FMath::UnwindDegrees(GetBaseAimRotation().Pitch);
	In.bClimbing = bClimbing;
	In.ClimbPhase = ClimbPhase;
	In.bSitting = bSitting;
	In.bMounted = bMounted || bOnBroom; In.bBroom = bOnBroom;   // (the broom rides astride, as the horse: KnightCharacterBroom.cpp)
	In.bSliding = bSliding;
	In.bFishing = bFishing;
	In.Emote = EmoteId; In.EmoteT = EmoteT; In.bEmoteStow = EmoteStows();   // (KnightCharacterEmote.cpp)
	In.bDrinking = bDrinking;
	In.DrinkU = DrinkT / FMath::Max(JeebTune::V().DrinkTime, 0.1f);
	In.bSwinging = bSwinging && GatherWalkLeft <= 0.f && In.Vel.Size2D() < 50.f;   // (walking into place or off with the tool: he walks, then swings. Other machines go by his speed.)
	In.bShieldArm = !bIsWizard && ShieldActive() && !bShieldBack && !bStowed;
	In.bWizHold = bIsWizard && WizardBookHeld && !bStowed && !bSwinging && !bFishing && !bDrinking;   // (2026-11-22: his book, ball or pipe held out on his left palm)
	if (bOnBroom) In.bWizHold = false;   // (2026-11-24: his off hand is on the broom, the item is hidden)
	static IConsoleVariable* LeanTest = IConsoleManager::Get().RegisterConsoleVariable(TEXT("jeeb.Test.BroomLean"), 0, TEXT("Test pictures: show the broom sprint lean while hovering (looks only)."), ECVF_Default);
	In.bBroomSprint = BroomSprinting() || (bOnBroom && LeanTest->GetInt() != 0);
	In.BroomPitch = BroomPitchNow;
	In.bSideways = bSideways;
	In.SwingU = FMath::Fmod(SwingTime, FMath::Max(SwingPeriod, 0.1f)) / FMath::Max(SwingPeriod, 0.1f);
	In.bShotLog = bShotFace;
	FillWallInput(In);   // (the free climb and the mantle: KnightCharacterClimb.cpp)
	if (bAttacking)
	{
		const float D = FMath::Max(AttackDur, 0.01f);
		In.MoveU = AttackT / D;
		In.SmashCharge = SmashCharge;
		In.DashFrom = DashActiveFrom / D;
		In.DashTo = DashActiveTo / D;
		In.ReboundU = SmashT / FMath::Max(ReboundDur, 0.05f);
		if (Smash == ESmash::Rebound) In.Move = EMove::Rebound;
		else if (AtkStyle == EAtkStyle::Kick) In.Move = EMove::Kick;
		else if (AtkStyle == EAtkStyle::Sweep) In.Move = EMove::Sweep;
		else if (AtkStyle == EAtkStyle::Dash) In.Move = EMove::Dash;
		else if (AtkStyle == EAtkStyle::Thrust) In.Move = EMove::Thrust;
		else if (AtkStyle == EAtkStyle::Cast) In.Move = EMove::Cast;
		else if (Smash == ESmash::Charging) In.Move = EMove::SmashCharge;
		else if (Smash == ESmash::Swing)
		{
			In.Move = EMove::SmashSwing;
			In.SmashSwingU = FMath::Clamp((AttackT - SmashApexT * D) / ((SmashPauseT - SmashApexT) * D), 0.f, 1.f);
		}
		else if (Smash == ESmash::EndLag)
		{
			In.Move = EMove::SmashEnd;
			In.SmashEndU = FMath::Clamp((SmashT + AttackT - SmashPauseT * D) / FMath::Max(JeebTune::V().SmashEndPause + (1.f - SmashPauseT) * D, 0.01f), 0.f, 1.f);
		}
		else In.Move = AttackDir == JabSecond ? EMove::Jab2 : EMove::Jab1;
	}
	else if (const float Hold = CastHoldNow(); Hold > 0.f)
	{
		In.Move = EMove::Cast;   // (charging a spell: the cast pose held until release)
		In.MoveU = Hold;
	}
}

void AKnightCharacter::TickPose(float Dt, const FVector& Vel, float Speed, bool bFalling, bool bFaceCam, float& OutS, float& OutC)
{
	// Landing: the dust reads how hard
	if (bFalling)
	{
		if (!bWasFalling) LastFallVz = 0.f;
		LastFallVz = FMath::Min(LastFallVz, float(Vel.Z));
	}
	else if (bWasFalling)
	{
		LandImpact = FMath::Clamp(-LastFallVz / 900.f, 0.3f, 1.f);
		LandTimer = LandDuration;
	}
	bWasFalling = bFalling;
	LandTimer = FMath::Max(0.f, LandTimer - Dt);

	// The blends (everything eases in and out)
	MoveBlend = FMath::FInterpTo(MoveBlend, FMath::Clamp(Speed / 150.f, 0.f, 1.f), Dt, 10.f);
	RunAlpha = FMath::FInterpTo(RunAlpha, FMath::Clamp((Speed - 250.f) / 500.f, 0.f, 1.f), Dt, 6.f);
	AirAlpha = FMath::FInterpTo(AirAlpha, bFalling ? 1.f : 0.f, Dt, 14.f);
	const bool bSweepSquat = bAttacking && AtkStyle == EAtkStyle::Sweep && AttackT < 0.76f * AttackDur;   // (the low sweep drops him into the squat, quickly)
	CrouchAlpha = FMath::FInterpTo(CrouchAlpha, (bCrouched || bSweepSquat) ? 1.f : 0.f, Dt, bSweepSquat ? 22.f : 10.f);
	BlockAlpha = FMath::FInterpTo(BlockAlpha, bBlocking ? 1.f : 0.f, Dt, 14.f);
	if (bSwinging) SwingTime = Speed < 50.f && GatherWalkLeft <= 0.f ? SwingTime + Dt : 0.f;
	const bool bBowIn = JeebItems::IsShooter(SelectedKind()) && !bStowed && BowHold;
	BowAimAlpha = FMath::FInterpTo(BowAimAlpha, (bBowIn && Bow != EBow::None) ? 1.f : 0.f, Dt, Bow == EBow::Drawing ? 12.f : 8.f);
	if (!BowHold) BowAimAlpha = 0.f;   // (the bow was put away mid-draw: R, a drop, death)

	// Facing the camera (strafe mode, shield stance), Fortnite style: the hips and legs turn up to 45 degrees toward the way he goes
	{
		const FVector LV = GetActorRotation().UnrotateVector(FVector(Vel.X, Vel.Y, 0.f));
		float Fw = 1.f, Sd = 0.f;
		if ((bBlockHeld || bStrafeMode) && !bMounted && LV.Size() > 30.f) { Fw = LV.X / LV.Size(); Sd = LV.Y / LV.Size(); }
		StepFw = FMath::FInterpTo(StepFw, Fw, Dt, 8.f);
		StepSd = FMath::FInterpTo(StepSd, Sd, Dt, 8.f);
		float WantYaw = 0.f;
		if (bFaceCam && (FMath::Abs(StepFw) + FMath::Abs(StepSd)) > 0.2f)
		{
			const float Ang = FMath::RadiansToDegrees(FMath::Atan2(StepSd, StepFw));   // 0 ahead, +90 right, 180 behind
			const float Toward = FMath::Abs(Ang) <= 90.f ? Ang : Ang - 180.f * FMath::Sign(Ang);
			WantYaw = FMath::Clamp(Toward, -45.f, 45.f);
		}
		LegYaw = FMath::FInterpTo(LegYaw, WantYaw * MoveBlend, Dt, 8.f);
	}

	TickSuitTests(Dt);
	if (Anim && Anim->IsReady())
	{
		// (nobody is looking at the server's knights: a pose ten times a second serves their hurt boxes, every frame only while a blade is out.
		// Other players' knights off screen likewise. The gear follows the capsule in between.)
		FVector SnowFeetAt = GetActorLocation() - FVector(0.f, 0.f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
		SnowFeet(Dt, SnowFeetAt);
		Anim->MoveTo(SnowFeetAt, GetActorRotation().Yaw);   // (the boxes stay on him between poses)
		AnimAccum += Dt;
		float Every = 0.f;
		if (IsNetMode(NM_DedicatedServer)) Every = bAttacking ? 0.f : 0.1f;
		else if (!IsLocallyControlled() && Anim->Mesh() && !Anim->Mesh()->WasRecentlyRendered(0.25f)) Every = 0.2f;
		if (AnimAccum >= Every)
		{
			JeebAnim::FInput In;
			FillAnimInput(In);
			if (bShotFace && !ShotGallery.IsSet()) ShotGallery = FParse::Param(FCommandLine::Get(), TEXT("SuitGallery"));
			if (ShotGallery.Get(false))
			{
				SetActorHiddenInGame(true);
				Anim->TickGallery(this, GetActorLocation() - FVector(0.f, 0.f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
			}
			else
			{
				const FVector Feet = SnowFeetAt;   // (sunk into deep snow: SnowFeet)
				Anim->Update(AnimAccum, In, Feet, GetActorRotation().Yaw);
				PlaceGear();
			}
			AnimAccum = 0.f;
		}
		// (the dust's footfalls: on the stride's two marks)
		const float P = 2.f * PI * (Anim->Phase01() - 0.15f);
		OutS = FMath::Sin(P);
		OutC = FMath::Cos(P);
	}
	if (!IsNetMode(NM_DedicatedServer)) TickCape(Dt);
}

// The staff's orb glow and its place in the fist, live in Play (2026-11-18 second review: the orb read white and blew out at night; the
// carried fist sat off the shaft). Players' machines only: nothing here changes what the game decides.
static TAutoConsoleVariable<float> CVarOrbGlow(TEXT("jeeb.Magic.OrbGlow"), 1.7f, TEXT("The staff's orb: its glow's brightness on screen (M_PropGlow GlowStrength; with the exposure lock about 1 is white's edge; keep MI_Staff's default in Tools/fixlook_assets.py equal)."), ECVF_Default);
static TAutoConsoleVariable<float> CVarOrbLock(TEXT("jeeb.Magic.OrbLock"), 1.f, TEXT("The staff's orb: 1 keeps its glow the same brightness on screen by day and night; 0 is a glow in the scene's light (it whitens at night)."), ECVF_Default);
static TAutoConsoleVariable<FString> CVarStaffGrip(TEXT("jeeb.Knight.StaffGrip"), TEXT("0,0,0"), TEXT("The staff moved in the fist, in its own frame (cm: X and Y across the shaft, Z along it, + toward the butt)."), ECVF_Default);

void AKnightCharacter::PlaceStaffModel()
{
	TArray<FString> P;
	CVarStaffGrip.GetValueOnGameThread().ParseIntoArray(P, TEXT(","));
	FVector Nudge = P.Num() == 3 ? FVector(FCString::Atof(*P[0]), FCString::Atof(*P[1]), FCString::Atof(*P[2])) : FVector::ZeroVector;
	Nudge.Z += StaffSlide();   // (the walking-staff hold: the model slides through the fist, KnightAnimatorCast.cpp places the arm for it)
	const float Glow = FMath::Max(0.f, CVarOrbGlow.GetValueOnGameThread()), Lock = FMath::Clamp(CVarOrbLock.GetValueOnGameThread(), 0.f, 1.f);
	for (UStaticMeshComponent* S : HeldStatics)
	{
		if (!S) continue;
		for (int32 i = 0; i < S->GetNumMaterials(); ++i)
			if (UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(S->GetMaterial(i)))   // (a MID ignores an unchanged value)
			{
				M->SetScalarParameterValue(TEXT("GlowStrength"), Glow);
				M->SetScalarParameterValue(TEXT("ExposureLock"), Lock);
			}
		if (S->GetAttachParent() == WristR) S->SetRelativeTransform(FTransform(Nudge) * JeebProps::FistPlace());   // (in the hand; stowed it hangs on UpperPivot)
	}
}

// Everything he carries goes where the pose puts it. WristR is the weapon's joint (the blade, its trail and its hit boxes), ElbowL the left hand's,
// UpperPivot the chest: the cape and whatever is on his back
void AKnightCharacter::PlaceGear()
{
	if (WristR) WristR->SetWorldTransform(Anim->RightGripWorld());
	if (ElbowL) ElbowL->SetWorldTransform(Anim->LeftGripWorld());
	PlaceWizardOffHand();
	if (UpperPivot) UpperPivot->SetWorldTransform(Anim->ChestWorld());
	const bool bWantBack = bStowed || ((bSwinging || bFishing) && !bBlocking);   // (both hands on a tool, or fishing: the shield goes on his back, unless he raises it)
	if (bWantBack != bShieldBack) { bShieldBack = bWantBack; PlaceShield(bShieldBack); }
	if (bStowed) PullStowed(Anim->StowPull());   // (bent flat over a ledge: in toward his back)
	if (!bShieldBack)
	{
		FTransform At[3];
		for (int32 s = 0; s < 3; ++s) At[s] = Anim->ShieldWorld(BlockAlpha, s);   // (each style's model rests its own back on the forearm)
		for (FArmorPart& P : ArmorParts)
			if (P.Slot == KnightDesign::SlotShield && P.Comp) P.Comp->SetWorldTransform(At[FMath::Clamp(P.Style, 0, 2)]);
#if !UE_BUILD_SHIPPING
		// (test pictures, -ShotShieldGap: the least gap from the worn shield model's back to his left forearm's real mesh, the least over each half second; under 0 sinks in)
		static const bool bGapLog = FParse::Param(FCommandLine::Get(), TEXT("ShotShieldGap"));
		if (bGapLog && bShotFace && !IsNetMode(NM_DedicatedServer))
			for (const FArmorPart& P : ArmorParts)
				if (P.Model && P.Model->IsVisible())
				{
					static float GapMin = 1e4f, GapT = 0.f, UpMin = 2.f, UpMax = -1.f; static FVector GapAt; static int32 GapArm = 0;
					FVector Where; int32 Arm = 0;
					const float G = Anim->ShieldGap(P.Model, Where, Arm);
					if (G < GapMin) { GapMin = G; GapAt = Where; GapArm = Arm; }
					if (BlockAlpha > 0.f && BlockAlpha < 1.f)   // (swinging up or down: every frame)
						UE_LOG(LogTemp, Display, TEXT("JEEBSHIELDSWING style %d block %.2f gap %.2f cm at (%.1f %.1f %.1f)"), P.Style, BlockAlpha, G, Where.X, Where.Y, Where.Z);
					const float U = Anim->ShieldUpShare();
					UpMin = FMath::Min(UpMin, U); UpMax = FMath::Max(UpMax, U);
					if ((GapT += GetWorld()->GetDeltaSeconds()) >= 0.5f)
					{
						UE_LOG(LogTemp, Display, TEXT("JEEBSHIELDGAP t %.1f style %d up %.2f-%.2f block %.2f speed %.0f: gap %.2f cm at (%.1f %.1f %.1f) in the model, %d arm points under it"),
							GetWorld()->GetTimeSeconds(), P.Style, UpMin, UpMax, BlockAlpha, GetVelocity().Size2D(), GapMin, GapAt.X, GapAt.Y, GapAt.Z, GapArm);
						GapMin = 1e4f; GapT = 0.f; UpMin = 2.f; UpMax = -1.f;
					}
				}
#endif
	}
	if (BowHold) BowHold->SetWorldTransform(SelectedKind() == JeebItems::K_Crossbow ? Anim->CrossWorld() : Anim->BowWorld(BowAimAlpha));
	if (IsNetMode(NM_DedicatedServer)) return;
	if (DrinkGroup && bDrinking)
	{
		DrinkGroup->SetWorldTransform(Anim->DrinkWorld());
		if (bShotFace) UE_LOG(LogTemp, Log, TEXT("JEEBDRINK u %.2f mouth off the bottle's line %.1f cm, cork %.1f cm from the mouth"), DrinkT / FMath::Max(JeebTune::V().DrinkTime, 0.1f), FMath::PointDistToLine(Anim->MouthWorld(), -DrinkGroup->GetComponentQuat().GetAxisZ(), DrinkGroup->GetComponentLocation()), float(FVector::Dist(Anim->MouthWorld(), DrinkGroup->GetComponentLocation() - DrinkGroup->GetComponentQuat().GetAxisZ() * 15.f)));
	}
	if (bShotFace && bSwinging && ActiveNode.IsValid())
	{
		const FVector Head = Anim->ToolHeadWorld(SelectedKind());
		{
			float OffHaft = 0.f, FromButt = 0.f;
			Anim->ToolHands(OffHaft, FromButt);
			UE_LOG(LogTemp, Log, TEXT("JEEBHANDS u %.2f left fist off the haft %.1f cm, %.1f cm up from the butt"), FMath::Fmod(SwingTime, FMath::Max(SwingPeriod, 0.1f)) / FMath::Max(SwingPeriod, 0.1f), OffHaft, FromButt);
		}
		const float Feet = GetActorLocation().Z - GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		UE_LOG(LogTemp, Log, TEXT("JEEBCONTACT swingT %.2f u %.2f head off the surface %.1f cm, head height %.0f, surface %.0f, from spot %.1f, kind %d"), SwingTime, FMath::Fmod(SwingTime, FMath::Max(SwingPeriod, 0.1f)) / FMath::Max(SwingPeriod, 0.1f), FVector::Dist2D(Head, ActiveNode->GetActorLocation()) - ActiveNode->SurfaceAlong(-GetActorForwardVector(), Head.Z - ActiveNode->GetActorLocation().Z), Head.Z - Feet, ActiveNode->SurfaceAlong(-GetActorForwardVector(), Head.Z - ActiveNode->GetActorLocation().Z), FVector::Dist2D(GatherSpot, GetActorLocation()), SelectedKind());
	}
	if (SelectedKind() == JeebItems::K_Staff) PlaceStaffModel();
	UpdateBowBits();
	UpdateQuiver();
	UpdateRockSack();   // (the troll's sack of rocks: KnightCharacterRock.cpp)
	UpdateBeltFlask();
	if (QuiverParts.Num() > 0 && QuiverParts[0])
		if (USceneComponent* Q = QuiverParts[0]->GetAttachParent()) Q->SetWorldTransform(Anim->QuiverWorld());
	UpdateRings();
	UpdateAmulet();   // (the chain is measured from his neck, so once the pose has been placed)
}

// ---------------- Test pictures ----------------
// -SuitWalk=<direction>_<share>_<from>_<for>: walks that way (0 = where he faces, 90 = his right), at that share of his top speed, starting <from> seconds in and
// lasting <for>. -SuitSprint sprints. -SuitDo=roll<deg>, stun<hundredths>, knock, drop, crouch: done to him once the pictures have started.
#if !UE_BUILD_SHIPPING
// (2026-11-15: set per -ShotEyes eye with -ShotEyesSet, so one run has him standing, walking and running)
static TAutoConsoleVariable<float> CVarTestWalk(TEXT("jeeb.Test.Walk"), 0.f, TEXT("Test pictures only: he moves the way he faces at this share of full speed (1 runs), 2 sprints, 0 stands."), ECVF_Default);
static TAutoConsoleVariable<int32> CVarTestBlock(TEXT("jeeb.Test.Block"), 0, TEXT("Test pictures only: 1 holds the shield up, 0 lowers it."), ECVF_Default);
#endif
void AKnightCharacter::TickSuitTests(float Dt)
{
	if (!IsLocallyControlled() || GetNetMode() != NM_Standalone) return;
#if !UE_BUILD_SHIPPING
	if (bShotFace)
	{
		static bool bTestSprint = false;
		const float W = CVarTestWalk.GetValueOnGameThread();
		if (W > 1.5f && !bTestSprint) { bTestSprint = true; StartSprint(); }
		if (W <= 1.5f && bTestSprint) { bTestSprint = false; StopSprint(); }
		if (W > 0.f) AddMovementInput(GetActorForwardVector(), FMath::Min(W, 1.f));
		// (2026-11-17: jeeb.Test.Block 1 raises the shield, 0 lowers it again; set per eye with -ShotEyesSet)
		static int32 TestBlockWas = 0;
		const int32 Bl = CVarTestBlock.GetValueOnGameThread();
		if (Bl != TestBlockWas) { TestBlockWas = Bl; if (Bl) StartBlock(); else StopBlock(); }
	}
#endif
	{
		static float TurnRate = 0.f;   // (-ShotTurn=200: the camera swings round at that many degrees a second, to check what he does in a move; log JEEBTURN)
		static const bool bTurnTest = FParse::Value(FCommandLine::Get(), TEXT("ShotTurn="), TurnRate);
		if (bTurnTest && bShotFace && GetController())
		{
			FRotator R = GetController()->GetControlRotation();
			R.Yaw += TurnRate * Dt;
			GetController()->SetControlRotation(R);
			UE_LOG(LogTemp, Log, TEXT("JEEBTURN attacking %d style %d yaw %.1f camera %.1f"), bAttacking ? 1 : 0, int32(AtkStyle), GetActorRotation().Yaw, R.Yaw);
		}
	}
	static FString Walk;
	{
		static bool bLabAt = false;   // (-LabAt=<x>_<y>: starts him there, for a look at what stands elsewhere in MoveLab)
		if (bShotFace && !bLabAt)
		{
			bLabAt = true;
			FString S; TArray<FString> Pr;
			if (FParse::Value(FCommandLine::Get(), TEXT("LabAt="), S) && S.ParseIntoArray(Pr, TEXT("_")) == 2) { const float LX = FCString::Atof(*Pr[0]), LY = FCString::Atof(*Pr[1]); SetActorLocation(FVector(LX, LY, FMath::Max(GetActorLocation().Z, JeebTerrain::HeightAt(LX, LY) + GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.f))); }
		}
	}
	static FString Do;
	static bool bRead = false;
	if (!bRead)
	{
		FParse::Value(FCommandLine::Get(), TEXT("SuitWalk="), Walk);
		FParse::Value(FCommandLine::Get(), TEXT("SuitDo="), Do);
		bRead = true;
	}
	if (!Walk.IsEmpty())
	{
		TArray<FString> P;
		if (Walk.ParseIntoArray(P, TEXT("_")) == 4)
		{
			SuitTestT += Dt;
			const float From = FCString::Atof(*P[2]), For = FCString::Atof(*P[3]);
			if (SuitTestT < From || SuitTestT > From + For) { if (bSprinting) StopSprint(); }
			else
			{
				if (FParse::Param(FCommandLine::Get(), TEXT("SuitSprint")) && !bSprinting) StartSprint();
				AddMovementInput(FRotator(0.f, GetActorRotation().Yaw + FCString::Atof(*P[0]), 0.f).Vector(), FCString::Atof(*P[1]));
			}
		}
	}
	if (Do.IsEmpty() || bSuitTestActed || !bShotFace) return;
	if (Do.StartsWith(TEXT("hopat")))   // (hopat60: a hop 0.6 s into whatever else the test is doing, such as drawing the bow)
	{
		static float Waited = 0.f;
		Waited += Dt;
		if (Waited < FCString::Atoi(*Do.Mid(5)) / 100.f) return;
		bSuitTestActed = true;
		Jump();
		ShotJumpFrom = GetActorLocation().Z;
		ShotJumpT = GetWorld()->GetTimeSeconds();
		FTimerHandle T;
		GetWorldTimerManager().SetTimer(T, FTimerDelegate::CreateWeakLambda(this, [this]() { StopJumping(); }), 0.2f, false);
		return;
	}
	bSuitTestActed = true;
	if (TrollSuitTest(Do)) return;   // (the troll's: punch, hop, chargejump, leapsizes, leaptest, hopall: KnightCharacterTroll.cpp)
	if (Do.StartsWith(TEXT("roll")))
	{
		// (StartRoll refuses during test pictures, against stray keys: this is its body)
		RollDir = FRotator(0.f, GetActorRotation().Yaw + FCString::Atof(*Do.Mid(4)), 0.f).Vector();
		RollT = 0.f;
		bSavedOrient = GetCharacterMovement()->bOrientRotationToMovement;
		GetCharacterMovement()->bOrientRotationToMovement = false;
		bRolling = true;
	}
	else if (Do == TEXT("crouch")) bCrouched = true;
	else if (Do.StartsWith(TEXT("stun"))) ApplyReact(FCString::Atoi(*Do.Mid(4)) / 100.f, -GetActorForwardVector() * 300.f, false);
	else if (Do == TEXT("knock"))
	{
		LaunchCharacter(-GetActorForwardVector() * 300.f, true, false);
		GetCharacterMovement()->Velocity = -GetActorForwardVector() * 300.f;
		MulticastKnock_Implementation(true, 0);
	}
	else if (Do == TEXT("drop")) MulticastKnock_Implementation(true, 1);
	else if (Do == TEXT("chopnode") || Do == TEXT("minenode"))   // (the real thing: right in front of the MoveLab's tree or rock, then the click)
	{
		const bool bTree = Do == TEXT("chopnode");
		AActor* Best = nullptr;
		static const bool bOakOnly = FParse::Param(FCommandLine::Get(), TEXT("ChopOak"));   // (-ChopOak: the nearest oak, not a pine that may need a higher level)
		for (TActorIterator<AResourceNode> It(GetWorld()); It; ++It)
			if ((bTree ? (It->IsA<AChoppableTree>() && (!bOakOnly || Cast<AChoppableTree>(*It)->GetKind() == ETreeKind::Oak)) : It->IsA<AMiningRock>()) && (!Best || FVector::Dist2D(It->GetActorLocation(), GetActorLocation()) < FVector::Dist2D(Best->GetActorLocation(), GetActorLocation()))) Best = *It;
		if (Best)
		{
			const FVector Dir = FRotator(0.f, ShotFaceYaw, 0.f).Vector();
			float FromD = bTree ? 125.f : 130.f, FromS = 0.f;   // (-GatherFrom=<distance>_<sideways>: where he clicks from, to see him walk into place)
			FString GF; TArray<FString> GP;
			if (FParse::Value(FCommandLine::Get(), TEXT("GatherFrom="), GF) && GF.ParseIntoArray(GP, TEXT("_")) == 2) { FromD = FCString::Atof(*GP[0]); FromS = FCString::Atof(*GP[1]); }
			FVector At = Best->GetActorLocation() - Dir * FromD + FVector::CrossProduct(FVector::UpVector, Dir) * FromS;
			At.Z = FMath::Max(GetActorLocation().Z - 300.f, JeebTerrain::HeightAt(At.X, At.Y) + GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 1.f);   // (on the ground there: a slope must not leave him falling, which ends the work)
			SetActorLocation(At);
			GetCharacterMovement()->SetMovementMode(MOVE_Walking);
			SetActorRotation(FRotator(0.f, ShotFaceYaw, 0.f));
			Interact();
		}
		UE_LOG(LogTemp, Log, TEXT("JEEBGATHER %s node %s swinging %d kind %d"), *Do, Best ? *Best->GetName() : TEXT("none"), bSwinging ? 1 : 0, SelectedKind());
	}
	else if (Do == TEXT("fishnode"))   // (the real thing, at MoveLab's pond)
	{
		SetActorRotation(FRotator(0.f, ShotFaceYaw, 0.f));
		Interact();
		UE_LOG(LogTemp, Log, TEXT("JEEBGATHER fishnode node %s fishing %d kind %d"), ActiveNode.IsValid() ? *ActiveNode->GetName() : TEXT("none"), bFishing ? 1 : 0, SelectedKind());
	}
	else if (Do == TEXT("chop")) { bSwinging = true; bSideways = true; SwingPeriod = 1.25f; }
	else if (Do == TEXT("mine")) { bSwinging = true; bSideways = false; SwingPeriod = 1.f; }
	else if (Do == TEXT("fish")) bFishing = true;
	else if (Do == TEXT("sit")) bSitting = true;
	else if (Do == TEXT("slide")) BeginSlide(GetActorForwardVector(), 1000.f);
	else if (Do == TEXT("drink"))
	{
		if (Skills->PotionSlot.Id == 0) { Skills->PotionSlot.Id = JeebItems::PotionId(JeebItems::P_Heal); Skills->PotionSlot.Count = 3; }
		MulticastDrink(uint8(JeebItems::PotionType(Skills->PotionSlot.Id)), true);
	}
	else if (Do == TEXT("climb")) bClimbing = true;
}

// Feet in the snow (2026-11-22, owner: "it looks like the knights feet sink into the snow instead of standing perfectly on top of it").
// Big games deform the snow itself (Red Dead 2's tessellated snow, render-target trails); a cheap version that costs nothing online:
// the drawn body (and his hit boxes with it, by a few cm, the same on the server) stands up to JeebOpen::SinkDepth lower on deep snow, so
// the boots go in to the ankle, and on players' machines he leaves soft footprints (decals, M_SnowPrint) that fade after half a minute.
// Worked out from his position alone, so every machine draws every knight alike with nothing sent.
void AKnightCharacter::SnowFeet(float Dt, FVector& Feet)
{
	const bool bGround = GetCharacterMovement() && GetCharacterMovement()->IsMovingOnGround();
	const float Want = bGround && !bMounted ? JeebOpen::SnowSink(Feet) : 0.f;
	SnowSinkNow = FMath::FInterpTo(SnowSinkNow, Want, Dt, 6.f);
	Feet.Z -= SnowSinkNow;
	if (IsNetMode(NM_DedicatedServer) || SnowSinkNow < 3.f) { PrintLast = Feet; return; }
	static TWeakObjectPtr<UMaterialInterface> PrintMat;
	static bool bTried = false;
	if (!bTried) { bTried = true; PrintMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_SnowPrint.M_SnowPrint"), nullptr, LOAD_Quiet | LOAD_NoWarn); }
	if (!PrintMat.IsValid()) return;
	const FVector2D Step(Feet.X - PrintLast.X, Feet.Y - PrintLast.Y);
	if (Step.SizeSquared() < 70.f * 70.f) return;
	if (Step.SizeSquared() > 400.f * 400.f) { PrintLast = Feet; return; }   // (a teleport or a respawn: no trail across)
	if (!bPrintNoDecals)
	{   // (the prints are for the snow only: never on him)
		bPrintNoDecals = true;
		TArray<UPrimitiveComponent*> Prims; GetComponents<UPrimitiveComponent>(Prims, true);
		for (UPrimitiveComponent* P : Prims) { P->bReceivesDecals = false; P->MarkRenderStateDirty(); }
	}
	const FVector2D Dir = Step.GetSafeNormal();
	const FVector2D Side(-Dir.Y, Dir.X);
	PrintSide ^= 1;
	const FVector2D At = FVector2D(Feet.X, Feet.Y) + Side * (PrintSide ? 11.f : -11.f);
	const float Z = JeebTerrain::DrawnHeight(At.X, At.Y);
	const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X));
	// (a decal projects along its X: pitched straight down; its box 30 cm deep so it lands on the slope and nothing else)
	if (UDecalComponent* D = UGameplayStatics::SpawnDecalAtLocation(this, PrintMat.Get(), FVector(15.f, 7.f, 13.f), FVector(At.X, At.Y, Z), FRotator(-90.f, Yaw, 0.f), 30.f))
	{
		D->SetFadeOut(18.f, 12.f, false);
		D->SetFadeScreenSize(0.002f);
	}
	PrintLast = Feet;
}

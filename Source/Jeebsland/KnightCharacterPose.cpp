#include "KnightCharacter.h"
#include "KnightShieldRest.h"
#include "JeebSave.h"
#include "Net/UnrealNetwork.h"
#include "Messages.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "InputModifiers.h"
#include "EnhancedInputComponent.h"
#include "SkillComponent.h"
#include "ResourceNode.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Terrain.h"
#include "GrassField.h"
#include "Canopy.h"
#include "Pig.h"
#include "Inventory.h"
#include "Npc.h"
#include "Horse.h"
#include "Cottage.h"
#include "Loot.h"
#include "Pond.h"
#include "HealthComponent.h"
#include "Projectile.h"
#include "Combat.h"
#include "WeaponDesign.h"
#include "ProceduralMeshComponent.h"
#include "Misc/ConfigCacheIni.h"

// The knight's pose, every frame (the last big part of Tick): no game rules here, only where each joint points.
//   PosePrep   landing squash, the eased blends (walk/run, air, crouch, shield ...), the stride cycle, how strong each motion is
//   PoseLegs   hips, knees and ankles
//   PoseBody   the body's rise and fall, the chest, the pelvis and the head
//   PoseCape   cape and plume
//   PoseArms   both arms: swinging, the shield, attacks, fishing, gathering
// The values one step works out for the next ones travel in FKnightPose.
struct FKnightPose
{
	float Dt = 0.f, Speed = 0.f;
	FVector Vel = FVector::ZeroVector;
	bool bFalling = false, bFaceCam = false;
	// from PosePrep
	float Squash = 0.f, CS = 0.f, Air = 0.f, Ground = 0.f, Duck = 0.f, DuckC = 0.f, DuckHip = 0.f, DuckKnee = 0.f, DuckLean = 0.f, Stream = 0.f;
	float GContact = 0.f, GAf = 0.f, GAb = 0.f, LegReach = 0.f, Hz = 0.f, UR = 0.f, WR = 0.f, WL = 0.f, S = 0.f, C = 0.f;
	float GHipR = 0.f, GKneeR = 0.f, GFollowR = 0.f, GToeR = 0.f, GHipL = 0.f, GKneeL = 0.f, GFollowL = 0.f, GToeL = 0.f;
	float ArmAmp = 0.f, ElbowBase = 0.f, ElbowSwing = 0.f, Lean = 0.f, Twist = 0.f, F01 = 0.f, Asym = 0.f;
	// from PoseLegs
	float HipRf = 0.f, HipLf = 0.f, KneeRf = 0.f, KneeLf = 0.f, IdleAmt = 0.f, BreathS = 0.f;
	// from PoseBody
	float TurnRoll = 0.f;
};

void AKnightCharacter::TickPose(float Dt, const FVector& Vel, float Speed, bool bFalling, bool bFaceCam, float& OutS, float& OutC)
{
	FKnightPose P;
	P.Dt = Dt; P.Vel = Vel; P.Speed = Speed; P.bFalling = bFalling; P.bFaceCam = bFaceCam;
	PosePrep(P);
	PoseLegs(P);
	PoseBody(P);
	PoseCape(P);
	PoseArms(P);
	OutS = P.S; OutC = P.C;
}

void AKnightCharacter::PosePrep(FKnightPose& P)
{
	const float Dt = P.Dt, Speed = P.Speed;
	const FVector Vel = P.Vel;
	const bool bFalling = P.bFalling;

	// ---------- Landing detection ----------
	if (bFalling)
	{
		if (!bWasFalling) LastFallVz = 0.f;
		LastFallVz = FMath::Min(LastFallVz, Vel.Z);
	}
	else if (bWasFalling)
	{
		LandImpact = FMath::Clamp(-LastFallVz / 900.f, 0.3f, 1.f); // harder landings squash more
		LandTimer = LandDuration;
	}
	bWasFalling = bFalling;
	LandTimer = FMath::Max(0.f, LandTimer - Dt);
	const float Squash = LandImpact * (LandTimer / LandDuration);

	// ---------- Blends (everything eases in and out) ----------
	MoveBlend = FMath::FInterpTo(MoveBlend, FMath::Clamp(Speed / 150.f, 0.f, 1.f), Dt, 10.f);
	RunAlpha  = FMath::FInterpTo(RunAlpha, FMath::Clamp((Speed - 250.f) / 500.f, 0.f, 1.f), Dt, 6.f); // 0 = walk, 1 = full sprint
	AirAlpha  = FMath::FInterpTo(AirAlpha, bFalling ? 1.f : 0.f, Dt, 14.f);
	CrouchAlpha = FMath::FInterpTo(CrouchAlpha, bCrouched ? 1.f : 0.f, Dt, 10.f);
	BlockAlpha  = FMath::FInterpTo(BlockAlpha, bBlocking ? 1.f : 0.f, Dt, 14.f);
	SlideAlpha  = FMath::FInterpTo(SlideAlpha, bSliding ? 1.f : 0.f, Dt, 12.f);
	SitAlpha    = FMath::FInterpTo(SitAlpha, bSitting ? 1.f : 0.f, Dt, 8.f);
	MountAlpha  = FMath::FInterpTo(MountAlpha, bMounted ? 1.f : 0.f, Dt, 8.f);
	TuckAlpha   = FMath::FInterpTo(TuckAlpha, bRolling ? 1.f : 0.f, Dt, 18.f);
	ClimbAlpha  = FMath::FInterpTo(ClimbAlpha, bClimbing ? 1.f : 0.f, Dt, 12.f);
	const float CS = FMath::Sin(ClimbPhase);
	const float Air = AirAlpha;
	const float Ground = 1.f - Air;
	const float Duck = FMath::Max(CrouchAlpha, BlockAlpha) * Ground; // how low the knight is sitting
	// Crouching (Ctrl) is a deep squat, thighs about level and knees well forward like a person squatting low; the shield
	// guard keeps its own shallower crouch. Both can be on at once (the deeper legs win; the shield arm still comes up).
	const float DuckC = CrouchAlpha * Ground, DuckB = BlockAlpha * Ground;
	const float DuckHip = FMath::Max(55.f * DuckB, 100.f * DuckC);
	const float DuckKnee = FMath::Min(-95.f * DuckB, -140.f * DuckC);
	const float DuckLean = FMath::Max(14.f * DuckB, 24.f * DuckC);   // a person squatting low leans forward to keep balance
	const float Stream = RunAlpha * MoveBlend; // how hard the wind pushes the cape and plume

	// ---------- Gait: a real stride (rewritten 2026-10-30) ----------
	// Each leg runs its own cycle U (0 to 1; the left half a cycle behind the right) in two parts, like a person:
	//  CONTACT: the foot is planted while the hip sweeps from reaching forward (Af) to trailing behind (Ab). The hip moves at
	//    exactly the body's speed, so a planted foot never slides; the standing knee gives a little mid-step.
	//  SWING: the foot lifts, the knee folds (most early in the swing), and the leg swings forward to land nearly straight.
	// Walking has a long contact (both feet down for a moment) and small swings; jogging (the normal speed) and sprinting
	// have a short contact and a moment in the air. The clock runs on the ground covered, not on time.
	const float JogT = FMath::Clamp((Speed - 150.f) / 250.f, 0.f, 1.f);   // walk ... jog (400, the normal speed)
	const float SprT = FMath::Clamp((Speed - 400.f) / 350.f, 0.f, 1.f);   // jog ... sprint (750)
	auto G3 = [&](float W, float J, float Sp) { return SprT > 0.f ? FMath::Lerp(J, Sp, SprT) : FMath::Lerp(W, J, JogT); };
	const float StepScale   = 1.f - 0.35f * CrouchAlpha;   // smaller steps when crouched
	const float GContact    = G3(0.60f, 0.26f, 0.21f);     // share of the cycle each foot is planted (short when running: a long float between footfalls, like BotW)
	const float GAf         = G3(32.f, 40.f, 52.f) * StepScale;   // hip reaching forward at the start of contact
	const float GAb         = G3(22.f, 32.f, 40.f) * StepScale;   // hip trailing behind at toe-off
	const float GStanceKnee = G3(8.f, 26.f, 32.f);         // how much the standing knee gives
	const float GSwingKnee  = G3(42.f, 50.f, 95.f) * StepScale;   // how far the swinging knee folds (normal speed: about half the sprint, the heels stay low)
	const float LegReach    = float(KnightDesign::ThighLen() + KnightDesign::ShinLen());
	const float SinAf = FMath::Sin(FMath::DegreesToRadians(GAf)), SinAb = FMath::Sin(FMath::DegreesToRadians(GAb));
	const float Sweep = LegReach * (SinAf + SinAb);        // ground a planted foot covers
	const float Hz = FMath::Clamp(GContact * Speed / FMath::Max(Sweep, 1.f), 0.5f, 3.4f);
	GaitPhase = FMath::Fmod(GaitPhase + 2.f * PI * Hz * Dt, 2.f * PI);
	IdleTime += Dt;
	FlutterTime += Dt * (1.f + 1.5f * Stream);
	// Every hand-over flows (2026-10-30, it looked rigid where one foot takes over from the other): the swing starts with
	// the leg still moving back and ends with it already drawing back to land (part of the planted sweep's speed carried
	// through), the standing knee eases in and out of its give, the foot rolls from heel-up straight into its swing, and
	// each foot's hold on the body (Weight) fades in and out instead of switching.
	const float SweepRate = FMath::RadiansToDegrees(SinAf + SinAb) * (1.f - GContact) / FMath::Max(GContact, 0.05f);   // planted hip speed, per unit of swing
	const float CarryEnd = -0.3f * SweepRate / FMath::Cos(FMath::DegreesToRadians(GAb));
	const float CarryStart = -0.3f * SweepRate / FMath::Cos(FMath::DegreesToRadians(GAf));
	auto LegCycle = [&](float U, float& Hip, float& Knee, float& Weight, float& Follow, float& ToeOff)
	{
		if (U < GContact)
		{
			const float T = U / GContact;
			Hip = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(SinAf - (SinAf + SinAb) * T, -1.f, 1.f)));
			Knee = -4.f - GStanceKnee * FMath::Square(FMath::Sin(PI * T));
			Weight = FMath::SmoothStep(0.f, 0.18f, T) * (1.f - FMath::SmoothStep(0.82f, 1.f, T));
			Follow = 0.f; ToeOff = FMath::SmoothStep(0.65f, 1.f, T);
		}
		else
		{
			const float T = (U - GContact) / (1.f - GContact);
			const float T2 = T * T, T3 = T2 * T;   // Hermite: from trailing (-Ab) to reaching (+Af), carrying the sweep's speed
			Hip = (2.f * T3 - 3.f * T2 + 1.f) * -GAb + (T3 - 2.f * T2 + T) * CarryEnd + (-2.f * T3 + 3.f * T2) * GAf + (T3 - T2) * CarryStart;
			Knee = -4.f - GSwingKnee * FMath::Square(FMath::Sin(PI * FMath::Pow(T, 0.75f)));
			Weight = 0.f;
			const float Roll = FMath::SmoothStep(0.f, 0.3f, T);   // heel-up push-off rolling into the trailing swing foot
			Follow = Roll * (1.f - FMath::SmoothStep(0.5f, 0.95f, T)); ToeOff = 1.f - Roll;
		}
	};
	const float UR = GaitPhase / (2.f * PI), UL = FMath::Fmod(UR + 0.5f, 1.f);
	float GHipR, GKneeR, GFollowR, GToeR, GHipL, GKneeL, GFollowL, GToeL, WR, WL;
	LegCycle(UR, GHipR, GKneeR, WR, GFollowR, GToeR);
	LegCycle(UL, GHipL, GKneeL, WL, GFollowL, GToeL);
	{
		const float Still = 1.f - FMath::SmoothStep(0.15f, 0.45f, MoveBlend);   // standing: both feet hold him
		WR = FMath::Lerp(WR, 1.f, Still); WL = FMath::Lerp(WL, 1.f, Still);
	}
	const float S = FMath::Clamp(GHipR / FMath::Max(GAf, 1.f), -1.f, 1.f);   // right leg forward when positive (for the arms and hips)
	const float C = FMath::Cos(GaitPhase);

	// ---------- Strength of each motion ----------
	const float ArmAmp     = FMath::Lerp(22.f, 55.f, RunAlpha) * MoveBlend;
	const float ElbowBase  = FMath::Lerp(12.f, 70.f, RunAlpha) * MoveBlend;
	const float ElbowSwing = FMath::Lerp(8.f, 25.f, RunAlpha) * MoveBlend;
	const float Lean       = FMath::Lerp(3.f, 20.f, RunAlpha) * MoveBlend;
	const float Twist      = FMath::Lerp(5.f, 12.f, RunAlpha) * MoveBlend;

	// ---------- Jump pose: where are we in the arc? 0 = rising fast ... 1 = falling fast ----------
	const float F01 = FMath::Clamp(0.5f - Vel.Z / 1000.f, 0.f, 1.f);
	const float Asym = FMath::Clamp(Speed / 250.f, 0.f, 1.f); // running leap (one knee up) vs standing jump (both tucked)

	P.Squash = Squash; P.CS = CS; P.Air = Air; P.Ground = Ground; P.Duck = Duck; P.DuckC = DuckC; P.DuckHip = DuckHip; P.DuckKnee = DuckKnee; P.DuckLean = DuckLean; P.Stream = Stream;
	P.GContact = GContact; P.GAf = GAf; P.GAb = GAb; P.LegReach = LegReach; P.Hz = Hz; P.UR = UR; P.WR = WR; P.WL = WL; P.S = S; P.C = C;
	P.GHipR = GHipR; P.GKneeR = GKneeR; P.GFollowR = GFollowR; P.GToeR = GToeR; P.GHipL = GHipL; P.GKneeL = GKneeL; P.GFollowL = GFollowL; P.GToeL = GToeL;
	P.ArmAmp = ArmAmp; P.ElbowBase = ElbowBase; P.ElbowSwing = ElbowSwing; P.Lean = Lean; P.Twist = Twist; P.F01 = F01; P.Asym = Asym;
}

void AKnightCharacter::PoseLegs(FKnightPose& P)
{
	const float Dt = P.Dt;
	const FVector Vel = P.Vel;
	const bool bFaceCam = P.bFaceCam;
	const float Ground = P.Ground, S = P.S, Asym = P.Asym, F01 = P.F01, Air = P.Air, Squash = P.Squash, DuckHip = P.DuckHip, DuckKnee = P.DuckKnee, Duck = P.Duck, CS = P.CS;
	const float GHipR = P.GHipR, GHipL = P.GHipL, GKneeR = P.GKneeR, GKneeL = P.GKneeL, GFollowR = P.GFollowR, GToeR = P.GToeR, GFollowL = P.GFollowL, GToeL = P.GToeL;

	// ---------- Legs ----------
	// On the ground: legs swing opposite each other; the knee bends most as a leg passes under the body
	// (smooth everywhere: no sharp corners in the curve, so no jerks)
	// Shield-up stance: the legs step the way you are actually moving. Backing up runs the stride in reverse; a sidestep
	// swings the legs out to the side instead (the leading leg reaches out, the other closes up after it).
	{
		const FVector LV = GetActorRotation().UnrotateVector(FVector(Vel.X, Vel.Y, 0.f));
		float Fw = 1.f, Sd = 0.f;
		if ((bBlockHeld || bStrafeMode) && !bMounted && LV.Size() > 30.f) { Fw = LV.X / LV.Size(); Sd = LV.Y / LV.Size(); }
		StepFw = FMath::FInterpTo(StepFw, Fw, Dt, 8.f);
		StepSd = FMath::FInterpTo(StepSd, Sd, Dt, 8.f);
	}
	// Facing the camera (strafe mode, shield stance), like Fortnite: the shoulders and back stay square to the camera while the
	// hips and legs turn up to 45 degrees toward the way you are going (backing up, toward the way straight behind) and run
	// that way; whatever is left over (moving straight sideways) is a sidestep. The chest turns back by the same amount.
	float LegFw = StepFw, LegSd = StepSd;
	{
		float WantYaw = 0.f;
		if (bFaceCam && (FMath::Abs(StepFw) + FMath::Abs(StepSd)) > 0.2f)
		{
			const float Ang = FMath::RadiansToDegrees(FMath::Atan2(StepSd, StepFw));   // 0 ahead, +90 right, 180 behind
			const float Toward = FMath::Abs(Ang) <= 90.f ? Ang : Ang - 180.f * FMath::Sign(Ang);
			WantYaw = FMath::Clamp(Toward, -45.f, 45.f);
		}
		LegYaw = FMath::FInterpTo(LegYaw, WantYaw * MoveBlend, Dt, 8.f);
		const float Rel = FMath::DegreesToRadians(FMath::RadiansToDegrees(FMath::Atan2(StepSd, StepFw)) - LegYaw);
		const float Mag = FMath::Min(1.f, FMath::Sqrt(StepFw * StepFw + StepSd * StepSd));
		if (bFaceCam) { LegFw = FMath::Cos(Rel) * Mag; LegSd = FMath::Sin(Rel) * Mag; }
	}
	const float HipRg = GHipR * MoveBlend * LegFw;
	const float HipLg = GHipL * MoveBlend * LegFw;
	const float SideAmp = 24.f * MoveBlend * Ground * (1.f - SlideAlpha) * (1.f - SitAlpha) * (1.f - MountAlpha) * (1.f - ClimbAlpha) * (1.f - TuckAlpha);
	const float HipRroll = SideAmp * LegSd * (0.5f + 0.5f * S);   // positive roll swings a hanging leg to the right
	const float HipLroll = SideAmp * LegSd * (0.5f - 0.5f * S);
	const float KneeRg = GKneeR * MoveBlend;
	const float KneeLg = GKneeL * MoveBlend;
	// In the air: knees tuck on the way up, legs reach forward on the way down
	const float HipRa  = FMath::Lerp(FMath::Lerp(30.f, 60.f, Asym), FMath::Lerp(15.f, 35.f, Asym), F01);
	const float HipLa  = FMath::Lerp(FMath::Lerp(30.f, -15.f, Asym), FMath::Lerp(15.f, 0.f, Asym), F01);
	const float KneeRa = -FMath::Lerp(FMath::Lerp(55.f, 85.f, Asym), 25.f, F01);
	const float KneeLa = -FMath::Lerp(FMath::Lerp(55.f, 40.f, Asym), 20.f, F01);

	float HipRf  = FMath::Lerp(HipRg, HipRa, Air) + 35.f * Squash + DuckHip;
	float HipLf  = FMath::Lerp(HipLg, HipLa, Air) + 35.f * Squash + DuckHip;
	float KneeRf = FMath::Lerp(KneeRg, KneeRa, Air) - 70.f * Squash + DuckKnee;
	float KneeLf = FMath::Lerp(KneeLg, KneeLa, Air) - 70.f * Squash + DuckKnee;
	// Standing still: slow breathing (the chest rises, the knees ease a little), like the idle in Breath of the Wild
	const float IdleAmt = (1.f - MoveBlend) * Ground * (1.f - SlideAlpha) * (1.f - SitAlpha) * (1.f - MountAlpha) * (1.f - ClimbAlpha) * (1.f - TuckAlpha);
	const float BreathS = FMath::Sin(IdleTime * 1.65f);   // one breath every 3.8 s
	const float Stance = StanceHip - 1.5f * BreathS * IdleAmt;
	// A relaxed stance: on the ground the knees are always a little bent (eased out as he crouches, which bends them anyway)
	const float Relax = Ground * (1.f - Duck) * (1.f - MoveBlend);
	HipRf += Stance * Relax;   HipLf += Stance * Relax;
	KneeRf -= 2.f * Stance * Relax;   KneeLf -= 2.f * Stance * Relax;
	// Sliding: one leg stretched out in front, the other folded under
	HipRf = FMath::Lerp(HipRf, 78.f, SlideAlpha);   KneeRf = FMath::Lerp(KneeRf, -4.f, SlideAlpha);
	HipLf = FMath::Lerp(HipLf, 45.f, SlideAlpha);   KneeLf = FMath::Lerp(KneeLf, -115.f, SlideAlpha);
	// Sitting: thighs level, shins hanging down
	HipRf = FMath::Lerp(HipRf, 90.f, SitAlpha);     KneeRf = FMath::Lerp(KneeRf, -90.f, SitAlpha);
	HipLf = FMath::Lerp(HipLf, 90.f, SitAlpha);     KneeLf = FMath::Lerp(KneeLf, -90.f, SitAlpha);
	// Riding: thighs forward and a little apart, shins hanging beside the horse
	HipRf = FMath::Lerp(HipRf, 62.f, MountAlpha);   KneeRf = FMath::Lerp(KneeRf, -42.f, MountAlpha);
	HipLf = FMath::Lerp(HipLf, 62.f, MountAlpha);   KneeLf = FMath::Lerp(KneeLf, -42.f, MountAlpha);
	// Climbing: legs step up the rungs alternately
	HipRf = FMath::Lerp(HipRf, 55.f + 45.f * CS, ClimbAlpha);   KneeRf = FMath::Lerp(KneeRf, -(65.f + 35.f * CS), ClimbAlpha);
	HipLf = FMath::Lerp(HipLf, 55.f - 45.f * CS, ClimbAlpha);   KneeLf = FMath::Lerp(KneeLf, -(65.f - 35.f * CS), ClimbAlpha);
	// Rolling: tucked into a ball
	HipRf = FMath::Lerp(HipRf, 105.f, TuckAlpha);   KneeRf = FMath::Lerp(KneeRf, -125.f, TuckAlpha);
	HipLf = FMath::Lerp(HipLf, 105.f, TuckAlpha);   KneeLf = FMath::Lerp(KneeLf, -125.f, TuckAlpha);

	HipR->SetRelativeRotation(FRotator(HipRf, 0.f, HipRroll));
	HipL->SetRelativeRotation(FRotator(HipLf, 0.f, HipLroll));
	KneeR->SetRelativeRotation(FRotator(KneeRf, 0.f, 0.f));
	KneeL->SetRelativeRotation(FRotator(KneeLf, 0.f, 0.f));
	// Feet stay flat on the ground (a running stride lets them tip a little); in the air, sitting, riding, climbing,
	// sliding and rolling they follow the shins as before
	const float Level = Ground * (1.f - SlideAlpha) * (1.f - SitAlpha) * (1.f - MountAlpha) * (1.f - ClimbAlpha) * (1.f - TuckAlpha);
	// Walking feet: flat while planted, the heel rising as the foot pushes off, then following the shin (toes trailing)
	// through the swing and levelling out again to land
	const float Gait = MoveBlend * Level;
	auto FootPitch = [&](float H, float K, float Follow, float Toe)
	{
		return Level * FMath::Lerp(-(H + K), -8.f, Follow * Gait) - 20.f * Toe * Gait;
	};
	AnkleR->SetRelativeRotation(FRotator(FootPitch(HipRf, KneeRf, GFollowR, GToeR), 0.f, 0.f));
	AnkleL->SetRelativeRotation(FRotator(FootPitch(HipLf, KneeLf, GFollowL, GToeL), 0.f, 0.f));

	P.HipRf = HipRf; P.HipLf = HipLf; P.KneeRf = KneeRf; P.KneeLf = KneeLf; P.IdleAmt = IdleAmt; P.BreathS = BreathS;
}

void AKnightCharacter::PoseBody(FKnightPose& P)
{
	const float Dt = P.Dt;
	const float HipRf = P.HipRf, HipLf = P.HipLf, KneeRf = P.KneeRf, KneeLf = P.KneeLf, LegReach = P.LegReach, UR = P.UR, GContact = P.GContact, Hz = P.Hz, GAb = P.GAb, GAf = P.GAf, WR = P.WR, WL = P.WL;
	const float Air = P.Air, Stream = P.Stream, F01 = P.F01, Lean = P.Lean, Ground = P.Ground, Squash = P.Squash, DuckLean = P.DuckLean, BreathS = P.BreathS, IdleAmt = P.IdleAmt, Twist = P.Twist, S = P.S, C = P.C, DuckC = P.DuckC;

	// ---------- Body bounce ----------
	// The hips dip as the legs spread (so planted feet stay on the ground) and rise over each step
	// Walking: the hips dip when the legs spread. Running: the body is lowest mid-stance and floats up
	// while the legs are spread, so there is a brief moment with both feet off the ground.
	// The hips ride exactly as high as the planted leg holds them (its thigh and shin angles), so a planted foot stays on
	// the ground: highest as the body passes over a straight leg, lowest as the feet spread, a little lower again as a
	// running knee gives. Both feet down: the lower of the two. Both up (running): between the two, floating.
	const float Thigh = float(KnightDesign::ThighLen()), Shin = float(KnightDesign::ShinLen());
	auto LegHeight = [&](float H, float K) { return Thigh * FMath::Cos(FMath::DegreesToRadians(H)) + Shin * FMath::Cos(FMath::DegreesToRadians(H + K)) - LegReach; };
	const float HR = LegHeight(HipRf, KneeRf), HL = LegHeight(HipLf, KneeLf);
	// In the float between running steps the body follows a real arc: from the height at push-off to the height at landing,
	// lifted by how long it is in the air
	float FlightZ = 0.f;
	{
		const float Half = FMath::Fmod(UR, 0.5f);
		const float F = FMath::Clamp((Half - GContact) / FMath::Max(0.5f - GContact, 0.01f), 0.f, 1.f);
		const float TAir = FMath::Max(0.5f - GContact, 0.f) / FMath::Max(Hz, 0.1f);
		const float Arc = 0.5f * 980.f * FMath::Square(0.5f * TAir);
		FlightZ = FMath::Lerp(LegHeight(-GAb, -4.f), LegHeight(GAf, -4.f), F) + 4.f * Arc * F * (1.f - F);
	}
	const float WAir = FMath::Max(0.f, 1.f - WR - WL);   // neither foot holding him: the float
	const float GroundZ = (WR * HR + WL * HL + WAir * FlightZ) / FMath::Max(WR + WL + WAir, 0.01f);
	float BodyZ = FMath::Lerp(GroundZ, 0.f, Air);
	BodyZ = FMath::Lerp(BodyZ, -42.f, SlideAlpha);
	BodyZ = FMath::Lerp(BodyZ, 0.f, SitAlpha);
	BodyZ = FMath::Lerp(BodyZ, 98.f, MountAlpha);                  // up onto the saddle
	BodyZ = FMath::Lerp(BodyZ, -45.f, TuckAlpha);
	SmoothBodyZ = FMath::FInterpTo(SmoothBodyZ, BodyZ, Dt, 14.f); // soften any remaining steps
	BodyRoot->SetRelativeLocation(FVector(0.f, 0.f, SmoothBodyZ));

	// ---------- Chest: lean into the run, twist against the hips, lean into turns ----------
	const float NewYaw = GetActorRotation().Yaw;
	const float TurnRate = FMath::Clamp(FMath::FindDeltaAngleDegrees(PrevYaw, NewYaw) / Dt, -720.f, 720.f);
	PrevYaw = NewYaw;
	YawRate = FMath::FInterpTo(YawRate, TurnRate, Dt, 8.f);
	const float TurnRoll = FMath::Clamp(YawRate * 0.025f, -12.f, 12.f) * Stream;

	const float AirLean = FMath::Lerp(4.f, -10.f, F01); // slight arch back while rising, forward while falling
	// at ease the chest leans a little forward (not standing against a wall) and lifts with each breath
	float UpperPitch = FMath::Lerp(-Lean - RestLean * Ground, AirLean, Air) - 12.f * Squash - DuckLean - 10.f * BlockAlpha * Ground + 0.9f * BreathS * IdleAmt;   // behind the shield he hunches over it
	UpperPitch = FMath::Lerp(UpperPitch, 28.f, SlideAlpha);  // lean back into the slide
	UpperPitch = FMath::Lerp(UpperPitch, 0.f, SitAlpha);
	UpperPitch = FMath::Lerp(UpperPitch, -6.f * RunAlpha, MountAlpha);   // a little forward in a gallop
	UpperPitch = FMath::Lerp(UpperPitch, -45.f, TuckAlpha);
	const float Sway = 0.9f * FMath::Sin(IdleTime * 0.62f) * IdleAmt;   // a slow shift of weight from side to side while standing
	UpperPivot->SetRelativeRotation(FRotator(UpperPitch, Twist * S * Ground - LegYaw, TurnRoll + Sway));   // (turned back against the hips' LegYaw: the chest stays square to the camera)

	// Chest bob: the upper body sinks as each foot takes weight and springs back up between steps.
	// (Only the chest moves, so the feet never sink into the ground.) In the air it stretches on the way up.
	const float BobAmp = FMath::Lerp(0.8f, 3.f, RunAlpha) * MoveBlend;   // (the hips carry the real rise and fall now)
	const float ChestTarget = (Ground * (-BobAmp * (1.f - S * S)) + Air * FMath::Lerp(5.f, -4.f, F01)) * (1.f - FMath::Max(SlideAlpha, SitAlpha)) + 0.6f * BreathS * IdleAmt;   // the chest rises as he breathes in
	SmoothChestZ = FMath::FInterpTo(SmoothChestZ, ChestTarget, Dt, 30.f);
	UpperPivot->SetRelativeLocation(UpperBase + FVector(0.f, 0.f, SmoothChestZ));

	// Pelvis sways with the steps: a little roll as weight shifts, and hips turning against the shoulders
	float FlipPitch = 0.f;
	if (bRolling)
	{
		const float K = FMath::Clamp(RollT / RollDur, 0.f, 1.f);
		FlipPitch = -360.f * (K * K * (3.f - 2.f * K)); // one forward somersault
	}
	BodyRoot->SetRelativeRotation(FRotator(FlipPitch, -0.5f * Twist * S * Ground + LegYaw, 2.5f * MoveBlend * C * Ground));

	// The head counter-steers so the knight keeps looking ahead, with a small nod on each step
	const float Nod = 1.2f * FMath::Sin(2.f * GaitPhase) * MoveBlend * Ground;
	// (crouched, the head also takes back the rest of the squat's lean, so the helm looks the same way as when standing)
	HeadPivot->SetRelativeRotation(FRotator(-0.6f * UpperPitch + Nod - 7.f * Ground - 12.f * BlockAlpha * Ground + 0.4f * 24.f * DuckC * (1.f - BlockAlpha), -0.7f * Twist * S * Ground, -0.5f * TurnRoll - 0.7f * Sway));

	P.TurnRoll = TurnRoll;
}

void AKnightCharacter::PoseCape(FKnightPose& P)
{
	const float Dt = P.Dt, Stream = P.Stream, F01 = P.F01, Air = P.Air, TurnRoll = P.TurnRoll;

	// ---------- Cape and plume ----------
	const float FlapAmp = 3.f + 9.f * Stream;
	const float CapeUpperG = FMath::Lerp(-4.f, -28.f, Stream) + FlapAmp * FMath::Sin(FlutterTime * 3.1f);
	const float CapeLowerG = FMath::Lerp(-6.f, -38.f, Stream) + FlapAmp * 1.6f * FMath::Sin(FlutterTime * 5.3f + 1.2f);
	const float CapeUpperA = FMath::Lerp(-5.f, -40.f, F01);  // streams down while rising, flies up while falling
	const float CapeLowerA = FMath::Lerp(-8.f, -65.f, F01);
	CapeUpper->SetRelativeRotation(FRotator(
		FMath::Lerp(CapeUpperG, CapeUpperA, Air), 0.f,
		(2.f + 5.f * Stream) * FMath::Sin(FlutterTime * 2.3f) - 0.5f * TurnRoll));
	CapeLower->SetRelativeRotation(FRotator(
		FMath::Lerp(CapeLowerG, CapeLowerA, Air), 0.f,
		(3.f + 9.f * Stream) * FMath::Sin(FlutterTime * 3.7f + 0.8f) - 0.8f * TurnRoll));

	const float PlumeG = FMath::Lerp(0.f, 20.f, Stream) + (3.f + 5.f * Stream) * FMath::Sin(FlutterTime * 4.1f + 0.5f);
	const float PlumeA = FMath::Lerp(20.f, -15.f, F01);
	PlumePivot->SetRelativeRotation(FRotator(FMath::Lerp(PlumeG, PlumeA, Air), 0.f, 4.f * Stream * FMath::Sin(FlutterTime * 3.3f)));
	TickCape(Dt);
}

void AKnightCharacter::PoseArms(FKnightPose& P)
{
	const float Dt = P.Dt, F01 = P.F01, Air = P.Air, ArmAmp = P.ArmAmp, S = P.S, Squash = P.Squash, Ground = P.Ground, BreathS = P.BreathS, IdleAmt = P.IdleAmt, ElbowBase = P.ElbowBase, ElbowSwing = P.ElbowSwing, CS = P.CS;

	// ---------- Arms ----------
	const float ArmSway = 1.5f * FMath::Sin(IdleTime * 1.7f) * (1.f - MoveBlend);
	const float ArmAirPitch = FMath::Lerp(100.f, 25.f, F01);   // arms thrown up on the way up, then out
	const float ArmAirSpread = FMath::Lerp(20.f, 50.f, F01);

	// Left arm (shield): swings opposite the left leg; raised in front of the chest when blocking
	ShieldHoldA = FMath::FInterpTo(ShieldHoldA, (ShieldActive() && !bStowed) ? 1.f : 0.f, Dt, 8.f);   // no shield: the left arm hangs normally
	float PitchL = FMath::Lerp(0.6f * ArmAmp * S + ArmSway, ArmAirPitch, Air) + 25.f * Squash + 4.f * Ground;
	float RollL = Air * ArmAirSpread * 0.7f + 1.2f * BreathS * IdleAmt;
	float ElbowLdeg = FMath::Lerp(ElbowBase + ElbowSwing * FMath::Max(S, 0.f), 15.f, Air) + ShieldRest().ElbowPitch * Ground * ShieldHoldA;   // at rest the left hand rests on the inside of the shield (when there is one)
	PitchL = FMath::Lerp(PitchL, 38.f, BlockAlpha);
	ElbowLdeg = FMath::Lerp(ElbowLdeg, 55.f, BlockAlpha);
	PitchL = FMath::Lerp(PitchL, -40.f, SlideAlpha);   RollL = FMath::Lerp(RollL, 30.f, SlideAlpha);
	PitchL = FMath::Lerp(PitchL, 32.f, SitAlpha);      ElbowLdeg = FMath::Lerp(ElbowLdeg, 70.f, SitAlpha);
	PitchL = FMath::Lerp(PitchL, 38.f, MountAlpha);    ElbowLdeg = FMath::Lerp(ElbowLdeg, 55.f, MountAlpha);    // holding the reins
	PitchL = FMath::Lerp(PitchL, 85.f, TuckAlpha);     ElbowLdeg = FMath::Lerp(ElbowLdeg, 100.f, TuckAlpha);
	PitchL = FMath::Lerp(PitchL, 140.f + 35.f * CS, ClimbAlpha);  ElbowLdeg = FMath::Lerp(ElbowLdeg, 25.f, ClimbAlpha);
	ArmPivotL->SetRelativeLocation(LeftBase + BlockAlpha * FVector(22.f, 30.f, 10.f)); // shield comes across in front of the body
	ArmPivotL->SetRelativeRotation(FRotator(PitchL, 0.f, RollL));
	ElbowL->SetRelativeRotation(FRotator(ElbowLdeg, ShieldRest().ElbowYaw * Ground * (1.f - BlockAlpha) * ShieldHoldA, ShieldRest().ElbowRoll * Ground * (1.f - BlockAlpha) * ShieldHoldA));   // the forearm flat behind the shield
	HoldShield();

	// Right arm (sword): swings opposite the right leg, unless it is busy mining or chopping
	float TargetPitch = FMath::Lerp(-0.22f * ArmAmp * S - ArmSway, ArmAirPitch, Air) + 25.f * Squash + 4.f * Ground;   // arms hang a touch forward (relaxed shoulders)
	float TargetYaw = 0.f;
	float RollR = -Air * ArmAirSpread - 1.2f * BreathS * IdleAmt - (EaseKind == JeebItems::K_Staff ? 14.f * EaseAlpha : 0.f);   // the arms ease out a little with each breath (and out to the side to hold a staff)
	float ElbowRdeg = FMath::Lerp(HeldElbow + 0.25f * ElbowBase, 40.f, Air);
	float ArmSpeed = 25.f;

	TargetPitch = FMath::Lerp(TargetPitch, -35.f, SlideAlpha);   RollR = FMath::Lerp(RollR, -30.f, SlideAlpha);
	TargetPitch = FMath::Lerp(TargetPitch, 32.f, SitAlpha);      ElbowRdeg = FMath::Lerp(ElbowRdeg, 70.f, SitAlpha);
	TargetPitch = FMath::Lerp(TargetPitch, 38.f, MountAlpha);    ElbowRdeg = FMath::Lerp(ElbowRdeg, 55.f, MountAlpha);
	TargetPitch = FMath::Lerp(TargetPitch, 85.f, TuckAlpha);     ElbowRdeg = FMath::Lerp(ElbowRdeg, 100.f, TuckAlpha);
	TargetPitch = FMath::Lerp(TargetPitch, 140.f - 35.f * CS, ClimbAlpha);  ElbowRdeg = FMath::Lerp(ElbowRdeg, 25.f, ClimbAlpha);

	if (bCharging && !bAttacking && !bSwinging)
	{
		// Spear drawn back, the stronger the pull the further back
		const float K = FMath::Clamp(ChargeT / 1.0f, 0.f, 1.f);
		TargetPitch = FMath::Lerp(70.f, 45.f, K);
		TargetYaw = FMath::Lerp(0.f, 18.f, K);
		ElbowRdeg = FMath::Lerp(60.f, 110.f, K);
		RollR = 0.f;
		ArmSpeed = 20.f;
	}

	if (bAttacking && !bSwinging)
	{
		const float T = AttackT / AttackDur;
		if (AtkStyle == EAtkStyle::Thrust)
		{
			// Pull back, then drive the arm straight out
			if (T < 0.35f)
			{
				const float A = T / 0.35f;
				TargetPitch = FMath::Lerp(55.f, 45.f, A);  TargetYaw = FMath::Lerp(10.f, 18.f, A);  ElbowRdeg = FMath::Lerp(90.f, 115.f, A);
			}
			else
			{
				const float A = FMath::Clamp((T - 0.35f) / 0.25f, 0.f, 1.f);
				TargetPitch = FMath::Lerp(45.f, 88.f, A);  TargetYaw = FMath::Lerp(18.f, 0.f, A);  ElbowRdeg = FMath::Lerp(115.f, 4.f, A);
			}
		}
		else if (AtkStyle == EAtkStyle::Cast)
		{
			// Raise the arm and point
			const float A = FMath::Clamp(T / 0.3f, 0.f, 1.f);
			const float E = A * A * (3.f - 2.f * A);
			TargetPitch = FMath::Lerp(20.f, 92.f, E);  TargetYaw = 0.f;  ElbowRdeg = FMath::Lerp(50.f, 8.f, E);
		}
		else if (AttackDir > 0)
		{
						// First swing, right to left: the arm comes up to shoulder height and swings out to the right,
			// then the whole arm sweeps flat across the body
			if (T < 0.35f)
			{
				const float A = T / 0.35f;
				const float E = A * A * (3.f - 2.f * A);
				TargetPitch = FMath::Lerp(10.f, 78.f, E);
				TargetYaw = FMath::Lerp(0.f, 80.f, E);
				ElbowRdeg = FMath::Lerp(ElbowRdeg, 65.f, E);
			}
			else if (T < 0.55f)
			{
				const float A = (T - 0.35f) / 0.2f;
				TargetPitch = FMath::Lerp(78.f, 84.f, A);
				TargetYaw = FMath::Lerp(80.f, -70.f, A * A);
				ElbowRdeg = FMath::Lerp(65.f, 5.f, A);
			}
			else { TargetPitch = 84.f; TargetYaw = -70.f; ElbowRdeg = 5.f; }
		}
		else
		{
						// Follow-up, left to right: starts where the first one ended, draws back across the body, whips flat back out
			if (T < 0.35f)
			{
				const float A = T / 0.35f;
				const float E = A * A * (3.f - 2.f * A);
				TargetPitch = FMath::Lerp(84.f, 80.f, E);
				TargetYaw = FMath::Lerp(-70.f, -88.f, E);
				ElbowRdeg = FMath::Lerp(5.f, 70.f, E);
			}
			else if (T < 0.55f)
			{
				const float A = (T - 0.35f) / 0.2f;
				TargetPitch = FMath::Lerp(80.f, 84.f, A);
				TargetYaw = FMath::Lerp(-88.f, 75.f, A * A);
				ElbowRdeg = FMath::Lerp(70.f, 5.f, A);
			}
			else { TargetPitch = 84.f; TargetYaw = 75.f; ElbowRdeg = 5.f; }
		}
		RollR = 0.f;
		ArmSpeed = 38.f;
	}

	if (bFishing)
	{
		// Pole held out over the water, with a slow bob
		TargetPitch = 66.f + 3.f * FMath::Sin(IdleTime * 2.f);
		TargetYaw = 0.f;
		RollR = 0.f;
		ElbowRdeg = 22.f;
		ArmSpeed = 14.f;
	}

	if (bSwinging)
	{
		SwingTime += Dt;
		const float T = FMath::Fmod(SwingTime, SwingPeriod) / SwingPeriod;

		if (T < 0.55f)
		{
			// Wind up: raise the arm smoothly
			const float A = T / 0.55f;
			TargetPitch = FMath::Lerp(StrikeAngle, WindupAngle, A * A * (3.f - 2.f * A));
		}
		else if (T < 0.7f)
		{
			// Strike: fast swing down
			const float A = (T - 0.55f) / 0.15f;
			TargetPitch = FMath::Lerp(WindupAngle, StrikeAngle, A * A);
		}
		else
		{
			// Brief pause on impact
			TargetPitch = StrikeAngle;
		}
		if (bSideways)
		{
			// The swing value drives a horizontal sweep, with the arm held out in front
			TargetYaw = TargetPitch;
			TargetPitch = 80.f;
		}
		RollR = 0.f;
		ElbowRdeg = 0.f;
		ArmSpeed = 30.f;
	}

	ArmPitch = FMath::FInterpTo(ArmPitch, TargetPitch, Dt, ArmSpeed);
	ArmYaw = FMath::FInterpTo(ArmYaw, TargetYaw, Dt, ArmSpeed);
	ArmPivot->SetRelativeRotation(FRotator(ArmPitch, ArmYaw, RollR));
	ElbowRCur = FMath::FInterpTo(ElbowRCur, ElbowRdeg, Dt, 18.f);
	ElbowR->SetRelativeRotation(FRotator(ElbowRCur, 0.f, 0.f));
	HoldWeaponAtEase(Dt, bAttacking || bCharging || bSwinging || bFishing || Air > 0.5f || FMath::Max(FMath::Max(SitAlpha, MountAlpha), FMath::Max(ClimbAlpha, FMath::Max(TuckAlpha, SlideAlpha))) > 0.5f);
}

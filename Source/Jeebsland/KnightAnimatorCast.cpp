#include "KnightAnimator.h"
#include "Inventory.h"
#include "WeaponDesign.h"
#include "KnightDesign.h"
#include "HAL/IConsoleManager.h"
#include "Logging/LogMacros.h"

using namespace JeebAnim;

// A wand's or staff's cast (2026-11-18, motion review: the cast used to borrow the sword's dash with the arms only, and the 126 cm staff
// swung low through his thighs like a pole). Now two Mixamo spell clips (KnightMoves.csv Cast_Wand and Cast_Staff; CastWand is Mixamo's
// "Standing 1H Magic Attack 01", CastPush "Standing 2H Magic Attack 01", CastUp "Standing 1H Cast Spell 01"), the arms and chest only: the
// legs stay his own stance. The game's timing is unchanged: the bolt leaves at 0.3 of the move (KnightCharacterCombat.cpp, TickAttack's
// HitFrac, from a fixed point ahead of his chest), so each row's ClipHit..ClipFollow (the push) is laid over CastPushFrom..0.3 and the
// arm is fully out as it flies. The wand's off arm keeps the shield where the guard holds it.
// What is in the hand is turned to a line through the cast: the staff stands nearly upright as it comes up and tips its orb toward
// the target at the release, the wand points ahead; jeeb.Knight.CastTilt (degrees from straight up: start, release, end) per kind.
namespace
{
	constexpr float CastRelease = 0.3f;    // (= TickAttack's HitFrac for a cast: keep the two together)
	constexpr float CastPushFrom = 0.16f;  // where the push starts in the move

	TAutoConsoleVariable<FString> CVarCastTiltStaff(TEXT("jeeb.Knight.CastTiltStaff"), TEXT("12,50,15"), TEXT("The staff through a cast: degrees from straight up at the start, the release and the end (its orb tips toward the target)."), ECVF_Default);
	TAutoConsoleVariable<FString> CVarCastTiltWand(TEXT("jeeb.Knight.CastTiltWand"), TEXT("45,88,50"), TEXT("The wand through a cast: degrees from straight up at the start, the release and the end (90 points straight ahead)."), ECVF_Default);
	TAutoConsoleVariable<float> CVarCastSquare(TEXT("jeeb.Knight.CastSquare"), 1.f, TEXT("How much his chest turns square to the shot through a cast (0 = as the clip turns it, to his right)."), ECVF_Default);
	TAutoConsoleVariable<float> CVarCastPoint(TEXT("jeeb.Knight.CastPoint"), 1.f, TEXT("How much the wand's arm points along the bolt's path at the release (0 = the clip's own arm)."), ECVF_Default);
	TAutoConsoleVariable<float> CVarCastPitch(TEXT("jeeb.Knight.CastPitch"), 6.f, TEXT("Degrees the wand's arm points above his aim at the release: the reticle sits above the middle of the screen, so the bolt climbs a little (jeeb.Magic.Reticle)."), ECVF_Default);
	TAutoConsoleVariable<float> CVarCastAim(TEXT("jeeb.Knight.CastAim"), 1.f, TEXT("How much the staff or wand is turned onto its cast line (0 = the clip's own hand)."), ECVF_Default);

	FVector Tilts(const TAutoConsoleVariable<FString>& V, const FVector& Def)
	{
		TArray<FString> P;
		V.GetValueOnGameThread().ParseIntoArray(P, TEXT(","));
		return P.Num() == 3 ? FVector(FCString::Atof(*P[0]), FCString::Atof(*P[1]), FCString::Atof(*P[2])) : Def;
	}

	// The staff carried as a walking staff (owner 2026-11-18, picture 12: "the bottom of the staff is on the ground"; before, it was upright
	// at his side with the butt at his knee). Its butt stands on the ground beside his right foot (jeeb.Knight.StaffButt, mesh frame from his
	// feet: -X his right, +Y ahead, +Z up; 0 is the ground), it leans toward where the fist wants to be (jeeb.Knight.StaffHand, from his
	// right shoulder), and the fist holds it jeeb.Knight.StaffHold cm up from the butt: the model slides through the fist to match
	// (AKnightCharacter::PlaceStaffModel), so the hand is always on the shaft and the butt always where it is set. Walking, the butt lifts a
	// little off the ground and swings with the stride like a walking stick (jeeb.Knight.StaffWalk: lift cm, swing cm; running lifts twice).
	TAutoConsoleVariable<FString> CVarStaffHand(TEXT("jeeb.Knight.StaffHand"), TEXT("-22,20,-36"), TEXT("Where the fist holding the staff wants to be, from his right shoulder (cm; -X his right, +Y ahead, +Z up): the staff leans from its butt toward it."), ECVF_Default);
	TAutoConsoleVariable<FString> CVarStaffButt(TEXT("jeeb.Knight.StaffButt"), TEXT("-36,26,0"), TEXT("Where the carried staff's butt stands, from his feet (cm; -X his right, +Y ahead, Z 0 the ground)."), ECVF_Default);
	TAutoConsoleVariable<FString> CVarStaffWalk(TEXT("jeeb.Knight.StaffWalk"), TEXT("7,12,0"), TEXT("Walking with the staff: how far its butt lifts off the ground (cm; twice running) and swings ahead and back with the stride (cm)."), ECVF_Default);
	TAutoConsoleVariable<float> CVarStaffHold(TEXT("jeeb.Knight.StaffHold"), 110.f, TEXT("How far up the staff from its butt the fist holds it (cm; the model is 165 long; read by PlaceStaffModel too)."), ECVF_Default);
	constexpr float StaffButtFromGrip = 36.3f;   // (the model as built: its grip 0.78 of its 165 cm from the orb end, Tools/prop_clean.py)

	// (2026-11-18 second review: the staff snapped from the carry to overhead in the first 0.13 s of its cast, the clip's wind-up squeezed into
	// 0.18 s under a 0.12 s blend.) Now the carry hands the arm over to the spell's raised pose over this share of the move (1.1 s for the staff),
	// blended joint by joint, so the fist travels a curve up and out from his hip, the staff staying near upright, instead of a straight jump.
	TAutoConsoleVariable<float> CVarCastLift(TEXT("jeeb.Knight.CastLift"), 0.24f, TEXT("The staff's cast: the share of the move over which the arm leaves the upright carry for the spell's raised pose (0.24 of 1.1 s; 0.02 is the old snap)."), ECVF_Default);
	// (the same review: the empty left arm stood stiffly raised: the sword-and-shield clips hold it up for a shield the staff never has)
	TAutoConsoleVariable<float> CVarStaffFreeArm(TEXT("jeeb.Knight.StaffFreeArm"), 1.f, TEXT("With the staff, how much the empty left arm hangs relaxed (his rest pose) where the sword-and-shield clips would hold it up for a shield, stepping sideways or back (0 = as the clips have it)."), ECVF_Default);

	FQuat BlendRot(const FQuat& A, const FQuat& B, float W)
	{
		const float S = (A | B) < 0.f ? -1.f : 1.f;
		FQuat Q(A.X + (B.X * S - A.X) * W, A.Y + (B.Y * S - A.Y) * W, A.Z + (B.Z * S - A.Z) * W, A.W + (B.W * S - A.W) * W);
		Q.Normalize();
		return Q;
	}
}

// The item in the right fist turned so that it points along Want (mesh frame), by W; returns the degrees it had to turn
static float TurnHeldTo(const FClipLib& L, FPose& B, const FVector& Want, float W)
{
	FQuat HandRot;
	L.BonePos(B, RHand, &HandRot);
	const FVector Dir = HandRot.RotateVector(L.BladeInHand).GetSafeNormal();
	const float Deg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(float(FVector::DotProduct(Dir, Want)), -1.f, 1.f)));
	if (W > 0.001f) L.TurnInMesh(B, RHand, FQuat::Slerp(FQuat::Identity, FQuat::FindBetweenNormals(Dir, Want), W));
	return Deg;
}

void FKnightAnimator::StaffCarry(const FInput& In, FPose& B, int32 Act, float Dt)
{
	// (not in the cast, which places the staff itself; nor rolling, knocked down, climbing or swimming; stowed it is on his back)
	const bool bOn = In.Kind == JeebItems::K_Staff && !In.Stowed() && !In.bSitting && !In.bMounted && !In.bDrinking && !In.bClimbing
		&& Act != 39 && Act != 20 && Act != 10 && Act != 11 && !(Act >= 70 && Act <= 99) && Act < 100;
	float W;
	if (Act == 39 && In.Kind == JeebItems::K_Staff)
	{
		// the cast: the carry lets go of the arm by how far the move has gone, not by time from its start (StaffW keeps what it had until then:
		// after the lift it is 0, and the carry comes back over 0.2 s once the cast is over, under the 0.36 s blend out)
		const float Lift = FMath::Clamp(CVarCastLift.GetValueOnGameThread(), 0.02f, 0.6f);
		const float U = FMath::Clamp(In.MoveU, 0.f, 1.f);
		W = FMath::SmoothStep(0.f, 1.f, StaffW) * (1.f - FMath::SmoothStep(0.f, Lift, U));
		if (U >= Lift) StaffW = 0.f;
	}
	else
	{
		StaffW = FMath::FInterpConstantTo(StaffW, bOn ? 1.f : 0.f, Dt, 1.f / 0.2f);
		W = FMath::SmoothStep(0.f, 1.f, StaffW);
	}
	if (In.Kind != JeebItems::K_Staff || W <= 0.001f) return;

	// The whole carry is worked out on the arm, then the arm's own joints are blended toward it by W (they were: the hand's target moved
	// in a straight line, and the elbow's solve flipped to the clip's at W 0). Joint by joint, the fist swings on a curve, as an arm does.
	const int32 Chain[3] = { RArm, RFore, RHand };
	FQuat Was[3];
	for (int32 i = 0; i < 3; ++i) Was[i] = B.Rot[Bone[Chain[i]]];
	// where the butt goes and which way the staff leans (walking: lifted and swinging with the stride)
	FVector Butt = Tilts(CVarStaffButt, FVector(-36.f, 26.f, 0.f));
	if (bMoving)
	{
		const FVector Wk = Tilts(CVarStaffWalk, FVector(7.f, 12.f, 0.f));
		const float Go = FMath::Clamp(SmSpeed / 150.f, 0.f, 1.f), Run = FMath::SmoothStep(230.f, 400.f, SmSpeed);
		Butt.Z += Wk.X * Go * (1.f + Run);
		Butt.Y += Wk.Y * Go * FMath::Sin(2.f * PI * Phase) + 10.f * Run;   // (running: carried a little further ahead)
	}
	const FVector HandSpot = BonePos(B, RArm) + Tilts(CVarStaffHand, FVector(-22.f, 20.f, -36.f));
	FVector Up = HandSpot - Butt;
	if (!Up.Normalize() || Up.Z < 0.5f) Up = FVector::UpVector;
	const float Hold = FMath::Clamp(CVarStaffHold.GetValueOnGameThread(), StaffButtFromGrip, 160.f);
	const FVector GripWant = Butt + Up * Hold;
	// The fist's grip point is not the hand bone: reach, turn the staff onto its line, see where the grip is, and reach again by the miss
	const FVector GripInHand = L.RightGrip.TransformPosition(KnightDesign::GripPos(false) - KnightDesign::PivotPos(KnightDesign::P_WristR));
	FQuat HandWas;
	FVector Target = BonePos(B, RHand, &HandWas);
	float Miss = 0.f;
	for (int32 It = 0; It < 3; ++It)
	{
		FQuat HR;
		const FVector At = BonePos(B, RHand, &HR);
		const FVector Off = GripWant - (At + HR.RotateVector(GripInHand));
		Miss = Off.Size();
		if (It > 0 && Miss < 0.3f) break;
		Target = (It == 0 ? At : Target) + Off;
		L.ReachTo(B, RArm, RFore, RHand, Target, FVector(-0.6f, -1.f, -0.2f));   // (the elbow back and out)
		FQuat HandNow;
		BonePos(B, RHand, &HandNow);
		TurnInMesh(B, RHand, HandWas * HandNow.Inverse());
		TurnHeldTo(L, B, Up, 1.f);
	}
	if (In.bShotLog)
	{
		FQuat HR;
		const FVector At = BonePos(B, RHand, &HR);
		const FVector Grip = At + HR.RotateVector(GripInHand);
		const FVector Axis = HR.RotateVector(L.BladeInHand).GetSafeNormal();
		const FVector ButtNow = Grip - Axis * Hold;
		UE_LOG(LogTemp, Log, TEXT("JEEBSTAFF w %.2f act %d moving %d grip miss %.1f cm, butt at %.0f,%.0f,%.1f (want %.0f,%.0f,%.1f), lean %.0f deg"), W, Act, bMoving ? 1 : 0,
			float(FVector::Dist(Grip, GripWant)), float(-ButtNow.X), float(ButtNow.Y), float(ButtNow.Z), float(-Butt.X), float(Butt.Y), float(Butt.Z),
			FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(float(Axis.Z), -1.f, 1.f))));
	}
	if (W < 0.999f)
		for (int32 i = 0; i < 3; ++i) B.Rot[Bone[Chain[i]]] = BlendRot(Was[i], B.Rot[Bone[Chain[i]]], W);

	// The empty left arm: not in a cast (the spell's push is two-handed). Standing he is in his rest pose already (no guard stance with
	// the staff: Locomotion); walking ahead his own walk swings it; only the sword-and-shield clips for going sideways and backing up raise it.
	if (Act != 0) return;
	float Side = 1.f;
	if (bMoving)
	{
		const float A = FMath::Atan2(DirR, DirF) - FMath::DegreesToRadians(In.LegYaw);
		const float C = FMath::Cos(A), S = FMath::Sin(A);
		Side = 1.f - FMath::Max(C, 0.f) / FMath::Max(FMath::Abs(C) + FMath::Abs(S), 0.01f);
	}
	const float FreeW = W * Side * FMath::Clamp(CVarStaffFreeArm.GetValueOnGameThread(), 0.f, 1.f);
	if (FreeW > 0.001f)
		if (const FClip* R = Get(TEXT("Rest")))
		{
			Sample(*R, 0.f, Scratch, false);
			for (const int32 i : { LShoulder, LArm, LFore, LHand }) B.Rot[Bone[i]] = BlendRot(B.Rot[Bone[i]], Scratch.Rot[Bone[i]], FreeW);
		}
}

void FKnightAnimator::CastPose(const FInput& In, FPose& B, float& OutBlend)
{
	const bool bStaff = In.Kind == JeebItems::K_Staff;
	const TCHAR* Row = bStaff ? TEXT("Cast_Staff") : TEXT("Cast_Wand");
	const FMove* M = Moves.Find(Row);
	if (!M) return;
	const float U = FMath::Clamp(In.MoveU, 0.f, 1.f);
	PlayLower = 0.f;   // (his own stance: the clips step out)
	if (!bStaff && (In.bShieldArm || In.bWizHold)) PlayKeepOffArm = 1.f;   // (the clip's off hand flings out; the shield, or the wizard's book, ball or pipe, stays held, a fixed pose under it)
	PlayAt(Row, MoveClipTime(*M, U, CastPushFrom, CastRelease), 1000.f, B, &OutBlend);

	// Facing the shot (owner 2026-11-18, picture 13: the wand's clip turned his chest and arm to his right while the bolt flew straight
	// ahead). He already faces the aim point (AKnightCharacter::StartAttack); now his chest turns square to it over the push, spread up the
	// spine, and the wand's arm points along the bolt's path, pitched with his aim. Eased in and out with the move. Every machine works this
	// out from the same replicated facing and aim pitch, so other players see him point where the bolt goes.
	const float FaceW = FMath::SmoothStep(0.f, CastPushFrom, U) * (1.f - FMath::SmoothStep(0.72f, 1.f, U));
	{
		const float Sq = FMath::Clamp(CVarCastSquare.GetValueOnGameThread(), 0.f, 1.f) * FaceW;
		const FVector Across = BonePos(B, RArm) - BonePos(B, LArm);   // (left shoulder to right: -X when square)
		const FVector Fwd = FVector::CrossProduct(Across, FVector::UpVector);
		if (Sq > 0.001f && FVector2D(Fwd.X, Fwd.Y).Size() > 1.f)
		{
			const float Off = FMath::Atan2(-float(Fwd.X), float(Fwd.Y));   // (how far the chest is turned toward his right (-X), +)
			TurnInMesh(B, Spine, FQuat(FVector::UpVector, -Off * 0.35f * Sq));
			TurnInMesh(B, Spine1, FQuat(FVector::UpVector, -Off * 0.35f * Sq));
			TurnInMesh(B, Spine2, FQuat(FVector::UpVector, -Off * 0.3f * Sq));
			if (In.bShotLog) UE_LOG(LogTemp, Log, TEXT("JEEBCASTFACE u %.3f chest off %.0f deg, squared by %.2f"), U, FMath::RadiansToDegrees(Off), Sq);
		}
	}
	if (!bStaff)
	{
		const float Pt = FMath::Clamp(CVarCastPoint.GetValueOnGameThread(), 0.f, 1.f) * FaceW;
		const FVector Sh = BonePos(B, RArm), Hd = BonePos(B, RHand);
		const FVector Now = (Hd - Sh).GetSafeNormal();
		const float P = FMath::DegreesToRadians(FMath::Clamp(In.AimPitch + CVarCastPitch.GetValueOnGameThread(), -45.f, 45.f));
		const FVector Want(0.f, FMath::Cos(P), FMath::Sin(P));
		if (Pt > 0.001f && !Now.IsNearlyZero())
			TurnInMesh(B, RArm, FQuat::Slerp(FQuat::Identity, FQuat::FindBetweenNormals(Now, Want), Pt));
	}

	// the item on its line: in over the first frames, held through the release, out as the recovery ends (the blend to guard does the rest)
	// (the staff is upright at his side before and after, StaffCarry, so it stays on its line the whole way: no ramp)
	const float Aim = FMath::Clamp(CVarCastAim.GetValueOnGameThread(), 0.f, 1.f);
	const float W = bStaff ? Aim : Aim * FMath::SmoothStep(0.f, 0.12f, U) * (1.f - FMath::SmoothStep(0.7f, 1.f, U));
	float Deg = 0.f;
	if (W > 0.001f)
	{
		const FVector T = Tilts(bStaff ? CVarCastTiltStaff : CVarCastTiltWand, bStaff ? FVector(12.f, 50.f, 15.f) : FVector(45.f, 88.f, 50.f));
		const float Tilt = U < CastRelease ? FMath::Lerp(T.X, T.Y, FMath::SmoothStep(0.04f, CastRelease, U))
		                                   : FMath::Lerp(T.Y, T.Z, FMath::SmoothStep(CastRelease + 0.1f, 1.f, U));
		const float R = FMath::DegreesToRadians(Tilt - (bStaff ? 0.f : FMath::Clamp(In.AimPitch + CVarCastPitch.GetValueOnGameThread(), -45.f, 45.f) * FaceW));   // (the wand along the bolt's path, up or down with his aim)
		const FVector Want(0.f, FMath::Sin(R), FMath::Cos(R));   // (mesh frame: +Y ahead, +Z up)
		Deg = TurnHeldTo(L, B, Want, W);
	}
	if (In.bShotLog) UE_LOG(LogTemp, Log, TEXT("JEEBCAST u %.3f row %s clip %.2f aim %.2f turned %.0f deg"), U, Row, MoveClipTime(*M, U, CastPushFrom, CastRelease), W, Deg);
}

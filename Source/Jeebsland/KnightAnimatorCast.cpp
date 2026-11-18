#include "KnightAnimator.h"
#include "Inventory.h"
#include "WeaponDesign.h"
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
	TAutoConsoleVariable<float> CVarCastAim(TEXT("jeeb.Knight.CastAim"), 1.f, TEXT("How much the staff or wand is turned onto its cast line (0 = the clip's own hand)."), ECVF_Default);

	FVector Tilts(const TAutoConsoleVariable<FString>& V, const FVector& Def)
	{
		TArray<FString> P;
		V.GetValueOnGameThread().ParseIntoArray(P, TEXT(","));
		return P.Num() == 3 ? FVector(FCString::Atof(*P[0]), FCString::Atof(*P[1]), FCString::Atof(*P[2])) : Def;
	}

	// The staff carried (2026-11-18, visual review: it was held like a rifle, flat at his hip, 1.4 m out in front and 0.8 m behind):
	// the fist out beside his right hip, the elbow a little bent, and the staff upright through it, its butt by his foot and the orb
	// above his shoulder, leaning a little forward and out. Live: jeeb.Knight.StaffHand (the fist from the shoulder, cm, mesh frame:
	// -X his right, +Y ahead, +Z up) and jeeb.Knight.StaffLean (degrees forward, degrees out).
	TAutoConsoleVariable<FString> CVarStaffHand(TEXT("jeeb.Knight.StaffHand"), TEXT("-20,18,-44"), TEXT("Where the fist holding the staff is, from his right shoulder (cm; -X his right, +Y ahead, +Z up)."), ECVF_Default);
	TAutoConsoleVariable<FString> CVarStaffLean(TEXT("jeeb.Knight.StaffLean"), TEXT("8,6,0"), TEXT("The carried staff's lean from upright: degrees forward, degrees out to his right (third number unused)."), ECVF_Default);

	FVector StaffUp()
	{
		const FVector Lean = Tilts(CVarStaffLean, FVector(8.f, 6.f, 0.f));
		const float F = FMath::DegreesToRadians(Lean.X), O = FMath::DegreesToRadians(Lean.Y);
		return FVector(-FMath::Sin(O), FMath::Sin(F), 1.f).GetSafeNormal();   // (mesh frame)
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
	StaffW = FMath::FInterpConstantTo(StaffW, bOn ? 1.f : 0.f, Dt, 1.f / 0.2f);
	if (In.Kind != JeebItems::K_Staff || StaffW <= 0.001f) return;
	const float W = FMath::SmoothStep(0.f, 1.f, StaffW);
	const FVector Off = Tilts(CVarStaffHand, FVector(-20.f, 18.f, -44.f));
	FQuat HandWas;
	const FVector Hand = BonePos(B, RHand, &HandWas);
	L.ReachTo(B, RArm, RFore, RHand, FMath::Lerp(Hand, BonePos(B, RArm) + Off, W), FVector(-0.6f, -1.f, -0.2f));   // (the elbow back and out)
	FQuat HandNow;
	BonePos(B, RHand, &HandNow);
	TurnInMesh(B, RHand, HandWas * HandNow.Inverse());
	TurnHeldTo(L, B, StaffUp(), W);
}

void FKnightAnimator::CastPose(const FInput& In, FPose& B, float& OutBlend)
{
	const bool bStaff = In.Kind == JeebItems::K_Staff;
	const TCHAR* Row = bStaff ? TEXT("Cast_Staff") : TEXT("Cast_Wand");
	const FMove* M = Moves.Find(Row);
	if (!M) return;
	const float U = FMath::Clamp(In.MoveU, 0.f, 1.f);
	PlayLower = 0.f;   // (his own stance: the clips step out)
	if (!bStaff && In.bShieldArm) PlayKeepOffArm = 1.f;   // (the clip's off hand flings out; the shield stays on guard, a fixed pose under it)
	PlayAt(Row, MoveClipTime(*M, U, CastPushFrom, CastRelease), 1000.f, B, &OutBlend);

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
		const float R = FMath::DegreesToRadians(Tilt);
		const FVector Want(0.f, FMath::Sin(R), FMath::Cos(R));   // (mesh frame: +Y ahead, +Z up)
		Deg = TurnHeldTo(L, B, Want, W);
	}
	if (In.bShotLog) UE_LOG(LogTemp, Log, TEXT("JEEBCAST u %.3f row %s clip %.2f aim %.2f turned %.0f deg"), U, Row, MoveClipTime(*M, U, CastPushFrom, CastRelease), W, Deg);
}

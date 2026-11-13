#include "KnightAnimator.h"
#include "JeebTuning.h"

using namespace JeebAnim;

// Free climbing and the mantle (2026-11-12): the pose. Keys 80-91 (ChooseAction hands them over here):
//   80 the grab, 81-84 climbing up, down, left, right (the clip's stride follows how far he has moved along the rock, so hands and feet stay put),
//   85-88 the climb jump up, down, left, right, 89 the ledge climb-up, 90 a mantle over a fence, 91 a mantle onto a block.
// On the rock the whole body is tilted to lie parallel to it, about the hips, and set so the hands are on it (WallFrame). The scripted moves
// (85-91) keep their clip's own path at first and hand over to the capsule's by the end, so nothing jumps when they end.
// Rows in Content/Data/KnightMoves.csv; for the loops (81-84) ClipHit is how far one cycle of the clip climbs (cm).

namespace
{
	float EaseW(float U) { U = FMath::Clamp(U, 0.f, 1.f); return U * U * (3.f - 2.f * U); }
	FVector RockUp(const FVector& N)
	{
		const FVector U = (FVector::UpVector - N * N.Z).GetSafeNormal();
		return U.IsNearlyZero() ? FVector::UpVector : U;
	}
}

const TCHAR* FKnightAnimator::WallRow(int32 Key)
{
	static const TCHAR* Rows[] = { TEXT("WallGrab"), TEXT("WallUp"), TEXT("WallDown"), TEXT("WallLeft"), TEXT("WallRight"),
		TEXT("HopUp"), TEXT("HopDown"), TEXT("HopLeft"), TEXT("HopRight"), TEXT("LedgeUp"), TEXT("VaultOver"), TEXT("ClimbOnto") };
	return (Key >= 80 && Key <= 91) ? Rows[Key - 80] : TEXT("WallUp");
}

// Measured once per clip: where its hips sit on average, how far ahead of them the hands are, and its start and end postures
const FKnightAnimator::FWallStat& FKnightAnimator::WallStat(int32 Key)
{
	if (const FWallStat* S = WallStats.Find(Key)) return *S;
	FWallStat S;
	const JeebAnim::FMove* M = Moves.Find(WallRow(Key));
	const FClip* C = M ? Get(*M->Clip) : nullptr;
	if (C)
	{
		FPose P;
		P.Rot = RefRot;
		constexpr int32 N = 16;
		float HipZ = 0.f, HipY = 0.f, Hand = 0.f;
		for (int32 i = 0; i < N; ++i)
		{
			Sample(*C, FMath::Lerp(M->ClipStart, M->ClipEnd, i / float(N - 1)), P, false);
			HipZ += P.Hip.Z; HipY += P.Hip.Y;
			Hand += 0.5f * float(BonePos(P, LHand).Y + BonePos(P, RHand).Y) - P.Hip.Y;
		}
		S.MeanHipZ = HipZ / N; S.MeanHipY = HipY / N; S.HandFwd = Hand / N;
		auto Ends = [&](float T, FVector& H, float& Y, float& Z)
		{
			Sample(*C, T, P, false);
			H = P.Hip;
			const FVector A = BonePos(P, LFoot), B = BonePos(P, RFoot), A2 = BonePos(P, LToe), B2 = BonePos(P, RToe);
			const float Low = FMath::Min(FMath::Min(float(A.Z), float(B.Z)), FMath::Min(float(A2.Z), float(B2.Z)));
			Y = float(H.Y) - 0.5f * float(A.Y + B.Y);
			Z = float(H.Z) - Low;
		};
		Ends(M->ClipStart, S.H0, S.Start0Y, S.Start0Z);
		Ends(M->ClipEnd, S.H1, S.End1Y, S.End1Z);
		// a clip that travels off to one side (ClimbOnto): turned so its travel runs straight ahead (mesh +Y)
		const FVector2D T(S.H1.X - S.H0.X, S.H1.Y - S.H0.Y);
		if (Key >= 90 && T.Size() > 25.f && FMath::Abs(FMath::Atan2(T.X, T.Y)) > FMath::DegreesToRadians(25.f)) S.AlignYaw = FMath::Atan2(T.X, T.Y);
		UE_LOG(LogTemp, Log, TEXT("JEEBCLIMBCLIP %s len %.2f hip mean z %.1f y %.1f hands ahead %.1f start hip z %.1f end hip z %.1f y %.1f travel %.0f,%.0f,%.0f align %.0f deg"),
			WallRow(Key), C->Len, S.MeanHipZ, S.MeanHipY, S.HandFwd, S.Start0Z, S.End1Z, S.End1Y, float(S.H1.X - S.H0.X), float(S.H1.Y - S.H0.Y), float(S.H1.Z - S.H0.Z), FMath::RadiansToDegrees(S.AlignYaw));
	}
	return WallStats.Add(Key, S);
}

int32 FKnightAnimator::WallKey(float Dt, const FInput& In)
{
	switch (In.WallMode)
	{
	case 1:
	{
		const bool bFresh = !(StateKey >= 80 && StateKey <= 88);
		if (bFresh) { WallGrabT = 0.f; WallMoveKey = 81; }
		WallGrabT += Dt;
		const float Up = In.WallUpV, Rt = In.WallRightV;
		const float Sp = FVector2D(Up, Rt).Size();
		if (Sp > 6.f)
		{
			// the clip by the main way he moves; a diagonal plays the bigger part (never two clips averaged), and switches only when the other way clearly leads
			const int32 Cand = FMath::Abs(Up) >= FMath::Abs(Rt) ? (Up > 0.f ? 81 : 82) : (Rt > 0.f ? 84 : 83);
			const bool bSameAxis = (Cand <= 82) == (WallMoveKey <= 82);
			const float CurAxis = WallMoveKey <= 82 ? FMath::Abs(Up) : FMath::Abs(Rt);
			const float CandAxis = Cand <= 82 ? FMath::Abs(Up) : FMath::Abs(Rt);
			if (Cand != WallMoveKey && (bSameAxis || CandAxis > 1.25f * CurAxis)) WallMoveKey = Cand;
		}
		const JeebAnim::FMove* G = Moves.Find(TEXT("WallGrab"));
		const float GrabLen = G ? G->ClipEnd - G->ClipStart : 0.f;
		(void)GrabLen;   // (the grab plays the climbing loop itself, blended in: the jump-to-hang clip pushed the hands into the rock and then peeled him off it)
		return WallMoveKey;
	}
	case 2:
		return FMath::Abs(In.WallJump.Y) >= FMath::Abs(In.WallJump.X) ? (In.WallJump.Y >= 0.f ? 85 : 86) : (In.WallJump.X > 0.f ? 88 : 87);
	// (after the ledge pull-up or the climb onto he stands still on top for a moment, ActHold: the clips end crouched, so that is rest, blended up out of the crouch)
	case 3: return In.ActHold > 0.f && WallModeT >= In.ActDur ? 0 : 89;
	case 4: return 90;
	case 5: return In.ActHold > 0.f && WallModeT >= In.ActDur ? 0 : 91;
	default: return 81;
	}
}

static constexpr float VaultTipDeg = 24.f;   // how far forward the vault's body tips over the rail and into the landing

void FKnightAnimator::WallPlay(int32 Key, const FInput& In, FPose& B, float& OutBlend)
{
	const TCHAR* Row = WallRow(Key);
	const JeebAnim::FMove* M = Moves.Find(Row);
	if (!M) return;
	const FWallStat& S = WallStat(Key);
	const float Len = FMath::Max(M->ClipEnd - M->ClipStart, 0.05f);
	if (ActionT <= 0.f) WallActFeet = CurFeet;
	float T;
	if (Key == 80) T = M->ClipStart + FMath::Min(ActionT, Len);
	else if (Key <= 84)
	{
		// the stride goes with the distance climbed (ClipHit: cm per cycle), and holds still when he does
		const float Axis = Key <= 82 ? FMath::Abs(In.WallUpV) : FMath::Abs(In.WallRightV);
		WallPhase = FMath::Fmod(WallPhase + Axis * PlayDt / FMath::Max(M->ClipHit, 10.f), 1.f);
		T = M->ClipStart + WallPhase * Len;
	}
	else T = M->ClipStart + FMath::Clamp(ActionT / FMath::Max(In.ActDur, 0.05f), 0.f, 1.f) * Len;
	PlayAt(Row, T, 1000.f, B, &OutBlend);
	if (Key <= 84)
	{
		// centred on the capsule, hips at a set height (how far from the rock: WallFrame)
		B.Hip.Z += 100.f - S.MeanHipZ;
		B.Hip.Y -= S.MeanHipY;
		return;
	}
	// The scripted moves: the clip's own path first, the capsule's by the end
	const float E = EaseW(ActionT / FMath::Max(In.ActDur, 0.05f));
	FVector Rel = B.Hip - S.H0;
	const FQuat Q = FQuat(FVector::UpVector, S.AlignYaw);
	if (S.AlignYaw != 0.f) { TurnInMesh(B, Hips, Q); Rel = Q.RotateVector(Rel); }
	const FVector Dw = CurFeet - WallActFeet;
	const FVector Dl = (WallTilt.Inverse() * FQuat(FRotator(0.f, CurYaw + YawNow, 0.f)).Inverse()).RotateVector(Dw);
	const FVector Dm(-Dl.Y, Dl.X, Dl.Z);   // (mesh: +Y ahead, -X his right)
	FVector A0, A1;
	if (Key <= 88) A0 = A1 = FVector(S.H0.X, 0.f, 100.f);                                         // a climb jump: on the rock before and after
	else if (Key == 89) { A0 = FVector(S.H0.X, 0.f, 100.f); A1 = FVector(S.H1.X, S.End1Y, S.End1Z); }   // the ledge: from the rock to standing on top
	else { A0 = FVector(0.f, S.Start0Y, S.Start0Z); A1 = FVector(0.f, S.End1Y, S.End1Z); }          // a mantle: standing to standing
	B.Hip = FMath::Lerp(A0, A1, E) + (Rel - Dm) * (1.f - E);
	if (Key == 90)
	{
		// The clip sits back with its legs out in front as the hips pass the rail: tip the whole body forward about the hips from there to the landing,
		// so his chest comes over his boots (the legs swing back under him; Sole lifts him if a boot would sink)
		const float U = ActionT / FMath::Max(In.ActDur, 0.05f);
		const float Tip = VaultTipDeg * FMath::SmoothStep(0.3f, 0.55f, U) * (1.f - FMath::SmoothStep(0.8f, 1.f, U));
		const FVector Head0 = BonePos(B, Head) - B.Hip;
		if (Tip > 0.01f) TurnInMesh(B, Hips, FQuat(FVector(1.f, 0.f, 0.f), FMath::DegreesToRadians(-Tip)));
		if (In.bShotLog)
		{
			const FVector Head1 = BonePos(B, Head) - B.Hip;
			UE_LOG(LogTemp, Log, TEXT("JEEBVAULT u %.2f tip %.0f head ahead of hips %.0f -> %.0f cm"), U, Tip, Head0.Y, Head1.Y);
		}
		if (In.bMantleEdge) PlantHands(In, B);
	}
	// the ledge and the mantles are full-body clips where a weapon would go through the obstacle: not on these (stowed for the climb-up)
}

// The vault clip's hands only touch down in the part where he leans right back (cut off): here they come down onto the fence top, a hand either side,
// from just after the take-off until his hips are over it, eased in and out, the elbows back. Mesh frame: +Y ahead, -X his right, Z up from his feet.
void FKnightAnimator::PlantHands(const FInput& In, FPose& B) const
{
	const float U = ActionT / FMath::Max(In.ActDur, 0.05f);
	const float W = FMath::SmoothStep(0.06f, 0.2f, U) * (1.f - FMath::SmoothStep(0.42f, 0.6f, U));
	if (W <= 0.001f) return;
	const FVector G = FQuat(FRotator(0.f, CurYaw + YawNow, 0.f)).UnrotateVector(In.WallPoint - CurFeet);   // (ahead, right, up)
	const FVector Rail(-G.Y, G.X, G.Z);
	const int32 Arms[6] = { LArm, LFore, LHand, RArm, RFore, RHand };
	FQuat Was[6];
	for (int32 i = 0; i < 6; ++i) Was[i] = B.Rot[L.Bone[Arms[i]]];
	const FVector LeftAt = Rail + FVector(18.f, 2.f, 8.f), RightAt = Rail + FVector(-18.f, 2.f, 8.f);   // (the wrist just above the palm on the rail)
	L.ReachTo(B, LArm, LFore, LHand, LeftAt, FVector(0.5f, -1.f, 0.f));
	L.ReachTo(B, RArm, RFore, RHand, RightAt, FVector(-0.5f, -1.f, 0.f));
	if (In.bShotLog)
		UE_LOG(LogTemp, Log, TEXT("JEEBVAULT u %.2f w %.2f rail ahead %.0f up %.0f, wrists off it %.1f %.1f cm"), U, W, Rail.Y, Rail.Z, float(FVector::Dist(BonePos(B, LHand), LeftAt)), float(FVector::Dist(BonePos(B, RHand), RightAt)));
	for (int32 i = 0; i < 6; ++i) { FQuat& R = B.Rot[L.Bone[Arms[i]]]; R = FQuat::Slerp(Was[i], R, W); }
}

// The frame the pose is drawn in: on the rock tilted about the hips to lie along it, and moved so the hands are on it
FTransform FKnightAnimator::WallFrame(const FInput& In, const FTransform& Frame, float Dt)
{
	const bool bWall = StateKey >= 80 && StateKey <= 88 && In.WallMode != 0;
	const bool bLedge = StateKey == 89 && In.WallMode != 0;
	FQuat TiltWant = FQuat::Identity;
	float Keep = 1.f;
	if (bLedge) Keep = 1.f - EaseW(ActionT / FMath::Max(In.ActDur, 0.05f));
	if (bWall || bLedge)
	{
		FVector UpL = Frame.GetRotation().UnrotateVector(RockUp(In.WallN));
		UpL.Y = 0.f;
		if (UpL.Normalize()) TiltWant = FQuat::Slerp(FQuat::Identity, FQuat::FindBetweenNormals(FVector::UpVector, UpL), Keep);
	}
	WallTilt = FQuat::Slerp(WallTilt, TiltWant, FMath::Min(1.f, 8.f * Dt));
	WallExitLeft = FMath::Max(0.f, WallExitLeft - Dt);
	BackLineW = FMath::FInterpConstantTo(BackLineW, (StateKey >= 80 && StateKey <= 91) ? 1.f : 0.f, Dt, 4.f);   // (in and out over a quarter of a second)
	const FVector HpG(Final.Hip.Y, -Final.Hip.X, Final.Hip.Z);   // the hips in the game's frame (ahead, right, up)
	WallRel = FTransform(-HpG) * FTransform(WallTilt) * FTransform(HpG);
	FTransform Out = WallRel * Frame;
	FVector OffWant = FVector::ZeroVector;
	if (bWall || bLedge)
	{
		const FVector HipW = Frame.TransformPosition(FVector(0.f, HpG.Y, HpG.Z));   // (the hips' mean spot over the stride: the clip's own sway in and out stays)
		const float D = FVector::DotProduct(HipW - In.WallPoint, In.WallN);
		OffWant = -In.WallN * (D - (WallStat(StateKey).HandFwd + 6.f)) * Keep;   // (hands on the rock, with room for the fingers)
	}
	WallOff = FMath::Lerp(WallOff, OffWant, FMath::Min(1.f, 10.f * Dt));
	Out.AddToTranslation(WallOff);
	ToWorld = Out;
	if (In.bShotLog && In.WallMode != 0) WallLog(In, Dt);
	return Out;
}

// Test pictures: how close hands and feet are to the rock, how fast the slowest of each slides along it (planted ones should be still)
void FKnightAnimator::WallLog(const FInput& In, float Dt)
{
	const int32 Ix[4] = { LHand, RHand, LFoot, RFoot };
	float Dist[4], Slip[4];
	for (int32 i = 0; i < 4; ++i)
	{
		const FVector P = BoneAt(Ix[i]);
		Dist[i] = FVector::DotProduct(P - In.WallPoint, In.WallN);
		const FVector Mv = FVector::VectorPlaneProject(P - WallLogPrev[i], In.WallN);
		if (Dt > 0.f) WallLogSlip[i] = FMath::Min(WallLogSlip[i], float(Mv.Size()) / Dt);
		Slip[i] = WallLogSlip[i];
		WallLogPrev[i] = P;
	}
	WallLogT += Dt;
	if (WallLogT < 0.1f) return;
	WallLogT = 0.f;
	for (float& Sl : WallLogSlip) Sl = 1e6f;
	const float Tilt = FMath::RadiansToDegrees(WallTilt.GetAngle());
	UE_LOG(LogTemp, Log, TEXT("JEEBCLIMBPOSE key %d t %.2f tilt %.0f hands %.1f %.1f feet %.1f %.1f slipH %.0f slipF %.0f up %.0f right %.0f stam %.2f off %.1f"),
		StateKey, ActionT, Tilt, Dist[0], Dist[1], Dist[2], Dist[3], FMath::Min(Slip[0], Slip[1]), FMath::Min(Slip[2], Slip[3]), In.WallUpV, In.WallRightV, In.WallStamina, float(WallOff.Size()));
}

// On the rock the clips' hands and boots went into the stone (owner, round 4: gauntlets sunk to the wrist, a boot and shin in the face): the clips were
// made on a flat wall at another distance, and the wrist and ankle bones are not where the fingers and toes are. Each hand and boot is checked against
// the surface traced under it (In.LimbP / LimbN, the rock's plane where nothing was found): the points along the gauntlet to the fingertips and along the
// boot to the toe, each with its thickness. Any part inside comes out to rest 1.5 cm off it; one near the rock (within 12 cm) is drawn onto it, so planted
// hands and toes touch; one swinging to the next hold is left alone. The body first eases off the rock by part of what the hands need, so the arms are
// not crushed, then two-bone reaches place each limb. Eased in on the rock, out as he leaves it.
void FKnightAnimator::ClimbIK(const FInput& In, float Dt)
{
	const bool bOn = StateKey >= 80 && StateKey <= 88 && (In.WallMode == 1 || In.WallMode == 2);
	const bool bLedgeFeet = StateKey == 89 && ActionT < 0.45f * In.ActDur;   // (pulling up over the top the boots are still on the face for a while; the hands are on top, not on it)
	ClimbIKW = FMath::FInterpConstantTo(ClimbIKW, (bOn || bLedgeFeet) ? 1.f : 0.f, Dt, 1.f / 0.2f);
	if (ClimbIKW <= 0.001f) return;
	static constexpr float Gap = 1.5f, Near = 12.f;
	auto ToMesh = [&](const FVector& W) { const FVector G = ToWorld.InverseTransformPosition(W); return FVector(-G.Y, G.X, G.Z); };
	auto DirToMesh = [&](const FVector& W) { const FVector G = ToWorld.InverseTransformVectorNoScale(W); return FVector(-G.Y, G.X, G.Z).GetSafeNormal(); };
	struct FLimb { int32 Up, Mid, End; FVector Hint; };
	const FLimb Limbs[4] = {
		{ LArm, LFore, LHand, FVector(0.8f, 0.f, -1.f) }, { RArm, RFore, RHand, FVector(-0.8f, 0.f, -1.f) },
		{ LUp, LLeg, LFoot, FVector(0.3f, 1.f, 0.f) }, { RUp, RLeg, RFoot, FVector(-0.3f, 1.f, 0.f) } };
	if (bOn)   // (easing out after he has left the rock, the last surfaces are kept: the input has none then)
		for (int32 i = 0; i < 4; ++i) { IKSurfP[i] = In.bLimbHit[i] ? In.LimbP[i] : In.WallPoint; IKSurfN[i] = In.bLimbHit[i] ? In.LimbN[i] : In.WallN; }
	FVector PlaneP[4], PlaneN[4];
	for (int32 i = 0; i < 4; ++i) { PlaneP[i] = ToMesh(IKSurfP[i]); PlaneN[i] = DirToMesh(IKSurfN[i]); }
	// how far the limb's nearest part is off its surface (minus its thickness): under 0 is inside the rock
	auto Clear = [&](int32 i) -> float
	{
		const FVector N = PlaneN[i], P = PlaneP[i];
		auto S = [&](const FVector& X, float R) { return float(FVector::DotProduct(X - P, N)) - R; };
		if (i < 2)
		{
			const FVector W = BonePos(Final, Limbs[i].End), Dir = (W - BonePos(Final, Limbs[i].Mid)).GetSafeNormal();
			return FMath::Min3(S(W, 5.f), S(W + Dir * 9.f, 4.5f), S(W + Dir * 17.f, 3.f));   // (wrist, knuckles, fingertips)
		}
		const int32 Toe = i == 2 ? LToe : RToe;
		const FVector A = BonePos(Final, Limbs[i].End), T = BonePos(Final, Toe), Tip = T + (T - A).GetSafeNormal() * 8.f;
		return FMath::Min3(S(A, 7.f), S(T, 5.f), S(Tip, 4.f));   // (ankle, ball, toe cap)
	};
	auto Want = [&](float C) -> float   // how far to move it out along the surface's normal (negative: in)
	{
		if (C < Gap) return Gap - C;
		const float Pull = 1.f - FMath::SmoothStep(6.f, Near, C);
		return -(C - Gap) * Pull;
	};
	float Before[4], Move[4];
	for (int32 i = 0; i < 4; ++i) { Before[i] = Clear(i); Move[i] = Want(Before[i]); }
	// the body off the rock by part of what the hands need, so the reaches aren't all elbows
	const float HandsOut = StateKey == 89 ? 0.f : 0.5f * (FMath::Max(Move[0], 0.f) + FMath::Max(Move[1], 0.f));
	if (HandsOut > 0.f) Final.Hip += 0.6f * HandsOut * ClimbIKW * PlaneN[0];
	for (int32 i = 0; i < 4; ++i)
	for (int32 Pass = 0; Pass < 3; ++Pass)   // (the reach moves the wrist or ankle; keeping the hand's turn moves the fingertips a little: a couple more passes settle it)
	{
		if (i < 2 && StateKey == 89) break;
		const float M = Want(Clear(i)) * ClimbIKW;
		if (FMath::Abs(M) < 0.3f) break;
		const FLimb& Lb = Limbs[i];
		FQuat EndRot;
		BonePos(Final, Lb.End, &EndRot);
		const FVector Target = BonePos(Final, Lb.End) + PlaneN[i] * M;
		L.ReachTo(Final, Lb.Up, Lb.Mid, Lb.End, Target, Lb.Hint);
		// (the hand and boot keep their turn in the world: flat on the rock as the clip had it)
		FQuat NowRot;
		BonePos(Final, Lb.End, &NowRot);
		TurnInMesh(Final, Lb.End, EndRot * NowRot.Inverse());
	}
	if (In.bShotLog)
	{
		WallIKLogT += Dt;
		if (WallIKLogT >= 0.1f)
		{
			WallIKLogT = 0.f;
			UE_LOG(LogTemp, Log, TEXT("JEEBCLIMBIK key %d w %.2f traced %d%d%d%d clear before hands %.1f %.1f feet %.1f %.1f, after hands %.1f %.1f feet %.1f %.1f, body out %.1f"),
				StateKey, ClimbIKW, In.bLimbHit[0] ? 1 : 0, In.bLimbHit[1] ? 1 : 0, In.bLimbHit[2] ? 1 : 0, In.bLimbHit[3] ? 1 : 0,
				Before[0], Before[1], Before[2], Before[3], Clear(0), Clear(1), Clear(2), Clear(3), 0.6f * HandsOut * ClimbIKW);
		}
	}
}

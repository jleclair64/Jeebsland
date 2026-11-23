#include "Frogs.h"
#include "Bushes.h"
#include "Terrain.h"
#include "WorldMap.h"
#include "GroundMask.h"
#include "DayNight.h"
#include "Fill.h"
#include "JeebWater.h"
#include "EngineUtils.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundWaveProcedural.h"
#include "Sound/SoundAttenuation.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"

extern TAutoConsoleVariable<int32> CVarCritters;   // Fireflies.cpp
static TAutoConsoleVariable<float> CVarFrogDensity(TEXT("jeeb.Frog.Density"), 1.f, TEXT("Frog groups against the usual spread (0 none, 1 as placed, 2 twice as many; squares are worked out again)"));
static TAutoConsoleVariable<float> CVarFrogScare(TEXT("jeeb.Frog.Scare"), 420.f, TEXT("cm: how near the player must come to make a sitting frog hop away (a running player scares from half as far again)"));
static TAutoConsoleVariable<float> CVarFrogCroak(TEXT("jeeb.Frog.Croak"), 1.f, TEXT("How loud the night croaks are (0 none)"));
static TAutoConsoleVariable<float> CVarFrogSize(TEXT("jeeb.Frog.Size"), 0.17f, TEXT("A frog's length in metres-ish: the model is 100 cm long, scaled by this (live)"));
static TAutoConsoleVariable<float> CVarFrogYaw(TEXT("jeeb.Frog.ModelYaw"), 90.f, TEXT("degrees: which way the model's nose points in its own frame (Meshy's frog; live)"));
static TAutoConsoleVariable<int32> CVarFrogTurn(TEXT("jeeb.Frog.Turn"), 1, TEXT("1: sitting frogs turn their heads now and then; 0: they keep still (test pictures)"));

namespace
{
	constexpr float CellSize = 2500.f;
	constexpr float BuildReach = 7000.f;    // squares this near the player are worked out
	constexpr float ForgetReach = 14000.f;
	constexpr float WakeReach = 5500.f;     // a frog this near the player is a mesh
	constexpr float SleepReach = 7000.f;
	inline float TurnToward(float Yaw, float To, float Dt, float K) { return Yaw + FMath::FindDeltaAngleDegrees(Yaw, To) * (1.f - FMath::Exp(-K * Dt)); }
	inline int64 KeyOf(int32 I, int32 J) { return (int64(I) << 32) ^ int64(uint32(J)); }
	inline int64 KeyAt(float X, float Y) { return KeyOf(FMath::FloorToInt(X / CellSize), FMath::FloorToInt(Y / CellSize)); }
	inline float Edge(float X, float Y)
	{
		const float E = JeebWorld::WaterEdge(X, Y);
		const float Pd = FVector2D::Distance(FVector2D(X, Y), JeebTerrain::PondCenter()) - JeebTerrain::PondWaterR;
		return FMath::Min(E, Pd);
	}
}

AFrogs::AFrogs()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void AFrogs::BeginPlay()
{
	Super::BeginPlay();
	if (!GetActorLocation().IsNearlyZero()) SetActorLocation(FVector::ZeroVector);
	Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Critters/SM_Frog.SM_Frog"));
	if (!Mesh) UE_LOG(LogTemp, Warning, TEXT("JEEBFROG /Game/Critters/SM_Frog is missing: run Tools/BuildFrog.ps1. No frogs."));
	Att = NewObject<USoundAttenuation>(this);
	Att->Attenuation.bAttenuate = true;
	Att->Attenuation.bSpatialize = true;
	Att->Attenuation.AttenuationShape = EAttenuationShape::Sphere;
	Att->Attenuation.AttenuationShapeExtents = FVector(300.f, 0.f, 0.f);
	Att->Attenuation.FalloffDistance = 2800.f;
	Att->Attenuation.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
}

// The lily pads (plain ones) that the bushes put on the water
void AFrogs::ReadPads()
{
	bPadsRead = true;
	for (TActorIterator<ABushes> It(GetWorld()); It; ++It)
	{
		TArray<UInstancedStaticMeshComponent*> Comps;
		It->GetComponents(Comps);
		for (UInstancedStaticMeshComponent* C : Comps)
		{
			if (!C || !C->GetStaticMesh() || C->GetStaticMesh()->GetName() != TEXT("SM_LilyPad")) continue;
			for (int32 i = 0; i < C->GetInstanceCount(); ++i)
			{
				FTransform T;
				if (!C->GetInstanceTransform(i, T, true)) continue;
				const FVector L = T.GetLocation();
				FPad P;
				P.Top = FVector(L.X, L.Y, L.Z + 3.f * T.GetScale3D().X);
				P.Cell = KeyAt(L.X, L.Y);
				PadsByCell.FindOrAdd(P.Cell).Add(Pads.Add(P));
			}
		}
	}
	UE_LOG(LogTemp, Log, TEXT("JEEBFROG %d lily pads to sit on"), Pads.Num());
}

float AFrogs::GroundZ(float X, float Y, bool& bWet) const
{
	const float G = JeebTerrain::DrawnHeight(X, Y);
	float S;
	bWet = JeebWater::SurfaceAt(X, Y, S) && S > G + 4.f;
	return bWet ? S - 1.f : G;
}

// A dry spot on a bank in this square: within a few metres of the water's edge, low above it, not on a road, a roof or rock
bool AFrogs::BankSpot(FRandomStream& R, float X0, float Y0, FVector& Out)
{
	for (int32 Try = 0; Try < 7; ++Try)
	{
		FVector2D P(X0 + R.FRand() * CellSize, Y0 + R.FRand() * CellSize);
		if (P.X < JeebWorld::MinX || P.X > JeebWorld::MaxX || P.Y < JeebWorld::MinY || P.Y > JeebWorld::MaxY) return false;
		if (Edge(P.X, P.Y) > 3200.f) continue;   // (far from every water: the square has no bank)
		const float Want = R.FRandRange(110.f, 380.f);
		bool bOk = false;
		for (int32 k = 0; k < 6 && !bOk; ++k)
		{
			const float E = Edge(P.X, P.Y);
			if (FMath::Abs(E - Want) < 40.f) { bOk = true; break; }
			const float Gx = Edge(P.X + 100.f, P.Y) - Edge(P.X - 100.f, P.Y), Gy = Edge(P.X, P.Y + 100.f) - Edge(P.X, P.Y - 100.f);
			FVector2D Gr(Gx, Gy);
			if (Gr.SizeSquared() < 0.04f * 40000.f) break;
			Gr.Normalize();
			P -= Gr * FMath::Clamp(E - Want, -2000.f, 2000.f);
		}
		if (!bOk) continue;
		const float G = JeebTerrain::DrawnHeight(P.X, P.Y);
		float S;
		if (JeebWater::SurfaceAt(P.X, P.Y, S) && (G < S + 6.f || G > S + 170.f)) continue;
		if (JeebWorld::InWater(P.X, P.Y) || JeebTerrain::NoGroundCover(P.X, P.Y)) continue;
		if (JeebTerrain::PathClearance(P.X, P.Y) < 150.f || JeebTerrain::BuildingNearness(P.X, P.Y) > 0.f || JeebFill::Covered(P.X, P.Y)) continue;
		const JeebGround::FMask M = JeebGround::At(P.X, P.Y, G, JeebGround::Upness(P.X, P.Y), false);
		if (M.Rock > 0.6f || M.Snow > 0.1f || M.Worn > 0.5f) continue;
		Out = FVector(P.X, P.Y, G);
		return true;
	}
	return false;
}

void AFrogs::AddFrog(int64 Cell, const FVector& Home, int32 Pad, FRandomStream& R)
{
	FFrog F;
	F.Cell = Cell; F.Home = Home; F.Pos = Home; F.HomePad = Pad; F.Pad = Pad;
	F.HomeYaw = F.Yaw = F.TurnTo = R.FRandRange(-180.f, 180.f);
	F.Seed = R.FRand();
	F.Idle = R.FRandRange(1.5f, 6.f);
	if (Pad >= 0) Pads[Pad].bTaken = true;
#if !UE_BUILD_SHIPPING
	if (FParse::Param(FCommandLine::Get(), TEXT("JeebShot"))) UE_LOG(LogTemp, Log, TEXT("JEEBFROG spot %.0f %.0f %.0f (HeightAt %.0f) yaw %.0f %s"), Home.X, Home.Y, Home.Z, JeebTerrain::HeightAt(Home.X, Home.Y), F.Yaw, Pad >= 0 ? TEXT("on a pad") : TEXT("on the bank"));
#endif
	Frogs.Add(F);
}

void AFrogs::WorkOutCell(int32 I, int32 J)
{
	const int64 Key = KeyOf(I, J);
	Done.Add(Key);
	const float Dens = CVarFrogDensity.GetValueOnGameThread();
	if (Dens <= 0.f) return;
	FRandomStream R(int32(HashCombine(GetTypeHash(I * 6151 + 3), GetTypeHash(J * 92821 + 11))) ^ 0xF506);
	const float X0 = I * CellSize, Y0 = J * CellSize;
	// a group on the bank: 2 or 3 frogs a step or two apart on dry ground
	if (R.FRand() < 0.28f * Dens)
	{
		FVector A;
		if (BankSpot(R, X0, Y0, A))
		{
			const int32 N = R.FRand() < 0.45f ? 3 : 2;
			int32 Placed = 0;
			for (int32 n = 0; n < N + 2 && Placed < N; ++n)
			{
				const float Ang = R.FRandRange(0.f, 2.f * PI), Rad = n == 0 ? 0.f : R.FRandRange(35.f, 120.f);
				const float X = A.X + FMath::Cos(Ang) * Rad, Y = A.Y + FMath::Sin(Ang) * Rad;
				bool bWet;
				const float Z = GroundZ(X, Y, bWet);
				float Sw;
				if (bWet || FMath::Abs(Z - A.Z) > 35.f || (JeebWater::SurfaceAt(X, Y, Sw) && Z < Sw + 8.f)) continue;   // (dry, not at the waterline)
				AddFrog(Key, FVector(X, Y, Z), -1, R);
				{   // bank frogs face the water, give or take
					const FVector2D Gr(Edge(X + 100.f, Y) - Edge(X - 100.f, Y), Edge(X, Y + 100.f) - Edge(X, Y - 100.f));
					if (Gr.SizeSquared() > 1.f) Frogs.Last().HomeYaw = Frogs.Last().Yaw = Frogs.Last().TurnTo = FMath::RadiansToDegrees(FMath::Atan2(-Gr.Y, -Gr.X)) + R.FRandRange(-35.f, 35.f);
				}
				++Placed;
			}
			if (Placed) ++Groups;
		}
	}
	// a group on lily pads: one pad and the one or two beside it
	if (const TArray<int32>* L = PadsByCell.Find(Key))
		for (int32 PadI : *L)
		{
			if (Pads[PadI].bTaken || R.FRand() > 0.07f * Dens) continue;
			TArray<int32> Mates;
			for (int32 m = 0; m < Pads.Num() && Mates.Num() < 2; ++m)
			{
				if (m == PadI || Pads[m].bTaken) continue;
				if (FVector::DistSquared2D(Pads[m].Top, Pads[PadI].Top) < FMath::Square(380.f) && R.FRand() < 0.8f) Mates.Add(m);
			}
			AddFrog(Key, Pads[PadI].Top, PadI, R);
			for (int32 m : Mates) AddFrog(Key, Pads[m].Top, m, R);
			++Groups;
		}
}

void AFrogs::Wake(FFrog& F)
{
	if (!Mesh || F.Comp) return;
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetStaticMesh(Mesh);
	C->SetMobility(EComponentMobility::Movable);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCanEverAffectNavigation(false);
	C->bAffectDistanceFieldLighting = false;
	C->bAffectDynamicIndirectLighting = false;
	C->SetCastShadow(false);
	C->SetupAttachment(GetRootComponent());
	C->RegisterComponent();
	F.Comp = C;
	if (F.State == EState::Gone) C->SetVisibility(false);
}

void AFrogs::Sleep(FFrog& F)
{
	if (F.Comp) { F.Comp->DestroyComponent(); F.Comp = nullptr; }
	// out of sight, so back on its spot
	if (F.Pad != F.HomePad && F.Pad >= 0) Pads[F.Pad].bTaken = false;
	if (F.HomePad >= 0) Pads[F.HomePad].bTaken = true;
	F.Pad = F.HomePad; F.Pos = F.Home; F.Yaw = F.TurnTo = F.HomeYaw; F.State = EState::Sit; F.Croak = 0.f; F.Away = 0.f;
}

void AFrogs::StartHop(FFrog& F, const FVector& Pl)
{
	FVector2D Away(F.Pos.X - Pl.X, F.Pos.Y - Pl.Y);
	if (Away.SizeSquared() < 1.f) Away = FVector2D(FMath::Cos(F.Seed * 6.28f), FMath::Sin(F.Seed * 6.28f));
	Away.Normalize();
	FRandomStream R(int32(F.Seed * 1.0e6f) + int32(GFrameCounter & 0xffff));
	FVector Best = F.Pos; float BestScore = -1.0e9f; int32 BestPad = -1; bool bBestWet = false;
	// another lily pad beside it, away from the player
	if (F.Pad >= 0)
		for (int32 m = 0; m < Pads.Num(); ++m)
		{
			if (m == F.Pad || Pads[m].bTaken) continue;
			const FVector2D D(Pads[m].Top.X - F.Pos.X, Pads[m].Top.Y - F.Pos.Y);
			const float Len = D.Size();
			if (Len < 60.f || Len > 360.f) continue;
			const float Score = FVector2D::DotProduct(D / Len, Away) + R.FRandRange(0.f, 0.4f);
			if (Score > 0.3f && Score > BestScore) { BestScore = Score; Best = Pads[m].Top; BestPad = m; bBestWet = false; }
		}
	if (BestPad < 0)
		for (int32 k = 0; k < 8; ++k)
		{
			const float Ang = FMath::Atan2(Away.Y, Away.X) + FMath::DegreesToRadians(-100.f + 200.f * k / 7.f + R.FRandRange(-12.f, 12.f));
			const float Len = R.FRandRange(95.f, 170.f);
			const float X = F.Pos.X + FMath::Cos(Ang) * Len, Y = F.Pos.Y + FMath::Sin(Ang) * Len;
			bool bWet;
			const float Z = GroundZ(X, Y, bWet);
			if (JeebTerrain::BuildingNearness(X, Y) > 0.f || (!bWet && JeebTerrain::NoGroundCover(X, Y))) continue;
			if (!bWet && FMath::Abs(Z - F.Pos.Z) > 55.f) continue;
			float Score = FVector2D::DotProduct(FVector2D(FMath::Cos(Ang), FMath::Sin(Ang)), Away);
			if (bWet) Score += F.Pad >= 0 ? 0.5f : 0.9f;   // toward the water: a frog's way out
			if (Score > BestScore) { BestScore = Score; Best = FVector(X, Y, Z); bBestWet = bWet; BestPad = -1; }
		}
	if (BestScore < -1.0e8f) { Best = F.Pos + FVector(Away.X, Away.Y, 0.f) * 110.f; bool bW; Best.Z = GroundZ(Best.X, Best.Y, bW); bBestWet = bW; }
	if (F.Pad >= 0) Pads[F.Pad].bTaken = false;
	if (BestPad >= 0) Pads[BestPad].bTaken = true;
	F.Pad = BestPad;
	F.From = F.Pos; F.To = Best; F.bToWet = bBestWet;
	const float Dist = FVector::Dist2D(F.From, F.To);
	F.State = EState::Hop; F.T = 0.f; F.Dur = 0.42f + Dist / 700.f;
	F.TurnTo = FMath::RadiansToDegrees(FMath::Atan2(Best.Y - F.Pos.Y, Best.X - F.Pos.X));
}

void AFrogs::Place(FFrog& F, const FVector& Pl, float Dt)
{
	const float Speed = 1.f;
	(void)Speed;
	float Sx = 1.f, Sy = 1.f, Sz = 1.f, Pitch = 0.f, Roll = 0.f;
	const float DistP = FVector2D::Distance(FVector2D(F.Pos), FVector2D(Pl));
	F.T += Dt;
	switch (F.State)
	{
	case EState::Sit:
	{
		const float Br = FMath::Sin(GetWorld()->GetTimeSeconds() * (2.2f + F.Seed) + F.Seed * 20.f);
		Sz = 1.f + 0.025f * Br; Sx = Sy = 1.f - 0.012f * Br;
		F.Idle -= Dt;
		if (F.Idle <= 0.f && CVarFrogTurn.GetValueOnGameThread() != 0) { F.Idle = FMath::FRandRange(3.f, 9.f); F.TurnTo = F.Yaw + FMath::FRandRange(-55.f, 55.f); }
		F.Yaw = TurnToward(F.Yaw, F.TurnTo, Dt, 2.5f);
		float Scare = CVarFrogScare.GetValueOnGameThread() * (0.85f + 0.3f * F.Seed);
		if (APawn* P = UGameplayStatics::GetPlayerPawn(this, 0)) if (P->GetVelocity().SizeSquared2D() > 450.f * 450.f) Scare *= 1.5f;
		if (Scare > 0.f && DistP < Scare)
		{
			F.State = EState::Crouch; F.T = 0.f; F.Dur = 0.12f + 0.12f * F.Seed;
			F.TurnTo = FMath::RadiansToDegrees(FMath::Atan2(F.Pos.Y - Pl.Y, F.Pos.X - Pl.X));
		}
		break;
	}
	case EState::Crouch:
	{
		const float U = FMath::Clamp(F.T / F.Dur, 0.f, 1.f);
		Sz = 1.f - 0.2f * U; Sx = Sy = 1.f + 0.08f * U;
		F.Yaw = TurnToward(F.Yaw, F.TurnTo, Dt, 14.f);
		if (F.T >= F.Dur) StartHop(F, Pl);
		break;
	}
	case EState::Hop:
	{
		const float U = FMath::Clamp(F.T / F.Dur, 0.f, 1.f);
		F.Pos = FMath::Lerp(F.From, F.To, U);
		const float Arc = 18.f + 0.14f * FVector::Dist2D(F.From, F.To);
		F.Pos.Z += 4.f * U * (1.f - U) * Arc;
		F.Yaw = TurnToward(F.Yaw, F.TurnTo, Dt, 20.f);
		Pitch = FMath::Lerp(32.f, -38.f, U);                 // nose up as it leaves, down as it lands
		Sz = 1.f + 0.18f * FMath::Sin(U * PI); Sx = Sy = 1.f - 0.07f * FMath::Sin(U * PI);   // stretched in flight
		if (U >= 1.f) { F.Pos = F.To; F.State = F.bToWet ? EState::Sink : EState::Land; F.T = 0.f; F.Dur = F.bToWet ? 0.8f : 0.16f; }
		break;
	}
	case EState::Land:
	{
		const float U = FMath::Clamp(F.T / F.Dur, 0.f, 1.f);
		Sz = 0.82f + 0.18f * U; Sx = Sy = 1.08f - 0.08f * U;
		if (F.T >= F.Dur) { F.State = EState::Sit; F.T = 0.f; F.Idle = FMath::FRandRange(0.4f, 1.2f); F.TurnTo = F.Yaw; }
		break;
	}
	case EState::Sink:
	{
		const float U = FMath::Clamp(F.T / F.Dur, 0.f, 1.f);
		const float Late = FMath::Clamp((U - 0.25f) / 0.75f, 0.f, 1.f);   // sits a moment on the surface, then slips under
		Sz = 1.f - 0.5f * Late; Sx = Sy = 1.f - 0.4f * Late;
		F.Pos.Z = F.To.Z - 16.f * Late * Late;
		if (F.T >= F.Dur) { F.State = EState::Gone; F.T = 0.f; if (F.Comp) F.Comp->SetVisibility(false); }
		break;
	}
	case EState::Gone:
		F.Away += Dt;
		if (DistP > 2000.f && F.Away > 10.f) { const bool bC = F.Comp != nullptr; Sleep(F); if (bC) Wake(F); }   // back on its spot (nobody is looking)
		return;
	}
	if (F.State == EState::Sit || F.State == EState::Land)
	{
		// a frog that hopped away to dry land goes home too once the player is far off (and not watching)
		if (FVector::DistSquared(F.Pos, F.Home) > 1.f && DistP > 2000.f) { F.Away += Dt; if (F.Away > 10.f) { const bool bC = F.Comp != nullptr; Sleep(F); if (bC) Wake(F); return; } }
		else F.Away = 0.f;
	}
	if (F.Croak > 0.f)
	{
		F.Croak -= Dt;
		const float K = FMath::Sin(PI * FMath::Clamp(1.f - F.Croak / 0.7f, 0.f, 1.f));
		Sx *= 1.f + 0.1f * K; Sy *= 1.f + 0.1f * K; Sz *= 1.f + 0.05f * K;
	}
	if (F.Comp)
	{
		const float Base = CVarFrogSize.GetValueOnGameThread() * (0.85f + 0.3f * F.Seed);
		F.Comp->SetWorldTransform(FTransform(FRotator(Pitch, F.Yaw + CVarFrogYaw.GetValueOnGameThread(), Roll), F.Pos, FVector(Base * Sx, Base * Sy, Base * Sz)));
	}
}

// A soft "rib-bit": two pulsed bursts of a low note (a pulse train, as a frog's throat makes), 22 kHz mono, made when it is needed
void AFrogs::PlayCroak(const FVector& At)
{
	const float Loud = CVarFrogCroak.GetValueOnGameThread();
	if (Loud <= 0.f) return;
	constexpr int32 Rate = 22050;
	const float Len = 0.62f;
	const int32 N = int32(Len * Rate);
	TArray<int16> Pcm; Pcm.SetNumZeroed(N);
	const float F0 = FMath::FRandRange(135.f, 190.f);
	float Lp = 0.f;
	for (int32 n = 0; n < N; ++n)
	{
		const float T = float(n) / Rate;
		float Env = 0.f, Hz = F0;
		if (T < 0.2f) { Env = FMath::Pow(FMath::Sin(PI * T / 0.2f), 1.4f); Hz = F0 * (1.1f - 0.2f * T / 0.2f); }
		else if (T > 0.26f && T < 0.58f) { const float U = (T - 0.26f) / 0.32f; Env = 0.85f * FMath::Pow(FMath::Sin(PI * U), 1.2f); Hz = F0 * 0.9f * (1.f - 0.15f * U); }
		if (Env <= 0.f) continue;
		const float Ph = 2.f * PI * Hz * T;
		const float Pulse = 0.5f + 0.5f * FMath::Sin(2.f * PI * 52.f * T);                                  // the pulse train
		float V = FMath::Sin(Ph) + 0.55f * FMath::Sin(2.f * Ph + 0.6f) + 0.3f * FMath::Sin(3.f * Ph + 1.1f);   // a rounded, hollow note
		V *= Env * (0.45f + 0.55f * Pulse);
		Lp += (V - Lp) * 0.35f;                                                                              // take the edge off
		Pcm[n] = int16(FMath::Clamp(Lp * 9000.f, -30000.f, 30000.f));
	}
	USoundWaveProcedural* W = NewObject<USoundWaveProcedural>(this);
	W->SetSampleRate(Rate);
	W->NumChannels = 1;
	W->Duration = Len + 0.05f;
	W->SoundGroup = SOUNDGROUP_Default;
	W->bLooping = false;
	W->QueueAudio(reinterpret_cast<const uint8*>(Pcm.GetData()), Pcm.Num() * sizeof(int16));
	Croaks.Add(W);
	if (Croaks.Num() > 12) Croaks.RemoveAt(0);
	UGameplayStatics::PlaySoundAtLocation(this, W, At, 0.28f * Loud, FMath::FRandRange(0.9f, 1.25f), 0.f, Att);
}

void AFrogs::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	APawn* P = UGameplayStatics::GetPlayerPawn(this, 0);
	const bool bOn = Mesh && P && CVarCritters.GetValueOnGameThread() != 0 && CVarFrogDensity.GetValueOnGameThread() > 0.f;
	if (!bOn)
	{
		if (Frogs.Num() > 0 || Done.Num() > 0)
		{
			for (FFrog& F : Frogs) { if (F.Comp) F.Comp->DestroyComponent(); if (F.HomePad >= 0) Pads[F.HomePad].bTaken = false; }
			Frogs.Reset(); Done.Reset(); for (FPad& Pd : Pads) Pd.bTaken = false; Groups = 0;
		}
		return;
	}
	if (!bPadsRead) ReadPads();
	const FVector Pl = P->GetActorLocation();
	if (LastDensity != CVarFrogDensity.GetValueOnGameThread())
	{
		LastDensity = CVarFrogDensity.GetValueOnGameThread();
		for (FFrog& F : Frogs) if (F.Comp) F.Comp->DestroyComponent();
		Frogs.Reset(); Done.Reset(); for (FPad& Pd : Pads) Pd.bTaken = false; Groups = 0;
	}

	// squares near the player are worked out a few a frame; far ones forgotten (their frogs with them)
	const int32 I0 = FMath::FloorToInt((Pl.X - BuildReach) / CellSize), I1 = FMath::FloorToInt((Pl.X + BuildReach) / CellSize);
	const int32 J0 = FMath::FloorToInt((Pl.Y - BuildReach) / CellSize), J1 = FMath::FloorToInt((Pl.Y + BuildReach) / CellSize);
	int32 Budget = Done.Num() == 0 ? 400 : 3;
	const int32 Before = Frogs.Num();
	for (int32 i = I0; i <= I1 && Budget > 0; ++i)
		for (int32 j = J0; j <= J1 && Budget > 0; ++j)
			if (!Done.Contains(KeyOf(i, j))) { WorkOutCell(i, j); --Budget; }
	if (Frogs.Num() != Before) UE_LOG(LogTemp, Log, TEXT("JEEBFROG %d frogs in %d groups so far (%d squares worked out)"), Frogs.Num(), Groups, Done.Num());
	CellTimer += DeltaSeconds;
	if (CellTimer > 8.f)
	{
		CellTimer = 0.f;
		for (int32 i = Frogs.Num() - 1; i >= 0; --i)
			if (FVector2D::Distance(FVector2D(Frogs[i].Home), FVector2D(Pl)) > ForgetReach)
			{
				if (Frogs[i].Comp) Frogs[i].Comp->DestroyComponent();
				if (Frogs[i].HomePad >= 0) Pads[Frogs[i].HomePad].bTaken = false;
				if (Frogs[i].Pad >= 0) Pads[Frogs[i].Pad].bTaken = false;
				Frogs.RemoveAtSwap(i);
			}
		for (auto It = Done.CreateIterator(); It; ++It)
		{
			const int32 I = int32(*It >> 32), J = int32(uint32(*It & 0xffffffff));
			if (FMath::Abs((I + 0.5f) * CellSize - Pl.X) > ForgetReach || FMath::Abs((J + 0.5f) * CellSize - Pl.Y) > ForgetReach) It.RemoveCurrent();
		}
	}

	// wake the near frogs into meshes, put the far ones to sleep, and run the near ones
	for (FFrog& F : Frogs)
	{
		const float D = FVector2D::Distance(FVector2D(F.Pos), FVector2D(Pl));
		if (!F.Comp && D < WakeReach) Wake(F);
		else if (F.Comp && D > SleepReach) { Sleep(F); continue; }
		if (F.Comp) Place(F, Pl, DeltaSeconds);
	}

	// night croaks: now and then one of the sitting frogs within 35 m sings
	CroakTimer -= DeltaSeconds;
	if (CroakTimer <= 0.f)
	{
		CroakTimer = FMath::FRandRange(4.f, 11.f);
		if (ADayNight::Night > 0.55f)
		{
			TArray<FFrog*> Near;
			for (FFrog& F : Frogs) if (F.Comp && F.State == EState::Sit && FVector2D::Distance(FVector2D(F.Pos), FVector2D(Pl)) < 3500.f) Near.Add(&F);
			if (Near.Num())
			{
				FFrog* F = Near[FMath::RandRange(0, Near.Num() - 1)];
				F->Croak = 0.7f;
				PlayCroak(F->Pos + FVector(0.f, 0.f, 6.f));
				UE_LOG(LogTemp, Verbose, TEXT("JEEBFROG croak"));
			}
		}
	}
}

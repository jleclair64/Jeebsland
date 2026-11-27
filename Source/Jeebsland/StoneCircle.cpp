#include "StoneCircle.h"
#include "KnightGameState.h"
#include "KnightCharacter.h"
#include "SkillComponent.h"
#include "Inventory.h"
#include "Loot.h"
#include "QuestData.h"
#include "JeebTuning.h"
#include "Terrain.h"
#include "WorldMap.h"
#include "OpenWorld.h"
#include "OakForest.h"
#include "PvpZones.h"
#include "ProceduralMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

DEFINE_LOG_CATEGORY_STATIC(LogJeebStone, Log, All);

// ------------------------------------------------------------------ the circles and the schedule
namespace JeebStones
{
	const TArray<FCircle>& Circles()
	{
		static const TArray<FCircle> All = []()
		{
			TArray<FCircle> Out;
			FString Text;
			const FString Path = FPaths::ProjectContentDir() / TEXT("Data") / TEXT("StoneCircles.csv");
			if (!FFileHelper::LoadFileToString(Text, *Path)) { UE_LOG(LogJeebStone, Error, TEXT("Jeebsland stone circles: cannot read %s"), *Path); return Out; }
			TArray<FString> Lines;
			Text.ParseIntoArrayLines(Lines);
			bool bHeader = true;
			for (const FString& Line : Lines)
			{
				if (Line.TrimStartAndEnd().IsEmpty() || Line.StartsWith(TEXT("#"))) continue;
				if (bHeader) { bHeader = false; continue; }
				TArray<FString> C;
				Line.ParseIntoArray(C, TEXT(","), false);
				if (C.Num() < 7) { UE_LOG(LogJeebStone, Error, TEXT("Jeebsland stone circles: row needs Name,Zone,X,Y,Radius,Frost,DayOffset: %s"), *Line); continue; }
				FCircle S;
				S.Name = C[0].TrimStartAndEnd(); S.Zone = C[1].TrimStartAndEnd();
				S.Pos = FVector2D(FCString::Atof(*C[2]) * 100.f, FCString::Atof(*C[3]) * 100.f);
				S.Radius = FMath::Clamp(FCString::Atof(*C[4]) * 100.f, 500.f, 2000.f);
				S.bFrost = C[5].TrimStartAndEnd().Equals(TEXT("yes"), ESearchCase::IgnoreCase) || C[5].TrimStartAndEnd() == TEXT("1");
				S.DayOffset = FCString::Atoi(*C[6]);
				Out.Add(S);
			}
			return Out;
		}();
		return All;
	}

	bool KeepOut(float X, float Y, float Extra)
	{
		for (const FCircle& C : Circles())
			if (FVector2D::DistSquared(FVector2D(X, Y), C.Pos) < FMath::Square(C.Radius + Extra)) return true;
		return false;
	}

	bool UnderCentre(float X, float Y)
	{
		for (const FCircle& C : Circles()) if (FVector2D::DistSquared(FVector2D(X, Y), C.Pos) < FMath::Square(150.f)) return true;
		return false;
	}

	static int32 Interval() { return FMath::Max(1, FMath::RoundToInt(JeebTune::V().IntervalDays)); }

	bool EventAt(double T, double TestNoon, int32 TestCircle, FEvent& Out)
	{
		const double Count = FMath::Clamp(double(JeebTune::V().CountdownSeconds), 10.0, DawnBefore - 10.0);
		const double Wake = FMath::Max(double(JeebTune::V().WakeSeconds), 10.0);
		if (TestNoon > 0.0 && Circles().IsValidIndex(TestCircle) && T >= TestNoon - Count - 15.0 && T < TestNoon + Wake)
		{
			Out.Circle = TestCircle; Out.Noon = TestNoon; Out.Dawn = TestNoon - Count - 15.0; Out.CountFrom = TestNoon - Count; Out.End = TestNoon + Wake; Out.bTest = true;
			return true;
		}
		const int64 Day = DayOf(T + DawnBefore);   // (a day's event runs from its dawn: from 348 s before its noon, past midnight's changeover)
		for (int64 D = Day - 1; D <= Day; ++D)
		{
			const double Noon = NoonOf(D);
			if (T < Noon - DawnBefore || T >= Noon + Wake) continue;
			const int64 InCycle = ((D % Interval()) + Interval()) % Interval();
			for (int32 c = 0; c < Circles().Num(); ++c)
				if (InCycle == ((Circles()[c].DayOffset % Interval()) + Interval()) % Interval())
				{
					Out.Circle = c; Out.Noon = Noon; Out.Dawn = Noon - DawnBefore; Out.CountFrom = Noon - Count; Out.End = Noon + Wake; Out.bTest = false;
					return true;
				}
		}
		return false;
	}

	double NextNoon(int32 C, double T)
	{
		if (!Circles().IsValidIndex(C)) return -1.0;
		for (int64 D = DayOf(T); D < DayOf(T) + 2 * Interval() + 2; ++D)
			if (((D % Interval()) + Interval()) % Interval() == ((Circles()[C].DayOffset % Interval()) + Interval()) % Interval() && NoonOf(D) > T) return NoonOf(D);
		return -1.0;
	}

	bool EventNow(const UWorld* World, FEvent& Out, double* OutT)
	{
		const AKnightGameState* GS = World ? World->GetGameState<AKnightGameState>() : nullptr;
		if (!GS) return false;
		const double T = GS->GetServerWorldTimeSeconds();
		if (OutT) *OutT = T;
		return EventAt(T, GS->StoneTestNoon, GS->StoneTestCircle, Out);
	}

	// ---- the loot (Content/Data/StoneCircleLoot.csv)
	struct FLootRow { int32 Id = 0; int32 Min = 1, Max = 1; float Weight = 1.f; };
	static const TArray<FLootRow>& LootRows()
	{
		static const TArray<FLootRow> All = []()
		{
			TArray<FLootRow> Out;
			FString Text;
			const FString Path = FPaths::ProjectContentDir() / TEXT("Data") / TEXT("StoneCircleLoot.csv");
			if (!FFileHelper::LoadFileToString(Text, *Path)) { UE_LOG(LogJeebStone, Error, TEXT("Jeebsland stone circles: cannot read %s"), *Path); return Out; }
			TArray<FString> Lines;
			Text.ParseIntoArrayLines(Lines);
			bool bHeader = true;
			for (int32 n = 0; n < Lines.Num(); ++n)
			{
				const FString& Line = Lines[n];
				if (Line.TrimStartAndEnd().IsEmpty() || Line.StartsWith(TEXT("#"))) continue;
				if (bHeader) { bHeader = false; continue; }
				TArray<FString> C;
				Line.ParseIntoArray(C, TEXT(","), false);
				if (C.Num() < 4) continue;
				FLootRow R;
				const FString Name = C[0].TrimStartAndEnd();
				if (Name.Equals(TEXT("Ingots"), ESearchCase::IgnoreCase)) R.Id = 0;
				else
				{
					R.Id = JeebQuests::ItemFromName(Name);
					if (R.Id <= 0 || JeebItems::ItemName(R.Id).IsEmpty()) { UE_LOG(LogJeebStone, Error, TEXT("Jeebsland stone circles: StoneCircleLoot.csv line %d: no item called \"%s\""), n + 1, *Name); continue; }
				}
				R.Min = FMath::Max(1, FCString::Atoi(*C[1]));
				R.Max = FMath::Max(R.Min, FCString::Atoi(*C[2]));
				R.Weight = FMath::Max(0.f, FCString::Atof(*C[3]));
				Out.Add(R);
			}
			return Out;
		}();
		return All;
	}

	// The stack for one wake: every Weight-0 row, then LootDraws different rows drawn by weight. Seeded by the wake, so a log can be checked.
	static void RollLoot(uint32 Seed, TArray<FInvSlot>& OutItems, int32& OutIngots)
	{
		FRandomStream Rs(static_cast<int32>(Seed));
		OutIngots = 0;
		TArray<int32> Pool;
		auto Give = [&](const FLootRow& Row)
		{
			const int32 N = Rs.RandRange(Row.Min, Row.Max);
			if (Row.Id == 0) { OutIngots += N; return; }
			if (FInvSlot* Have = OutItems.FindByPredicate([&](const FInvSlot& S) { return S.Id == Row.Id; })) Have->Count += N;
			else OutItems.Add(FInvSlot(Row.Id, N));
		};
		const TArray<FLootRow>& Rows = LootRows();
		for (int32 i = 0; i < Rows.Num(); ++i) { if (Rows[i].Weight <= 0.f) Give(Rows[i]); else Pool.Add(i); }
		const int32 Draws = FMath::Max(0, FMath::RoundToInt(JeebTune::V().LootDraws));
		for (int32 d = 0; d < Draws && Pool.Num() > 0; ++d)
		{
			float Sum = 0.f;
			for (int32 i : Pool) Sum += Rows[i].Weight;
			float Pick = Rs.FRand() * Sum;
			int32 At = Pool.Num() - 1;
			for (int32 k = 0; k < Pool.Num(); ++k) { Pick -= Rows[Pool[k]].Weight; if (Pick <= 0.f) { At = k; break; } }
			Give(Rows[Pool[At]]);
			Pool.RemoveAt(At);
		}
	}
}

#if !UE_BUILD_SHIPPING
// jeeb.Stones.WakeNow [circle] [seconds to noon]: a test wake (the server's; on a player's machine online it does nothing)
static FAutoConsoleCommandWithWorldAndArgs GStoneWakeNow(TEXT("jeeb.Stones.WakeNow"),
	TEXT("Test (not Shipping): a stone circle wakes soon. Args: circle (0 = the first row of StoneCircles.csv), seconds until its noon (default the countdown + 10). Server or single player only."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		AKnightGameState* GS = World ? World->GetGameState<AKnightGameState>() : nullptr;
		if (!GS || !GS->HasAuthority()) { UE_LOG(LogJeebStone, Warning, TEXT("JEEBSTONE wake-now: only the server (or single player) can wake a circle")); return; }
		const int32 C = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 0;
		const float Lead = Args.Num() > 1 ? FCString::Atof(*Args[1]) : JeebTune::V().CountdownSeconds + 10.f;
		GS->StoneTestCircle = C;
		GS->StoneTestNoon = GS->GetServerWorldTimeSeconds() + FMath::Max(Lead, 1.f);
		UE_LOG(LogJeebStone, Log, TEXT("JEEBSTONE test wake: circle %d at server %.1f (in %.0f s)"), C, GS->StoneTestNoon, Lead);
	}));
#endif

// ------------------------------------------------------------------ the stones' shapes (made in code: a closed surface each)
namespace
{
	struct FShape { TArray<FVector> V; TArray<int32> I; };

	// A weathered stone: rings of K points (a rounded-square section, gently uneven) at the profile's heights, closed top and bottom.
	// Profile: (height share, size share). Local X is its thickness (D), Y its width (W), Z up; it stands from Z = -Sink.
	// Triangles wind so cross(B - A, C - A) points out (Unreal's front faces).
	FShape Stone(float H, float W, float D, const TArray<FVector2D>& Profile, float Slant, float Rough, int32 Seed, bool bDome, float Sink)
	{
		FShape S;
		const int32 K = 18;
		const float Sd = float(Seed % 997) * 1.37f;
		for (int32 l = 0; l < Profile.Num(); ++l)
		{
			const float T = Profile[l].X, Sc = Profile[l].Y;
			for (int32 k = 0; k < K; ++k)
			{
				const float A = 2.f * PI * k / K;
				const float Ca = FMath::Cos(A), Sa = FMath::Sin(A);
				// a rounded square (superellipse, n = 3): flat faces with soft corners
				const float Px = FMath::Sign(Ca) * FMath::Pow(FMath::Abs(Ca), 0.5f), Py = FMath::Sign(Sa) * FMath::Pow(FMath::Abs(Sa), 0.5f);
				const float N = FMath::PerlinNoise3D(FVector(Ca * 0.8f + Sd, Sa * 0.8f - Sd, T * 1.4f + Sd * 0.5f)) * Rough * 1.3f + FMath::PerlinNoise3D(FVector(Ca * 1.7f - Sd, Sa * 1.7f + Sd, T * 3.1f)) * Rough * 0.6f
					+ FMath::PerlinNoise3D(FVector(Ca * 3.1f - Sd, Sa * 3.1f + Sd, T * 6.f)) * Rough * 0.35f;
				const float Rs = Sc * (1.f + N);
				const float X = Px * D * 0.5f * Rs, Y = Py * W * 0.5f * Rs;
				float Z = -Sink + T * (H + Sink);
				if (T > 0.6f) Z += Slant * (Y / (W * 0.5f)) * (T - 0.6f) / 0.4f;   // (one shoulder lower)
				S.V.Add(FVector(X, Y, Z));
			}
		}
		const int32 L = Profile.Num();
		for (int32 l = 0; l + 1 < L; ++l)
			for (int32 k = 0; k < K; ++k)
			{
				const int32 A = l * K + k, B = l * K + (k + 1) % K, C = (l + 1) * K + k, Dd = (l + 1) * K + (k + 1) % K;
				S.I.Append({ A, B, C, B, Dd, C });
			}
		// the top: a dome point (a worn rounded top) or a flat cap (the centre stone)
		FVector TopMid = FVector::ZeroVector;
		for (int32 k = 0; k < K; ++k) TopMid += S.V[(L - 1) * K + k];
		TopMid /= float(K);
		if (bDome) TopMid.Z += H * 0.025f;
		const int32 Top = S.V.Add(TopMid);
		for (int32 k = 0; k < K; ++k) S.I.Append({ (L - 1) * K + k, (L - 1) * K + (k + 1) % K, Top });
		const int32 Bot = S.V.Add(FVector(0.f, 0.f, -Sink));
		for (int32 k = 0; k < K; ++k) S.I.Append({ (k + 1) % K, k, Bot });
		return S;
	}

	void Normals(const FShape& S, TArray<FVector>& N)
	{
		N.Init(FVector::ZeroVector, S.V.Num());
		for (int32 t = 0; t + 2 < S.I.Num(); t += 3)
		{
			const FVector& A = S.V[S.I[t]]; const FVector& B = S.V[S.I[t + 1]]; const FVector& C = S.V[S.I[t + 2]];
			const FVector F = FVector::CrossProduct(B - A, C - A);   // (area-weighted)
			N[S.I[t]] += F; N[S.I[t + 1]] += F; N[S.I[t + 2]] += F;
		}
		for (FVector& V : N) V = V.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
	}

	// The frost on a northern stone's top: the stone's own upper rings, a hair larger, from Share of its height up
	FShape Cap(const FShape& Src, int32 Rings, int32 FromRing)
	{
		const int32 K = 18;
		FShape S;
		const FVector Mid = Src.V[Rings * K];   // (the top point)
		for (int32 l = FromRing; l < Rings; ++l)
			for (int32 k = 0; k < K; ++k)
			{
				FVector P = Src.V[l * K + k];
				const FVector Out = FVector(P.X, P.Y, 0.f).GetSafeNormal();
				P += Out * 2.5f + FVector(0.f, 0.f, l == FromRing ? -3.f : 1.5f);
				S.V.Add(P);
			}
		const int32 L = Rings - FromRing;
		for (int32 l = 0; l + 1 < L; ++l)
			for (int32 k = 0; k < K; ++k)
			{
				const int32 A = l * K + k, B = l * K + (k + 1) % K, C = (l + 1) * K + k, Dd = (l + 1) * K + (k + 1) % K;
				S.I.Append({ A, B, C, B, Dd, C });
			}
		const int32 Top = S.V.Add(Mid + FVector(0.f, 0.f, 3.f));
		for (int32 k = 0; k < K; ++k) S.I.Append({ (L - 1) * K + k, (L - 1) * K + (k + 1) % K, Top });
		// (its lower rim is open: it hugs the stone, whose own surface is under it)
		return S;
	}

	float ProfileSize(const TArray<FVector2D>& P, float T)
	{
		for (int32 i = 0; i + 1 < P.Num(); ++i)
			if (T <= P[i + 1].X) return FMath::Lerp(P[i].Y, P[i + 1].Y, (T - P[i].X) / FMath::Max(P[i + 1].X - P[i].X, 0.001f));
		return P.Last().Y;
	}
}

// ------------------------------------------------------------------ the actor
AStoneCircles::AStoneCircles()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.f;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	SetReplicates(false);
}

void AStoneCircles::BeginPlay()
{
	Super::BeginPlay();
	SetActorLocation(FVector::ZeroVector);
	if (FParse::Param(FCommandLine::Get(), TEXT("JeebStoneSearch"))) Search();
	for (int32 c = 0; c < JeebStones::Circles().Num(); ++c) Build(c);
#if !UE_BUILD_SHIPPING
	if (HasAuthority())
	{
		float In = -1.f; int32 Ci = 0;
		if (FParse::Value(FCommandLine::Get(), TEXT("StoneWakeIn="), In) && In > 0.f)
		{
			FParse::Value(FCommandLine::Get(), TEXT("StoneWakeCircle="), Ci);
			if (AKnightGameState* GS = GetWorld()->GetGameState<AKnightGameState>())
			{
				GS->StoneTestCircle = Ci;
				GS->StoneTestNoon = GS->GetServerWorldTimeSeconds() + In;
				UE_LOG(LogJeebStone, Log, TEXT("JEEBSTONE test wake (command line): circle %d at server %.1f"), Ci, GS->StoneTestNoon);
			}
		}
	}
#endif
	const double Now = GetWorld()->GetGameState() ? GetWorld()->GetGameState()->GetServerWorldTimeSeconds() : 0.0;
	for (int32 c = 0; c < JeebStones::Circles().Num(); ++c)
	{
		const double Next = JeebStones::NextNoon(c, Now);
		UE_LOG(LogJeebStone, Log, TEXT("JEEBSTONE circle %d %s: next wake at server %.0f (game day %lld, in %.1f min)"), c, *JeebStones::Circles()[c].Name, Next, JeebStones::DayOf(Next), (Next - Now) / 60.0);
	}
}

void AStoneCircles::Build(int32 Ci)
{
	const JeebStones::FCircle& C = JeebStones::Circles()[Ci];
	FLook& Lk = Looks.AddDefaulted_GetRef();
	const bool bServer = IsNetMode(NM_DedicatedServer);
	UMaterialInterface* StoneMat = bServer ? nullptr : LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Stone.M_Stone"));
	UMaterialInterface* ColorMat = bServer ? nullptr : LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"));
	UMaterialInterface* GlowMat = bServer ? nullptr : LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Props/M_GlowOrb.M_GlowOrb"));
	UStaticMesh* Cube = bServer ? nullptr : LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	const FLinearColor Tint = C.bFrost ? FLinearColor(0.86f, 0.9f, 0.98f) : FLinearColor(0.97f, 0.95f, 0.9f);
	const FLinearColor RuneCol = C.bFrost ? FLinearColor(0.45f, 0.78f, 1.f) : FLinearColor(0.35f, 1.f, 0.85f);

	// the ground's lowest point under a footprint, so no stone floats on a slope
	auto Ground = [](const FVector2D& P, float R)
	{
		float Lo = JeebTerrain::HeightAt(P.X, P.Y);
		for (int32 k = 0; k < 6; ++k) { const float A = k * PI / 3.f; Lo = FMath::Min(Lo, JeebTerrain::HeightAt(P.X + R * FMath::Cos(A), P.Y + R * FMath::Sin(A))); }
		return Lo;
	};
	auto AddMesh = [&](const FShape& S, const FTransform& Xf, UMaterialInterface* Mat, const FLinearColor& Col, bool bTint)
	{
		if (bServer) return;
		UProceduralMeshComponent* M = NewObject<UProceduralMeshComponent>(this);
		M->SetupAttachment(GetRootComponent());
		M->SetWorldTransform(Xf);
		M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		M->SetCanEverAffectNavigation(false);
		M->bUseAsyncCooking = true;
		TArray<FVector> N;
		Normals(S, N);
		M->CreateMeshSection_LinearColor(0, S.V, S.I, N, TArray<FVector2D>(), TArray<FLinearColor>(), TArray<FProcMeshTangent>(), false);
		M->SetCullDistance(40000.f);
		M->RegisterComponent();
		if (Mat) if (UMaterialInstanceDynamic* D = M->CreateDynamicMaterialInstance(0, Mat)) D->SetVectorParameterValue(bTint ? TEXT("Tint") : TEXT("Color"), Col);
	};
	auto AddSolid = [&](const FTransform& Xf, const FVector& Half)
	{
		UBoxComponent* B = NewObject<UBoxComponent>(this);
		B->SetupAttachment(GetRootComponent());
		B->SetBoxExtent(Half);
		B->SetWorldTransform(Xf);
		B->SetCollisionProfileName(TEXT("BlockAll"));
		B->SetCanEverAffectNavigation(false);
		B->RegisterComponent();
	};
	auto AddRune = [&](const FTransform& Xf, const FVector& Local, const FVector& Size, const FRotator& Rot)
	{
		if (bServer || !GlowMat || !Cube) return;
		UStaticMeshComponent* R = NewObject<UStaticMeshComponent>(this);
		R->SetupAttachment(GetRootComponent());
		R->SetStaticMesh(Cube);
		R->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		R->SetCastShadow(false);
		R->SetWorldTransform(FTransform(Rot.Quaternion(), Local, Size / 100.f) * Xf);
		R->SetCullDistance(30000.f);
		R->RegisterComponent();
		if (UMaterialInstanceDynamic* D = R->CreateDynamicMaterialInstance(0, GlowMat))
		{
			D->SetVectorParameterValue(TEXT("Color"), RuneCol);
			D->SetScalarParameterValue(TEXT("Strength"), 0.f);
			Lk.Runes.Add(D);
		}
		R->SetVisibility(false);
	};

	FRandomStream R(int32(GetTypeHash(C.Name)) ^ 0x5701);
	const TArray<FVector2D> Tall = { {0.f, 1.03f}, {0.12f, 1.f}, {0.3f, 0.97f}, {0.5f, 0.94f}, {0.66f, 0.9f}, {0.78f, 0.83f}, {0.87f, 0.8f}, {0.94f, 0.68f}, {0.985f, 0.5f} };
	const int32 Count = 11;
	const float Turn = R.FRandRange(0.f, 360.f);
	for (int32 s = 0; s < Count; ++s)
	{
		const float H = R.FRandRange(210.f, 390.f), W = R.FRandRange(105.f, 190.f), D = R.FRandRange(55.f, 95.f);
		const float Jit = R.FRandRange(-6.f, 6.f), Lean = R.FRandRange(-4.f, 4.f), LeanSide = R.FRandRange(-3.f, 3.f), Slant = R.FRandRange(-0.3f, 0.3f) * W, Spin = R.FRandRange(-8.f, 8.f);
		const bool bFallen = (s == 4);   // one has fallen long ago and lies in the grass
		const float A = FMath::DegreesToRadians(Turn + s * 360.f / Count + Jit);
		const FVector2D Dir(FMath::Cos(A), FMath::Sin(A));
		const FVector2D P = C.Pos + Dir * C.Radius * R.FRandRange(0.96f, 1.04f);
		const float Yaw = FMath::RadiansToDegrees(A) + Spin;   // (local X points out from the middle: the broad faces face the centre)
		const FShape Sh = Stone(H, W, D, Tall, Slant, R.FRandRange(0.12f, 0.2f), 31 * Ci + s, (s % 3) != 0, 60.f);
		FTransform Xf;
		if (bFallen)
		{
			// on its side, half sunk, pointing round the ring
			const float Gz = Ground(P, 160.f);
			Xf = FTransform(FRotator(-86.f, Yaw + 90.f, 0.f).Quaternion() * FRotator(0.f, 0.f, 0.f).Quaternion(), FVector(P.X, P.Y, Gz + D * 0.32f));
			AddSolid(FTransform(Xf.GetRotation(), Xf.TransformPosition(FVector(0.f, 0.f, H * 0.45f))), FVector(D * 0.45f, W * 0.48f, H * 0.5f));
		}
		else
		{
			const float Gz = Ground(P, W * 0.45f);
			Xf = FTransform(FRotator(Lean, Yaw, LeanSide), FVector(P.X, P.Y, Gz));
			AddSolid(FTransform(Xf.GetRotation(), Xf.TransformPosition(FVector(0.f, 0.f, H * 0.45f))), FVector(D * 0.45f, W * 0.46f, H * 0.47f));
			// the rune lines on the inner face (local -X): a tall stroke and two short ticks, each laid on the face at its own height
			for (int32 g = 0; g < 3; ++g)
			{
				const float T = g == 0 ? 0.42f : (g == 1 ? 0.3f : 0.56f);
				const float Len = g == 0 ? H * 0.36f : H * 0.1f;
				const float Face = -(D * 0.5f * ProfileSize(Tall, T) * 0.93f) - 1.5f;
				const float Yoff = g == 0 ? 0.f : (g == 1 ? -W * 0.16f : W * 0.16f);
				AddRune(Xf, FVector(Face, Yoff, -60.f + T * (H + 60.f)), FVector(3.f, 9.f, Len * 1.3f), FRotator(0.f, 0.f, g == 0 ? 0.f : (g == 1 ? 40.f : -40.f)));
			}
		}
		AddMesh(Sh, Xf, StoneMat, Tint, true);
		if (C.bFrost && ColorMat && !bFallen) AddMesh(Cap(Sh, Tall.Num(), 6), Xf, ColorMat, FLinearColor(0.82f, 0.86f, 0.92f), false);
	}
	// the flat centre stone: low and broad, a level top for the loot
	{
		const TArray<FVector2D> Slab = { {0.f, 1.04f}, {0.45f, 1.f}, {0.82f, 0.98f}, {0.93f, 0.95f}, {1.f, 0.86f} };
		const float H = 100.f, W = 240.f, D = 170.f;
		const float Gz = Ground(C.Pos, 110.f);
		const FShape Sh = Stone(H, W, D, Slab, 0.f, 0.035f, 900 + Ci, false, 40.f);
		const FTransform Xf(FRotator(0.f, Turn + 17.f, 0.f), FVector(C.Pos.X, C.Pos.Y, Gz));
		AddMesh(Sh, Xf, StoneMat, Tint * 0.96f, true);
		if (C.bFrost && ColorMat) AddMesh(Cap(Sh, Slab.Num(), 3), Xf, ColorMat, FLinearColor(0.82f, 0.86f, 0.92f), false);
		AddSolid(FTransform(Xf.GetRotation(), Xf.TransformPosition(FVector(0.f, 0.f, H * 0.5f - 10.f))), FVector(D * 0.46f, W * 0.46f, H * 0.5f + 10.f));
		Lk.Top = Xf.TransformPosition(FVector(0.f, 0.f, H + 3.f));
		// a glowing ring cut in its top (four strokes round the middle)
		for (int32 g = 0; g < 4; ++g)
		{
			const FRotator Ga(0.f, g * 90.f + 45.f, 0.f);
			AddRune(Xf, Ga.RotateVector(FVector(58.f, 0.f, H + 0.5f)), FVector(8.f, 60.f, 2.f), Ga);
		}
		if (!bServer)
		{
			UPointLightComponent* Pl = NewObject<UPointLightComponent>(this);
			Pl->SetupAttachment(GetRootComponent());
			Pl->SetWorldLocation(Lk.Top + FVector(0.f, 0.f, 160.f));
			Pl->SetIntensityUnits(ELightUnits::Candelas);
			Pl->SetIntensity(0.f);
			Pl->SetAttenuationRadius(1600.f);
			Pl->SetLightColor(RuneCol);
			Pl->SetCastShadows(false);
			Pl->SetVisibility(false);
			Pl->RegisterComponent();
			Lk.Light = Pl;
		}
	}
	UE_LOG(LogJeebStone, Log, TEXT("JEEBSTONE built %s at %.0f %.0f (ground %.0f, %s): %d stones, centre top %.0f; zone %s; PvP zone here: %s"), *C.Name, C.Pos.X, C.Pos.Y,
		JeebTerrain::HeightAt(C.Pos.X, C.Pos.Y), C.bFrost ? TEXT("frost") : TEXT("plain"), Count, Lk.Top.Z, *C.Zone, IsInPvpZone(FVector(C.Pos, 0.f)) ? TEXT("yes") : TEXT("NO"));
}

void AStoneCircles::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	JeebStones::FEvent E;
	double T = 0.0;
	const bool bOn = JeebStones::EventNow(GetWorld(), E, &T);

	// ---- every player's machine: the glow
	if (!IsNetMode(NM_DedicatedServer))
		for (int32 c = 0; c < Looks.Num(); ++c)
		{
			FLook& Lk = Looks[c];
			float G = 0.f;
			if (bOn && E.Circle == c)
			{
				if (T < E.CountFrom) G = 0.f;
				else if (T < E.Noon) G = 0.22f * float((T - E.CountFrom) / FMath::Max(E.Noon - E.CountFrom, 1.0));   // (stirring through the countdown)
				else G = FMath::Min(1.f, 0.22f + float(T - E.Noon) / 2.5f) * FMath::Clamp(float(E.End - T) / 20.f, 0.f, 1.f);
				G *= 0.88f + 0.12f * FMath::Sin(float(FMath::Fmod(T, 1000.0)) * 1.7f);   // (a slow breath)
			}
			if (FMath::Abs(G - Lk.Glow) < 0.004f) continue;
			const bool bShow = G > 0.001f;
			for (TWeakObjectPtr<UMaterialInstanceDynamic>& M : Lk.Runes)
				if (UMaterialInstanceDynamic* D = M.Get())
				{
					D->SetScalarParameterValue(TEXT("Strength"), G * JeebTune::V().RuneGlow);
					if (UPrimitiveComponent* P = Cast<UPrimitiveComponent>(D->GetOuter())) P->SetVisibility(bShow);
				}
			if (UPointLightComponent* L = Lk.Light.Get()) { L->SetVisibility(bShow); L->SetIntensity(G * JeebTune::V().WakeLight); }
			Lk.Glow = G;
		}

	// ---- the server: the loot, once per wake
	if (!HasAuthority()) return;
	Check -= DeltaSeconds;
	if (Check > 0.f) return;
	Check = 0.25f;
#if !UE_BUILD_SHIPPING
	TickNetTest(T);
#endif
	if (!bOn || T < E.Noon || T >= E.End || !Looks.IsValidIndex(E.Circle)) return;
	if (Spawned == E.Noon && SpawnedCircle == E.Circle) return;
	Spawned = E.Noon; SpawnedCircle = E.Circle;
	TArray<FInvSlot> Items; int32 Ingots = 0;
	JeebStones::RollLoot(uint32(JeebStones::DayOf(E.Noon)) * 2654435761u + uint32(E.Circle), Items, Ingots);
	FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (ALootDrop* Drop = GetWorld()->SpawnActor<ALootDrop>(Looks[E.Circle].Top, FRotator::ZeroRotator, SP))
	{
		Drop->Setup(ELootLook::Item, Items, Ingots);
		Drop->bOneTaker = true;
		Drop->SetLifeSpan(FMath::Max(JeebTune::V().LootLife, 30.f));
		Stack = Drop;
		FString What;
		for (const FInvSlot& S : Items) What += FString::Printf(TEXT("%s x%d, "), *JeebItems::ItemName(S.Id), S.Count);
		UE_LOG(LogJeebStone, Log, TEXT("JEEBSTONE woke %s at server %.1f%s: loot %s%d ingots, on the centre stone at %.0f %.0f %.0f"), *JeebStones::Circles()[E.Circle].Name, T, E.bTest ? TEXT(" (test)") : TEXT(""), *What, Ingots, Drop->GetActorLocation().X, Drop->GetActorLocation().Y, Drop->GetActorLocation().Z);
	}
}

#if !UE_BUILD_SHIPPING
// -JeebStoneNetTest on the server (Tools/stone_net.sh): once the loot lies, the first player is moved 60 m off and asks for it (refused:
// too far), then both players are put beside it; the second takes it (gets it) and the first asks again (refused: gone). Through
// AKnightCharacter::Inv, the same server code a player's E press reaches.
void AStoneCircles::TickNetTest(double T)
{
	static const bool bOn = FParse::Param(FCommandLine::Get(), TEXT("JeebStoneNetTest"));
	if (!bOn || TestStep >= 9) return;
	ALootDrop* Drop = Stack.Get();
	if (!Drop || Drop->IsActorBeingDestroyed()) { if (TestStep == 0) return; }
	TArray<AKnightCharacter*> K;
	for (TActorIterator<AKnightCharacter> It(GetWorld()); It; ++It) if (It->GetController()) K.Add(*It);
	K.Sort([](const AKnightCharacter& A, const AKnightCharacter& B) { return A.GetName() < B.GetName(); });
	auto Held = [](AKnightCharacter* Who) { int32 N = 0; if (USkillComponent* Sk = Who->FindComponentByClass<USkillComponent>()) for (const FInvSlot& S : Sk->Slots) N += S.Id != 0 ? S.Count : 0; return N; };
	auto Left = [](ALootDrop* D) { int32 N = 0; if (D && !D->IsActorBeingDestroyed()) for (const FInvSlot& S : D->GetItems()) N += S.Id != 0 ? S.Count : 0; return N; };
	if (K.Num() < 2) return;
	const FVector Top = Drop ? Drop->GetActorLocation() : FVector::ZeroVector;
	switch (TestStep)
	{
	case 0:
		K[0]->TeleportTo(Top + FVector(6000.f, 0.f, 300.f), K[0]->GetActorRotation());
		UE_LOG(LogJeebStone, Log, TEXT("JEEBSTONETEST step 0: loot has %d things; %s moved 60 m off"), Left(Drop), *K[0]->GetName());
		++TestStep; break;
	case 2:
	{
		const int32 Before = Left(Drop), HeldBefore = Held(K[0]);
		K[0]->Inv(EInvOp::LootTakeAll, 0, 0, 0, Drop);
		const bool bRefused = Left(Drop) == Before && Held(K[0]) == HeldBefore;
		UE_LOG(LogJeebStone, Log, TEXT("JEEBSTONETEST far take by %s from %.0f m: %s (loot %d -> %d)"), *K[0]->GetName(), FVector::Dist2D(K[0]->GetActorLocation(), Top) / 100.f, bRefused ? TEXT("PASS refused") : TEXT("FAIL taken"), Before, Left(Drop));
		K[0]->TeleportTo(Top + FVector(150.f, 60.f, 120.f), K[0]->GetActorRotation());
		K[1]->TeleportTo(Top + FVector(-150.f, -60.f, 120.f), K[1]->GetActorRotation());
		++TestStep; break;
	}
	case 4:
	{
		const int32 Before = Left(Drop), HeldBefore = Held(K[1]);
		K[1]->Inv(EInvOp::LootTakeAll, 0, 0, 0, Drop);
		UE_LOG(LogJeebStone, Log, TEXT("JEEBSTONETEST near take by %s: %s (it now holds %d more things; loot %d -> %d)"), *K[1]->GetName(), Held(K[1]) > HeldBefore ? TEXT("PASS got it") : TEXT("FAIL nothing"), Held(K[1]) - HeldBefore, Before, Left(Drop));
		++TestStep; break;
	}
	case 6:
	{
		const int32 HeldBefore = Held(K[0]);
		if (Drop && !Drop->IsActorBeingDestroyed()) K[0]->Inv(EInvOp::LootTakeAll, 0, 0, 0, Drop);
		UE_LOG(LogJeebStone, Log, TEXT("JEEBSTONETEST second take by %s: %s (the stack is %s)"), *K[0]->GetName(), Held(K[0]) == HeldBefore ? TEXT("PASS refused") : TEXT("FAIL got some"), (Drop && !Drop->IsActorBeingDestroyed()) ? TEXT("still there") : TEXT("gone"));
		++TestStep; break;
	}
	default: ++TestStep; break;   // (odd steps: a quarter second for the moves to settle)
	}
}
#endif

// ------------------------------------------------------------------ how the spots were chosen (-JeebStoneSearch)
void AStoneCircles::Search()
{
	const FVector2D Plaza(-3550.f, 18290.f);
	auto TreeAt = [&](float X, float Y) -> float
	{
		const float D = FVector2D::Distance(FVector2D(X, Y), Plaza);
		if (D < JeebOakForest::Reach - 3000.f && D > 26000.f) return 1.f;   // (Oak Hill's forest)
		const float A = JeebTerrain::HeightAt(X, Y) / JeebOpen::PeakTop;
		if (A > JeebOpen::Treeline) return 0.f;
		return JeebOpen::Woods(X, Y) * (1.f - 0.82f * JeebRework::Moor(X, Y)) * (1.f - JeebWorld::Smooth(0.44f, JeebOpen::Treeline, A));
	};
	auto RoadD = [](float X, float Y)
	{
		float Best = JeebTerrain::PathClearance(X, Y);
		for (const TArray<FVector2D>& L : JeebOpen::RoadLines())
			for (int32 i = 0; i < L.Num(); i += 20) Best = FMath::Min(Best, float(FVector2D::Distance(L[i], FVector2D(X, Y))) - 300.f);
		return Best;
	};
	struct FBox2 { const TCHAR* Name; float X0, X1, Y0, Y1; FVector2D Aim; bool bSnow; };
	const FBox2 Boxes[2] = {
		{ TEXT("middle"), -40000.f, 30000.f, 20000.f, 90000.f, FVector2D(-10400.f, 49200.f), false },
		{ TEXT("north"), 84500.f, 103000.f, -30000.f, 130000.f, FVector2D(95000.f, 50000.f), true } };
	for (const FBox2& B : Boxes)
	{
		struct FCand { FVector2D P; float Score; int32 Dirs; float Flat, Road, Town, Snow; };
		TArray<FCand> All;
		for (float X = B.X0; X <= B.X1; X += 1500.f)
			for (float Y = B.Y0; Y <= B.Y1; Y += 1500.f)
			{
				if (X < JeebWorld::MinX + 6000.f || X > JeebWorld::MaxX - 6000.f || Y < JeebWorld::MinY + 6000.f || Y > JeebWorld::MaxY - 6000.f) continue;
				if (JeebWorld::WaterEdge(X, Y) < 5000.f || JeebWorld::CoastY(X) - Y < 8000.f) continue;
				if (FVector2D::Distance(FVector2D(X, Y), Plaza) < JeebOakForest::Reach + 3000.f) continue;
				float TownD = 1.0e9f;
				for (const JeebWorld::FTown& Tn : JeebWorld::Towns()) TownD = FMath::Min(TownD, float(FVector2D::Distance(Tn.Pos, FVector2D(X, Y))));
				if (TownD < 30000.f) continue;
				// open: no trees within 30 m, flat within 15 m
				bool bOpen = true; float Lo = 1e9f, Hi = -1e9f;
				for (int32 k = 0; k < 16 && bOpen; ++k)
					for (float Rr : { 0.f, 1500.f, 3000.f })
					{
						const float A = k * PI / 8.f, Px = X + Rr * FMath::Cos(A), Py = Y + Rr * FMath::Sin(A);
						if (TreeAt(Px, Py) > 0.2f) { bOpen = false; break; }
						if (Rr <= 1500.f) { const float Z = JeebTerrain::HeightAt(Px, Py); Lo = FMath::Min(Lo, Z); Hi = FMath::Max(Hi, Z); }
					}
				if (!bOpen || Hi - Lo > 450.f || (B.bSnow && (Lo > 9000.f || Lo < 3000.f))) continue;
				const float Road = RoadD(X, Y);
				if (Road < 6000.f) continue;
				int32 Dirs = 0;
				for (int32 k = 0; k < 8; ++k)
				{
					const float A = k * PI / 4.f;
					for (float Rr = 5000.f; Rr <= 18000.f; Rr += 1000.f)
						if (TreeAt(X + Rr * FMath::Cos(A), Y + Rr * FMath::Sin(A)) > 0.35f) { ++Dirs; break; }
				}
				const float Snow = JeebOpen::SnowOnGround(X, Y);
				const float Score = Dirs * 10.f - FVector2D::Distance(FVector2D(X, Y), B.Aim) / 4000.f - (Hi - Lo) / 100.f + (B.bSnow ? Snow * 20.f : 0.f);
				All.Add({ FVector2D(X, Y), Score, Dirs, Hi - Lo, Road, TownD, Snow });
			}
		All.Sort([](const FCand& A, const FCand& Bb) { return A.Score > Bb.Score; });
		UE_LOG(LogJeebStone, Log, TEXT("JEEBSTONESEARCH %s: %d open spots"), B.Name, All.Num());
		for (int32 i = 0; i < FMath::Min(12, All.Num()); ++i)
			UE_LOG(LogJeebStone, Log, TEXT("JEEBSTONESEARCH %s #%d at %.0f %.0f m: score %.1f, woods in %d of 8 directions, rise %.0f cm in 15 m, road %.0f m, town %.0f m, snow %.2f, ground %.0f"),
				B.Name, i, All[i].P.X / 100.f, All[i].P.Y / 100.f, All[i].Score, All[i].Dirs, All[i].Flat, All[i].Road / 100.f, All[i].Town / 100.f, All[i].Snow, JeebTerrain::HeightAt(All[i].P.X, All[i].P.Y));
	}
}

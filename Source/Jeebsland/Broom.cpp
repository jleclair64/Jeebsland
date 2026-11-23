#include "Broom.h"
#include "OpenWorld.h"
#include "Terrain.h"
#include "Inventory.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

// ---------------------------------------------------------------- the sheets
namespace
{
	// A small CSV reader (quoted cells, # comments), as QuestData.cpp's
	bool ReadCsv(const TCHAR* Name, TMap<FString, int32>& Cols, TArray<TArray<FString>>& Rows)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *(FPaths::ProjectContentDir() / TEXT("Data") / Name)))
		{
			UE_LOG(LogTemp, Error, TEXT("JEEBBROOM cannot read Content/Data/%s"), Name);
			return false;
		}
		TArray<TArray<FString>> All;
		TArray<FString> Row;
		FString Cell;
		bool bQuoted = false, bStarted = false;
		auto EndCell = [&]() { Row.Add(Cell.TrimStartAndEnd()); Cell.Reset(); bStarted = false; };
		auto EndRow = [&]() { EndCell(); if (Row.Num() > 1 || !Row[0].IsEmpty()) All.Add(Row); Row.Reset(); };
		for (int32 i = 0; i < Text.Len(); ++i)
		{
			const TCHAR C = Text[i];
			if (bQuoted)
			{
				if (C == '"') { if (i + 1 < Text.Len() && Text[i + 1] == '"') { Cell.AppendChar('"'); ++i; } else bQuoted = false; }
				else Cell.AppendChar(C);
				continue;
			}
			if (C == '"' && !bStarted) { bQuoted = true; bStarted = true; }
			else if (C == ',') EndCell();
			else if (C == '\n') EndRow();
			else if (C == '\r' || C == 0xFEFF) continue;
			else { Cell.AppendChar(C); if (C != ' ') bStarted = true; }
		}
		if (!Cell.IsEmpty() || Row.Num() > 0) EndRow();
		All.RemoveAll([](const TArray<FString>& R) { return R.Num() > 0 && R[0].StartsWith(TEXT("#")); });
		if (All.Num() == 0) return false;
		for (int32 c = 0; c < All[0].Num(); ++c) Cols.Add(All[0][c].ToLower(), c);
		All.RemoveAt(0);
		Rows = MoveTemp(All);
		return true;
	}
	FString Cell(const TMap<FString, int32>& Cols, const TArray<FString>& R, const TCHAR* Col)
	{
		const int32* C = Cols.Find(FString(Col).ToLower());
		return (C && R.IsValidIndex(*C)) ? R[*C] : FString();
	}

	struct FCost { FString Item; int32 Tier = -1; float CastMul = 1.f, Guard = 0.f; };
	struct FTables
	{
		TArray<JeebBroom::FBroomRow> Brooms;
		TArray<FCost> Costs;
		FTables()
		{
			TMap<FString, int32> C; TArray<TArray<FString>> R;
			if (ReadCsv(TEXT("Brooms.csv"), C, R))
				for (const TArray<FString>& Row : R)
				{
					if (Brooms.Num() >= JeebBroom::MaxBrooms) break;
					JeebBroom::FBroomRow B;
					B.Key = Cell(C, Row, TEXT("Key")); B.Name = Cell(C, Row, TEXT("Name")); B.Blurb = Cell(C, Row, TEXT("Blurb"));
					B.Price = FCString::Atoi(*Cell(C, Row, TEXT("Price")));
					const FString S = Cell(C, Row, TEXT("SpeedMul")), D = Cell(C, Row, TEXT("Drain"));
					B.SpeedMul = S.IsEmpty() ? 1.f : FMath::Clamp(FCString::Atof(*S), 0.2f, 3.f);
					B.Drain = D.IsEmpty() ? 3.f : FMath::Max(0.f, FCString::Atof(*D));
					Brooms.Add(B);
				}
			C.Reset(); R.Reset();
			static const TCHAR* TierNames[] = { TEXT("Tin"), TEXT("Iron"), TEXT("Silver"), TEXT("Gold"), TEXT("Emerald"), TEXT("Diamond") };
			if (ReadCsv(TEXT("MagicCosts.csv"), C, R))
				for (const TArray<FString>& Row : R)
				{
					FCost K;
					K.Item = Cell(C, Row, TEXT("Item"));
					const FString T = Cell(C, Row, TEXT("Tier"));
					for (int32 t = 0; t < UE_ARRAY_COUNT(TierNames); ++t) if (T.Equals(TierNames[t], ESearchCase::IgnoreCase)) K.Tier = t;
					const FString M = Cell(C, Row, TEXT("CastMul")), G = Cell(C, Row, TEXT("GuardDrain"));
					K.CastMul = M.IsEmpty() ? 1.f : FMath::Max(0.f, FCString::Atof(*M));
					K.Guard = G.IsEmpty() ? 0.f : FMath::Max(0.f, FCString::Atof(*G));
					Costs.Add(K);
				}
			UE_LOG(LogTemp, Log, TEXT("JEEBBROOM sheets: %d brooms, %d magic cost rows"), Brooms.Num(), Costs.Num());
		}
		const FCost* Find(const TCHAR* Item, int32 Tier) const
		{
			const FCost* Any = nullptr;
			for (const FCost& K : Costs)
			{
				if (!K.Item.Equals(Item, ESearchCase::IgnoreCase)) continue;
				if (K.Tier == Tier) return &K;
				if (K.Tier < 0) Any = &K;
			}
			return Any;
		}
	};
	const FTables& Tab() { static const FTables T; return T; }
}

namespace JeebBroom
{
	int32 NumBrooms() { return Tab().Brooms.Num(); }
	const FBroomRow& Row(int32 Index) { static const FBroomRow None; return Tab().Brooms.IsValidIndex(Index) ? Tab().Brooms[Index] : None; }
	float CastMul(const TCHAR* Item, int32 Tier) { const FCost* K = Tab().Find(Item, Tier); return K ? K->CastMul : 1.f; }
	float GuardDrain(const TCHAR* Item, int32 Tier) { const FCost* K = Tab().Find(Item, Tier); return K ? K->Guard : 0.f; }

	// On Silver Point's green, 6 m from its middle on the side away from the placeholder house, facing the middle
	FVector StallSpot()
	{
		const TArray<JeebOpen::FSite>& S = JeebOpen::Sites();
		if (S.Num() == 0) return FVector(0.f, 0.f, -100000.f);
		const FVector2D Away = (S[0].Pos - S[0].House).GetSafeNormal();
		const FVector2D P = S[0].Pos + Away * 600.f;
		return FVector(P.X, P.Y, JeebTerrain::HeightAt(P.X, P.Y));
	}
	float StallYaw()
	{
		const TArray<JeebOpen::FSite>& S = JeebOpen::Sites();
		if (S.Num() == 0) return 0.f;
		const FVector2D To = S[0].Pos - FVector2D(StallSpot());
		return FMath::RadiansToDegrees(FMath::Atan2(To.Y, To.X));
	}
}

// ---------------------------------------------------------------- the stall
ABroomStall::ABroomStall()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void ABroomStall::PlaceIn(UWorld* World)
{
	if (!World) return;
	for (TActorIterator<ABroomStall> It(World); It; ++It) return;
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	World->SpawnActor<ABroomStall>(ABroomStall::StaticClass(), JeebBroom::StallSpot(), FRotator(0.f, JeebBroom::StallYaw(), 0.f), P);
	UE_LOG(LogTemp, Log, TEXT("JEEBBROOM stall at %s facing %.0f"), *JeebBroom::StallSpot().ToCompactString(), JeebBroom::StallYaw());
}

void ABroomStall::BeginPlay()
{
	Super::BeginPlay();
	if (IsNetMode(NM_DedicatedServer)) return;   // (nothing to see on a server; the distance check needs no parts)
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* Cyl = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UStaticMesh* BroomMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Props/SM_Broom.SM_Broom"));
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"));
	int32 N = 0;
	auto Part = [&](UStaticMesh* Mesh, const FVector& Loc, const FRotator& Rot, const FVector& Scale, const FLinearColor& Col, bool bSolid)
	{
		if (!Mesh) return;
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, *FString::Printf(TEXT("StallPart%d"), N++));
		C->SetStaticMesh(Mesh);
		C->SetupAttachment(GetRootComponent());
		C->SetRelativeLocationAndRotation(Loc, Rot);
		C->SetRelativeScale3D(Scale);
		C->SetCollisionEnabled(bSolid ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
		C->RegisterComponent();
		if (Base && Col.A > 0.f) if (UMaterialInstanceDynamic* M = C->CreateDynamicMaterialInstance(0, Base)) M->SetVectorParameterValue(TEXT("Color"), Col);
	};
	const FLinearColor Wood(0.16f, 0.07f, 0.03f), Dark(0.08f, 0.04f, 0.02f), Cloth(0.28f, 0.10f, 0.45f), Trim(0.75f, 0.58f, 0.20f);
	// The counter (solid), four posts and a striped purple awning
	Part(Cube, FVector(0, 0, 50), FRotator::ZeroRotator, FVector(0.8f, 2.4f, 1.0f), Wood, true);
	Part(Cube, FVector(0, 0, 101), FRotator::ZeroRotator, FVector(0.9f, 2.5f, 0.04f), Dark, false);
	for (int32 sx = -1; sx <= 1; sx += 2) for (int32 sy = -1; sy <= 1; sy += 2)
		Part(Cyl, FVector(sx * 45.f, sy * 120.f, 120.f), FRotator::ZeroRotator, FVector(0.08f, 0.08f, 2.4f), Dark, false);
	for (int32 k = 0; k < 5; ++k)
		Part(Cube, FVector(0, -100.f + k * 50.f, 242.f), FRotator(-12.f, 0.f, 0.f), FVector(1.3f, 0.5f, 0.03f), (k % 2) ? Trim : Cloth, false);
	// Brooms for sale leaning on the counter's front, and one on the counter
	if (BroomMesh)
	{
		for (int32 k = 0; k < 3; ++k)
			Part(BroomMesh, FVector(62.f, -70.f + k * 70.f, 70.f), FRotator(-62.f, 180.f, 0.f), FVector(0.85f), FLinearColor(0, 0, 0, 0), false);
		Part(BroomMesh, FVector(0.f, 0.f, 110.f), FRotator(0.f, 90.f, 0.f), FVector(0.85f), FLinearColor(0, 0, 0, 0), false);
	}
}

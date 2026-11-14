#include "FillerLineup.h"
#include "FillerLineupData.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/PointLightComponent.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

// Layout: rows run along +X, one row per group (a long group wraps onto another row), the first row nearest the floor. A piece stands on the
// row's line with its bottom on the grass; its name stands on a small sign in front of it (toward the floor, readable from the floor side).
namespace
{
	constexpr float YardTop = -2300.f;      // the first row's front edge, just past the floor's -Y edge (-2000)
	constexpr float YardHalfWidth = 5600.f; // rows wrap within +-this
	constexpr float Gap = 120.f;            // between pieces along a row
	constexpr float RowGap = 340.f;         // between rows (room for the signs)

	FString Spaced(const FString& Name)
	{
		FString Out;
		for (int32 i = 0; i < Name.Len(); ++i)
		{
			if (i > 0 && FChar::IsUpper(Name[i]) && (!FChar::IsUpper(Name[i - 1]) || (i + 1 < Name.Len() && FChar::IsLower(Name[i + 1])))) Out += TEXT(" ");
			Out.AppendChar(Name[i]);
		}
		return Out;
	}
	UStaticMesh* LoadPiece(const FString& Name)
	{
		return LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Game/Filler/SM_%s.SM_%s"), *Name, *Name));
	}
	bool GivesLight(const FString& Name) { return Name.Contains(TEXT("Lantern")) || Name.Contains(TEXT("LampPost")) || Name.Contains(TEXT("CandleCage")) || Name.StartsWith(TEXT("Torch")) || Name == TEXT("Sconce") || Name == TEXT("WellVillage"); }
	bool IsHungBanner(const FString& Name) { return Name.StartsWith(TEXT("Banner")) && !Name.Contains(TEXT("Pole")) && !Name.Contains(TEXT("Post")); }
}

void JeebFiller::BuildYard(UWorld* World, AActor* Owner, USceneComponent* Root, float FloorZ)
{
	if (!World || !Owner || !Root || World->GetNetMode() == NM_DedicatedServer) return;
	if (!LoadPiece(TEXT("Barrel"))) return;   // (not cooked into the packaged game: no yard)
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	struct FPlaced { UStaticMesh* Mesh; UStaticMesh* Flame; FString Name; FString Label; float W, D, H, Slot, LabelSize; };
	struct FRow { FString Title; TArray<FPlaced> Pieces; float Depth = 0.f; float MaxH = 0.f; };
	TArray<FRow> Rows;
	for (const FFillerGroup& G : GFillerGroups)
	{
		TArray<FString> Names;
		FString(UTF8_TO_TCHAR(G.Names)).ParseIntoArray(Names, TEXT(" "));
		FRow* Row = &Rows.AddDefaulted_GetRef();
		Row->Title = UTF8_TO_TCHAR(G.Title);
		float Used = 0.f;
		for (const FString& N : Names)
		{
			UStaticMesh* M = LoadPiece(N);
			if (!M) continue;
			const FBox B = M->GetBoundingBox();
			FPlaced P;
			P.Mesh = M; P.Flame = LoadPiece(N + TEXT("Flame")); P.Name = N; P.Label = Spaced(N);
			P.W = FMath::Max(40.f, B.Max.X - B.Min.X); P.D = FMath::Max(40.f, B.Max.Y - B.Min.Y); P.H = B.Max.Z;
			P.LabelSize = P.W < 90.f ? 26.f : 32.f;
			P.Slot = FMath::Max(P.W, P.Label.Len() * P.LabelSize * 0.62f);   // (room for the name too, so no two names touch)
			if (Used > 0.f && Used + P.Slot > 2.f * YardHalfWidth)
			{
				const FString T = Row->Title;
				Row = &Rows.AddDefaulted_GetRef();
				Row->Title = T + TEXT(" (more)");
				Used = 0.f;
			}
			Used += P.Slot + Gap;
			Row->Depth = FMath::Max(Row->Depth, P.D);
			Row->MaxH = FMath::Max(Row->MaxH, FMath::Max(P.H, IsHungBanner(N) ? 260.f : 0.f));
			Row->Pieces.Add(P);
		}
	}
	float TotalDepth = 0.f;
	for (const FRow& R : Rows) TotalDepth += R.Depth + RowGap;
	const float BottomY = YardTop - TotalDepth - 200.f;
	auto Static = [&](UStaticMesh* M, const FVector& Loc, const FVector& Scale, bool bShadow) -> UStaticMeshComponent*
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Owner);
		C->SetStaticMesh(M);
		C->SetupAttachment(Root);
		C->SetRelativeLocation(Loc);
		C->SetRelativeScale3D(Scale);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(bShadow);
		C->RegisterComponent();
		return C;
	};
	auto Sign = [&](const FString& Text, const FVector& Loc, float Size, const FColor& Col, bool bRight = false, float Yaw = 90.f)
	{
		UTextRenderComponent* S = NewObject<UTextRenderComponent>(Owner);
		S->SetupAttachment(Root);
		S->SetRelativeLocation(Loc);
		S->SetRelativeRotation(FRotator(0.f, Yaw, 0.f));   // (yaw 90: read from the floor side, looking toward -Y; -90 reads from the far side)
		S->SetHorizontalAlignment(bRight ? EHTA_Left : EHTA_Center);
		S->SetWorldSize(Size);
		S->SetTextRenderColor(Col);
		S->SetText(FText::FromString(Text));
		S->SetCastShadow(false);
		S->RegisterComponent();
	};
	// The grass yard: one flat slab, its top at the floor's height (like the test houses' patch past the other edge)
	if (Cube && Base)
	{
		const float Depth = YardTop - BottomY;
		UStaticMeshComponent* Pad = Static(Cube, FVector(0.f, (YardTop + BottomY) * 0.5f, -50.f), FVector(2.f * YardHalfWidth + 2600.f, Depth, 100.f) / 100.f, false);
		Pad->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Pad->SetCollisionResponseToAllChannels(ECR_Block);
		if (UMaterialInstanceDynamic* Mi = Pad->CreateDynamicMaterialInstance(0, Base)) Mi->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.20f, 0.30f, 0.14f));
	}
	float Y = YardTop - 120.f;
	int32 Count = 0, Lights = 0;
	for (const FRow& R : Rows)
	{
		if (R.Pieces.IsEmpty()) continue;
		const float Line = Y - R.Depth * 0.5f;   // (the row's middle line: pieces are centred on it from front to back)
		float Used = 0.f;
		for (const FPlaced& P : R.Pieces) Used += P.Slot + Gap;
		float X = -0.5f * (Used - Gap);
		const float TitleZ = R.MaxH + 90.f;   // (above the tallest piece of the row, so no piece can stand in front of it)
		Sign(R.Title, FVector(X - 110.f, Y + 40.f, TitleZ), 90.f, FColor(255, 238, 130), true);   // (starts beside the first piece and runs away from the row: never over a piece)
		for (const FPlaced& P : R.Pieces)
		{
			const FVector Loc(X + P.Slot * 0.5f, Line, 0.f);
			const bool bHung = IsHungBanner(P.Name);
			FVector At = Loc;
			if (bHung)
			{
				At.Z = 250.f;   // (the cloth alone, its top edge at the origin: hung from a rail)
				if (Cube) Static(Cube, FVector(Loc.X, Loc.Y, 254.f), FVector(P.W + 20.f, 6.f, 6.f) / 100.f, false);
			}
			Static(P.Mesh, At, FVector::OneVector, true)->SetCullDistance(6000.f);
			if (P.Flame) Static(P.Flame, At, FVector::OneVector, false)->SetCullDistance(6000.f);
			if (GivesLight(P.Name) && Lights < 20)
			{
				UPointLightComponent* L = NewObject<UPointLightComponent>(Owner);
				L->SetupAttachment(Root);
				L->SetRelativeLocation(Loc + FVector(0.f, 30.f, FMath::Max(25.f, P.H * (P.Name.Contains(TEXT("LampPost")) ? 0.9f : 0.75f))));
				L->SetIntensity(900.f);
				L->SetAttenuationRadius(450.f);
				L->SetLightColor(FLinearColor(1.f, 0.62f, 0.28f));
				L->SetCastShadows(false);
				L->RegisterComponent();
				++Lights;
			}
			Sign(P.Label, FVector(X + P.Slot * 0.5f, Y + 40.f, bHung ? 60.f : 90.f), P.LabelSize, FColor(255, 255, 255));
			X += P.Slot + Gap;
			++Count;
		}
		Y -= R.Depth + RowGap;
	}
	UE_LOG(LogTemp, Display, TEXT("JEEBFILLER %d pieces in %d rows, %d lights, yard Y %.0f..%.0f"), Count, Rows.Num(), Lights, YardTop, BottomY);
}

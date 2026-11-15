#include "TownLabLineup.h"
#include "TownLabDecor.h"
#include "OakHill.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "StaticMeshResources.h"

namespace
{
	float ScaleOf(const TCHAR* Model)   // the scale Oak Hill draws this design at (OakHill.h)
	{
		for (const JeebOakHill::FModel& M : JeebOakHill::Models())
			if (FCString::Strcmp(M.Name, Model) == 0) return M.Scale;
		return 1.f;
	}
	float ScaleZOf(const TCHAR* Model)   // its height's scale (the potion stall keeps its full height: FModel ScaleZ)
	{
		for (const JeebOakHill::FModel& M : JeebOakHill::Models())
			if (FCString::Strcmp(M.Name, Model) == 0) return M.ScaleZ > 0.f ? M.ScaleZ : M.Scale;
		return 1.f;
	}
	int32 Tris(UStaticMesh* M)
	{
		const FStaticMeshRenderData* R = M ? M->GetRenderData() : nullptr;
		return (R && R->LODResources.Num()) ? R->LODResources[0].GetNumTriangles() : 0;
	}
}

void JeebTownLab::BuildLineup(UWorld* World, AActor* Owner, USceneComponent* Root, float FloorZ)
{
	if (!World || !Owner || !Root) return;
	FString Set = TEXT("both");
	FParse::Value(FCommandLine::Get(), TEXT("TownLabSet="), Set);
	FString Group;   // -TownLabGroup=a: build-a's four pairs (bank, bank stall, smithy, corner house) instead of the house and store
	FParse::Value(FCommandLine::Get(), TEXT("TownLabGroup="), Group);
	const bool bGroupA = Group == TEXT("a");
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	const float YardY = 5600.f;
	if (Cube)   // a grass-coloured yard, 70 x 28 m, its top at the floor's height
	{
		UStaticMeshComponent* G = NewObject<UStaticMeshComponent>(Owner);
		G->SetStaticMesh(Cube);
		G->SetupAttachment(Root);
		G->SetRelativeLocation(FVector(0.f, YardY, -50.f));
		G->SetRelativeScale3D(FVector(bGroupA ? 150.f : 70.f, 28.f, 1.f));
		G->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		G->SetCollisionResponseToAllChannels(ECR_Block);
		G->RegisterComponent();
		if (Base)
			if (UMaterialInstanceDynamic* Mi = G->CreateDynamicMaterialInstance(0, Base)) Mi->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.20f, 0.30f, 0.14f));
	}
	if (Cube)   // build-b's two rows behind it (town hall and church, then potion shop and well): one more yard, 76 x 72 m
	{
		UStaticMeshComponent* G = NewObject<UStaticMeshComponent>(Owner);
		G->SetStaticMesh(Cube);
		G->SetupAttachment(Root);
		G->SetRelativeLocation(FVector(0.f, YardY + 5400.f, -50.f));
		G->SetRelativeScale3D(FVector(76.f, 72.f, 1.f));
		G->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		G->SetCollisionResponseToAllChannels(ECR_Block);
		G->RegisterComponent();
		if (Base)
			if (UMaterialInstanceDynamic* Mi = G->CreateDynamicMaterialInstance(0, Base)) Mi->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.20f, 0.30f, 0.14f));
	}
	struct FSpot { const TCHAR* Mesh; const TCHAR* Col; const TCHAR* Model; const TCHAR* Decor; const TCHAR* Sign; float X; bool bNew; float Row = 0.f; };   // (Row: cm behind the first row, +Y)
	const FSpot Spots[] = {
		{ TEXT("/Game/Buildings/SM_OakHillHouse_300k"), TEXT("/Game/Buildings/SM_OakHillHouse_300k_Col"), TEXT("OakHillHouse"), nullptr, TEXT("HOUSE: OLD (300k)"), -2700.f, false },
		{ TEXT("/Game/TownLab/Buildings/SM_TL_House"), TEXT("/Game/TownLab/Buildings/SM_TL_House_Col"), TEXT("OakHillHouse"), TEXT("TL_House"), TEXT("HOUSE: NEW"), -1000.f, true },
		{ TEXT("/Game/Buildings/SM_OakHillStore_300k"), TEXT("/Game/Buildings/SM_OakHillStore_300k_Col"), TEXT("OakHillStore"), nullptr, TEXT("STORE: OLD (300k)"), 900.f, false },
		{ TEXT("/Game/TownLab/Buildings/SM_TL_Store"), TEXT("/Game/TownLab/Buildings/SM_TL_Store_Col"), TEXT("OakHillStore"), TEXT("TL_Store"), TEXT("STORE: NEW"), 2800.f, true },
		// build-b (townbuild2): town hall and church 36 m behind, potion shop and well 74 m behind
		{ TEXT("/Game/Buildings/SM_OakHillTownHall_300k"), TEXT("/Game/Buildings/SM_OakHillTownHall_300k_Col"), TEXT("OakHillTownHall"), nullptr, TEXT("TOWN HALL: OLD"), -2600.f, false, 3600.f },
		{ TEXT("/Game/TownLab/Buildings/SM_TL_TownHall"), TEXT("/Game/TownLab/Buildings/SM_TL_TownHall_Col"), TEXT("OakHillTownHall"), TEXT("TL_TownHall"), TEXT("TOWN HALL: NEW"), -600.f, true, 3600.f },
		{ TEXT("/Game/Buildings/SM_OakHillChurch_300k"), TEXT("/Game/Buildings/SM_OakHillChurch_300k_Col"), TEXT("OakHillChurch"), nullptr, TEXT("CHURCH: OLD"), 1300.f, false, 3600.f },
		{ TEXT("/Game/TownLab/Buildings/SM_TL_Church"), TEXT("/Game/TownLab/Buildings/SM_TL_Church_Col"), TEXT("OakHillChurch"), TEXT("TL_Church"), TEXT("CHURCH: NEW"), 2900.f, true, 3600.f },
		{ TEXT("/Game/Buildings/SM_OakHillPotionShop_300k"), TEXT("/Game/Buildings/SM_OakHillPotionShop_300k_Col"), TEXT("OakHillPotionShop"), nullptr, TEXT("POTIONS: OLD"), -2200.f, false, 7400.f },
		{ TEXT("/Game/TownLab/Buildings/SM_TL_PotionShop"), TEXT("/Game/TownLab/Buildings/SM_TL_PotionShop_Col"), TEXT("OakHillPotionShop"), TEXT("TL_PotionShop"), TEXT("POTIONS: NEW"), -700.f, true, 7400.f },
		{ TEXT("/Game/Buildings/SM_OakHillWell_150k"), TEXT("/Game/Buildings/SM_OakHillWell_150k_Col"), TEXT("OakHillWell"), nullptr, TEXT("WELL: OLD"), 1000.f, false, 7400.f },
		{ TEXT("/Game/TownLab/Buildings/SM_TL_Well"), TEXT("/Game/TownLab/Buildings/SM_TL_Well_Col"), TEXT("OakHillWell"), TEXT("TL_Well"), TEXT("WELL: NEW"), 1800.f, true, 7400.f },
	};
	const FSpot SpotsA[] = {   // (build-a, 2026-11-14)
		{ TEXT("/Game/Buildings/SM_OakHillBank_300k"), TEXT("/Game/Buildings/SM_OakHillBank_300k_Col"), TEXT("OakHillBank"), nullptr, TEXT("BANK: OLD (300k)"), -6800.f, false },
		{ TEXT("/Game/TownLab/Buildings/SM_TL_Bank"), TEXT("/Game/TownLab/Buildings/SM_TL_Bank_Col"), TEXT("OakHillBank"), TEXT("TL_Bank"), TEXT("BANK: NEW"), -5000.f, true },
		{ TEXT("/Game/Buildings/SM_BankStall_300k"), TEXT("/Game/Buildings/SM_BankStall_300k_Col"), TEXT("BankStall"), nullptr, TEXT("STALL: OLD"), -3100.f, false },
		{ TEXT("/Game/TownLab/Buildings/SM_TL_BankStall"), TEXT("/Game/TownLab/Buildings/SM_TL_BankStall_Col"), TEXT("BankStall"), TEXT("TL_BankStall"), TEXT("STALL: NEW"), -1500.f, true },
		{ TEXT("/Game/Buildings/SM_OakHillSmith_300k"), TEXT("/Game/Buildings/SM_OakHillSmith_300k_Col"), TEXT("OakHillSmith"), nullptr, TEXT("SMITH: OLD"), 400.f, false },
		{ TEXT("/Game/TownLab/Buildings/SM_TL_Smith"), TEXT("/Game/TownLab/Buildings/SM_TL_Smith_Col"), TEXT("OakHillSmith"), TEXT("TL_Smith"), TEXT("SMITH: NEW"), 2200.f, true },
		{ TEXT("/Game/Buildings/SM_OakHillCornerHouse_300k"), TEXT("/Game/Buildings/SM_OakHillCornerHouse_300k_Col"), TEXT("OakHillCornerHouse"), nullptr, TEXT("CORNER: OLD"), 4100.f, false },
		{ TEXT("/Game/TownLab/Buildings/SM_TL_CornerHouse"), TEXT("/Game/TownLab/Buildings/SM_TL_CornerHouse_Col"), TEXT("OakHillCornerHouse"), TEXT("TL_CornerHouse"), TEXT("CORNER: NEW"), 5800.f, true },
	};
	// build-c (2026-11-14): -TownLabSet=stalls | stallsold | stallsnew shows the market stall and the potion stall, old and new, in the
	// same four spots instead
	const FSpot StallSpots[] = {
		{ TEXT("/Game/Buildings/SM_GeneralStore_300k"), TEXT("/Game/Buildings/SM_GeneralStore_300k_Col"), TEXT("GeneralStore"), nullptr, TEXT("MARKET STALL: OLD (300k)"), -2700.f, false },
		{ TEXT("/Game/TownLab/Buildings/SM_TL_MarketStall"), TEXT("/Game/TownLab/Buildings/SM_TL_MarketStall_Col"), TEXT("GeneralStore"), TEXT("TL_MarketStall"), TEXT("MARKET STALL: NEW"), -1000.f, true },
		{ TEXT("/Game/Buildings/SM_PotionStall_300k"), TEXT("/Game/Buildings/SM_PotionStall_300k_Col"), TEXT("PotionStall"), nullptr, TEXT("POTION STALL: OLD (300k)"), 900.f, false },
		{ TEXT("/Game/TownLab/Buildings/SM_TL_PotionStall"), TEXT("/Game/TownLab/Buildings/SM_TL_PotionStall_Col"), TEXT("PotionStall"), TEXT("TL_PotionStall"), TEXT("POTION STALL: NEW"), 2800.f, true },
	};
	const bool bStalls = Set.StartsWith(TEXT("stalls"));
	if (bStalls) Set = Set.Mid(6).IsEmpty() ? FString(TEXT("both")) : Set.Mid(6);
	for (const FSpot& S : bGroupA ? TArrayView<const FSpot>(SpotsA) : bStalls ? TArrayView<const FSpot>(StallSpots) : TArrayView<const FSpot>(Spots))
	{
		if ((Set == TEXT("old") && S.bNew) || (Set == TEXT("new") && !S.bNew)) continue;
		const FString Name = FPaths::GetBaseFilename(S.Mesh);
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("%s.%s"), S.Mesh, *Name));
		if (!Mesh) { UE_LOG(LogTemp, Warning, TEXT("JEEBTLB missing %s"), S.Mesh); continue; }
		const float Sc = ScaleOf(S.Model);
		const FTransform Rel(FRotator(0.f, 180.f, 0.f), FVector(S.X, YardY + S.Row, 0.f), FVector(Sc, Sc, ScaleZOf(S.Model)));   // (door, the model's +Y, toward the floor)
		UStaticMeshComponent* M = NewObject<UStaticMeshComponent>(Owner);
		M->SetStaticMesh(Mesh);
		M->SetupAttachment(Root);
		M->SetRelativeTransform(Rel);
		M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		M->SetCustomPrimitiveDataFloat(0, S.bNew ? 0.37f : 0.61f);   // (when its windows light at dusk: M_Building's HouseOffset)
		M->RegisterComponent();
		{	// (the windows as the town lights them: M_Building's "Night" is ADayNight::Dusk there; here 0 by day and 1 at a low-sun still)
			float Phase = 0.f;
			FParse::Value(FCommandLine::Get(), TEXT("ShotPhase="), Phase);
			for (int32 i = 0; i < M->GetNumMaterials(); ++i)
				if (UMaterialInstanceDynamic* Mi = M->CreateDynamicMaterialInstance(i)) Mi->SetScalarParameterValue(TEXT("Night"), Phase >= 0.35f ? 1.f : 0.f);
		}
		const FString ColName = FPaths::GetBaseFilename(S.Col);
		if (UStaticMesh* ColMesh = LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("%s.%s"), S.Col, *ColName)))
		{
			UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Owner);
			C->SetStaticMesh(ColMesh);
			C->SetupAttachment(Root);
			C->SetRelativeTransform(Rel);
			C->SetHiddenInGame(true);
			C->SetCastShadow(false);
			C->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			C->SetCollisionResponseToAllChannels(ECR_Block);
			C->RegisterComponent();
		}
		int32 DecorDraws = 0, DecorPieces = 0, DecorTris = 0, Lights = 0;
		if (const JeebTownLabDecor::FBuildingDecor* D = S.Decor ? JeebTownLabDecor::For(S.Decor) : nullptr)
		{
			// one instanced set per mesh and shadow setting: a draw per kind, as the town should do it
			TMap<FString, UInstancedStaticMeshComponent*> Sets;
			const FTransform World = Rel * Root->GetComponentTransform();
			for (int32 i = 0; i < D->Num; ++i)
			{
				const JeebTownLabDecor::FDecor& P = D->Pieces[i];
				if (!FString(P.Mesh).StartsWith(TEXT("/Game/TownLab")))
				{
					// (Oak Hill's filler props: one or two of each, and their materials aren't flagged for instancing, so plain components)
					UStaticMesh* PM = LoadObject<UStaticMesh>(nullptr, P.Mesh);
					if (!PM) { UE_LOG(LogTemp, Warning, TEXT("JEEBTLB missing decor %s"), P.Mesh); continue; }
					UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Owner);
					C->SetStaticMesh(PM);
					C->SetupAttachment(Root);
					C->SetWorldTransform(JeebTownLabDecor::Place(World, P));
					C->SetCastShadow(P.bCastShadow);
					C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
					C->SetCachedMaxDrawDistance(9000.f);
					C->RegisterComponent();
					++DecorDraws; ++DecorPieces; DecorTris += Tris(PM);
				}
				else
				{
				const FString Key = FString::Printf(TEXT("%s|%d"), P.Mesh, P.bCastShadow ? 1 : 0);
				UInstancedStaticMeshComponent*& Ism = Sets.FindOrAdd(Key);
				if (!Ism)
				{
					UStaticMesh* PM = LoadObject<UStaticMesh>(nullptr, P.Mesh);
					if (!PM) { UE_LOG(LogTemp, Warning, TEXT("JEEBTLB missing decor %s"), P.Mesh); continue; }
					Ism = NewObject<UInstancedStaticMeshComponent>(Owner);
					Ism->SetStaticMesh(PM);
					Ism->SetupAttachment(Root);
					Ism->SetCastShadow(P.bCastShadow);
					Ism->SetCollisionEnabled(ECollisionEnabled::NoCollision);
					Ism->SetCullDistances(0, 9000);
					Ism->RegisterComponent();
					++DecorDraws;
				}
				Ism->AddInstance(JeebTownLabDecor::Place(World, P), true);
				++DecorPieces;
				DecorTris += Tris(Ism->GetStaticMesh());
				}
				if (P.bLight)
				{
					UPointLightComponent* L = NewObject<UPointLightComponent>(Owner);
					L->SetupAttachment(Root);
					L->SetWorldLocation(JeebTownLabDecor::Place(World, P).GetLocation() + FVector(0.f, 0.f, 25.f));
					L->SetIntensityUnits(ELightUnits::Lumens);
					L->SetIntensity(70.f);
					L->SetAttenuationRadius(300.f);   // (a lantern's glow, not a spotlight on the whole front: visual review 2026-11-14)
					L->SetLightColor(FLinearColor(1.f, 0.55f, 0.24f));
					L->SetCastShadows(false);
					{	// (lit only at a low-sun still, as the windows: the town switches its lanterns on at dusk; build-b review 2026-11-14)
						float LightPhase = 0.f;
						FParse::Value(FCommandLine::Get(), TEXT("ShotPhase="), LightPhase);
						L->SetVisibility(LightPhase >= 0.35f);
					}
					L->RegisterComponent();
					++Lights;
				}
			}
		}
		UE_LOG(LogTemp, Display, TEXT("JEEBTLB %s scale %.2f nanite %d fallback tris %d materials %d | decor pieces %d draws %d tris %d lights %d"),
			*Name, Sc, Mesh->HasValidNaniteData() ? 1 : 0, Tris(Mesh), Mesh->GetStaticMaterials().Num(), DecorPieces, DecorDraws, DecorTris, Lights);
		UTextRenderComponent* Sign = NewObject<UTextRenderComponent>(Owner);
		Sign->SetupAttachment(Root);
		Sign->SetRelativeLocation(FVector(S.X, YardY + S.Row - 900.f, S.Row > 0.f ? 2600.f : 1500.f));
		Sign->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
		Sign->SetHorizontalAlignment(EHTA_Center);
		Sign->SetWorldSize(70.f);
		Sign->SetTextRenderColor(FColor::White);
		Sign->SetText(FText::FromString(S.Sign));
		Sign->SetCastShadow(false);
		Sign->RegisterComponent();
	}
}

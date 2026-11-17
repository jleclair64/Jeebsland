#include "TreeGroves.h"

#if UE_BUILD_SHIPPING
void JeebGroves::Build(UWorld*, AForest*) {}
#else

#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "Engine/SkeletalMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "HAL/IConsoleManager.h"
#include "Terrain.h"
#include "WorldMap.h"
#include "OakHill.h"
#include "Canopy.h"
#include "Forest.h"

// Sample groves (TreeGroves.h). One row of small groves running north-south on open meadow east of Oak Hill, each about 23 m across, the signs on
// their west side facing the town. Everything is code-built at load and not solid (nothing here may stop a player the server doesn't see stop).
namespace
{
	TAutoConsoleVariable<int32> CVarGroves(TEXT("jeeb.Test.Groves"), 1,
		TEXT("TEMPORARY test setting: 1 plants the sample tree groves east of Oak Hill at the next load, 0 leaves them out."), ECVF_Default);

	constexpr float GroveR = 1150.f;     // a grove's radius (23 m across)
	constexpr float Pitch = 3700.f;      // from one grove's middle to the next's along the row (13.5 m between neighbours' edges)

	// Autumn leaf colours the Megaplants and the DZ broadleaves are tinted with, and the index of the world's fallen-leaf colour that goes with each
	// (AutumnDecor.cpp: 0 orange, 1 red, 2 amber, 3 gold, 4 green holdout)
	struct FAutumn { FLinearColor Tint; int32 Leaf; FLinearColor Dz; };   // (Dz: multiplies the DZ materials' own red fall colour: more green lifts it toward orange and gold)
	const FAutumn GAutumn[] = {
		{ FLinearColor(1.00f, 0.55f, 0.20f), 0, FLinearColor(1.f, 3.f, 0.5f) }, { FLinearColor(0.95f, 0.30f, 0.15f), 1, FLinearColor(1.f, 1.f, 1.f) }, { FLinearColor(1.00f, 0.72f, 0.22f), 2, FLinearColor(1.f, 5.f, 0.5f) },
		{ FLinearColor(0.95f, 0.80f, 0.30f), 3, FLinearColor(1.f, 8.f, 0.6f) }, { FLinearColor(0.80f, 0.90f, 0.40f), 4, FLinearColor(0.7f, 6.f, 1.f) },
	};

	enum class ECover : uint8 { None, Broad, Conifer };   // what the grass and the fallen leaves do under it
	struct FKind
	{
		const TCHAR* Path;   // asset path
		int32 Count;
		float S0, S1;        // scale range
		ECover Cover;
		bool bAutumn;        // tinted with GAutumn (the pack has no autumn material of its own)
		bool bUnder;         // understory: planted after the big trees, close between them
	};
	struct FGrove
	{
		const TCHAR* Title;
		const TCHAR* Source;
		TArray<FKind> Kinds;
		int32 Seed;
	};

	struct FPlan { UObject* Asset; const FKind* K; FVector2D P; float Scale, Yaw, Crown; int32 Autumn; };

	// Gives a tree's leaf materials their autumn twins (/Game/TreeGroves, Tools/grove_materials.py: the pack's own leaf material with its fall or season
	// switch on) and this tree's own colour. Other slots (bark, branches) keep the pack's materials.
	int32 ApplyFall(UMeshComponent* C, const FAutumn& A)
	{
		static const struct { const TCHAR* From; const TCHAR* To; bool bSeasons; } Swaps[] = {
			{ TEXT("MI_Aspen_Leaves_Big_1"), TEXT("MI_Fall_AspenLeavesBig"), false },
			{ TEXT("MI_Aspen_Leaves_Small_7"), TEXT("MI_Fall_AspenLeavesSmall"), false },
			{ TEXT("MI_Cork_Oak_Leaves_1"), TEXT("MI_Fall_CorkOakLeaves"), false },
		};
		int32 Done = 0;
		for (int32 i = 0; i < C->GetNumMaterials(); ++i)
		{
			UMaterialInterface* M = C->GetMaterial(i);
			if (!M) continue;
			for (const auto& S : Swaps)
			{
				if (M->GetName() != S.From) continue;
				UMaterialInterface* Fall = LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("/Game/TreeGroves/%s.%s"), S.To, S.To), nullptr, LOAD_Quiet | LOAD_NoWarn);
				if (!Fall) break;
				UMaterialInstanceDynamic* D = UMaterialInstanceDynamic::Create(Fall, C);
				if (S.bSeasons)
				{
					D->SetVectorParameterValue(TEXT("Season Color 1"), A.Tint * 0.55f);
					D->SetVectorParameterValue(TEXT("Season Color 2"), A.Tint);
					D->SetScalarParameterValue(TEXT("Season Strength"), 1.f);
					// (the seasons switch alone left the leaves green in the first picture run: the tint and the health overlay say the colour outright)
					D->SetScalarParameterValue(TEXT("Health Offset"), 1.f);
					D->SetVectorParameterValue(TEXT("Health Color Overlay"), A.Tint * 0.6f);
					D->SetVectorParameterValue(TEXT("BaseColor Tint"), A.Tint * 0.5f);
				}
				else D->SetVectorParameterValue(TEXT("Fall Color Tint"), A.Dz);
				C->SetMaterial(i, D);
				++Done;
				break;
			}
		}
		return Done;
	}

	// Validity of a grove spot (all in cm): dry land, not on a road, a building, Oak Hill's own ground or the river's valley, and nearly level.
	// Returns 0 when fine, else the count of failed samples.
	int32 SpotProblems(const FVector2D& C, float R, float& OutRise)
	{
		int32 Bad = 0;
		float Lo = 1.0e9f, Hi = -1.0e9f;
		for (int32 i = 0; i < 9; ++i)
		{
			const FVector2D Q = i == 0 ? C : C + FVector2D(FMath::Cos(i * PI / 4.f), FMath::Sin(i * PI / 4.f)) * R * 1.3f;
			const float H = JeebTerrain::HeightAt(Q.X, Q.Y);
			Lo = FMath::Min(Lo, H); Hi = FMath::Max(Hi, H);
			float Half;
			if (H < JeebWorld::RiverLevel + 250.f) ++Bad;
			if (JeebWorld::InWater(Q.X, Q.Y)) ++Bad;
			if (JeebWorld::RiverDistance(Q.X, Q.Y, Half) < Half + 2500.f) ++Bad;
			if (JeebTerrain::PathClearance(Q.X, Q.Y) < 700.f) ++Bad;
			if (JeebTerrain::BuildingNearness(Q.X, Q.Y) > 0.f || JeebTerrain::NoGroundCover(Q.X, Q.Y)) ++Bad;
			if (JeebOakHill::InBox(Q.X, Q.Y, 1500.f)) ++Bad;
		}
		OutRise = Hi - Lo;
		if (OutRise > 600.f) ++Bad;
		return Bad;
	}

	// ------------------------------------------------------------------ Spruce Forest LOD fix (2026-11-16, owner: "these two smaller pine trees never seem to want to load in")
	// Some of the pack's lower LODs draw their needle cards with a bark or branch material (no opacity mask), so the needles show as flat tan cards:
	// the saplings spruce_small_02-04 from LOD 1 (LOD 0 only shows when the tree fills the screen, so they stood as cards until the player was right on
	// them), and LOD 3 (mid-distance) of the young spruces spruce_small_05-07 and of spruce_half_01/03/04. Such a LOD has no leaf or imposter material on
	// any section. A tree with one holds LOD 1 when that is still whole, else LOD 0 (the saplings: 133-206 triangles; young spruces 1.3k-3.2k at LOD 1,
	// half-grown 10k-14k). The real fix is in the pack (each such section's material slot set to the leaf slot); see ProjectNotes "Tree groves".
	TAutoConsoleVariable<int32> CVarSpruceFix(TEXT("jeeb.Test.SpruceFix"), 1,
		TEXT("TEMPORARY test setting: 1 holds a whole LOD on the Spruce Forest trees whose lower LODs draw the needles with the bark material; 0 leaves the pack's LODs as they are (live)."), ECVF_Default);
	TArray<TPair<TWeakObjectPtr<UStaticMeshComponent>, int32>> GSpruceFixed;   // (tree, the LOD it holds)

	// The first LOD (after 0) with no leaf or imposter material on any section, or -1
	int32 FirstNeedlelessLod(const UStaticMeshComponent* C)
	{
		const UStaticMesh* Sm = C->GetStaticMesh();
		const FStaticMeshRenderData* RD = Sm ? Sm->GetRenderData() : nullptr;
		if (!RD) return -1;
		for (int32 l = 1; l < RD->LODResources.Num(); ++l)
		{
			bool bNeedles = false;
			for (const FStaticMeshSection& S : RD->LODResources[l].Sections)
			{
				const UMaterialInterface* M = C->GetMaterial(S.MaterialIndex);
				const FString N = M ? M->GetName() : FString();
				bNeedles |= N.Contains(TEXT("leaf")) || N.Contains(TEXT("imposter"));
			}
			if (!bNeedles) return l;
		}
		return -1;
	}

	void SpruceFix(UStaticMeshComponent* C)
	{
		static const bool bHook = []() { CVarSpruceFix->SetOnChangedCallback(FConsoleVariableDelegate::CreateLambda([](IConsoleVariable*)
			{ for (const auto& T : GSpruceFixed) if (T.Key.IsValid()) T.Key->SetForcedLodModel(CVarSpruceFix.GetValueOnGameThread() ? T.Value + 1 : 0); })); return true; }();
		(void)bHook;
		const int32 Bad = FirstNeedlelessLod(C);
		if (Bad < 0) return;
		const int32 Hold = Bad >= 2 ? 1 : 0;   // (LOD 1 is what a player mostly sees close up: LOD 0 starts at screen size 1)
		GSpruceFixed.Add({ C, Hold });
		C->SetForcedLodModel(CVarSpruceFix.GetValueOnGameThread() ? Hold + 1 : 0);   // (ForcedLodModel is the LOD + 1; 0 = automatic)
		const FStaticMeshRenderData* RD = C->GetStaticMesh()->GetRenderData();
		UE_LOG(LogTemp, Display, TEXT("JEEBSPRUCE %s at %.0f %.0f: LOD %d (from screen size %.2f) has no needle material, holding LOD %d (%d triangles)"), *C->GetStaticMesh()->GetName(),
			C->GetComponentLocation().X, C->GetComponentLocation().Y, Bad, RD->ScreenSize[Bad].Default, Hold, RD->LODResources[Hold].GetNumTriangles());
	}
}

void JeebGroves::Build(UWorld* World, AForest* Forest)
{
	if (!World || CVarGroves.GetValueOnGameThread() == 0) return;
	const bool bServer = World->GetNetMode() == NM_DedicatedServer;

	// ------------------------------------------------------------------ what each grove holds
	TArray<FGrove> Groves;
	{
		const FKind S[] = {   // Spruce Forest (Project Nature): three full-size spruces, four half-grown, saplings between
			{ TEXT("/Game/PN_interactiveSpruceForest/Meshes/full/high/spruce_full_01.spruce_full_01"), 1, 0.9f, 1.15f, ECover::Conifer, false, false },
			{ TEXT("/Game/PN_interactiveSpruceForest/Meshes/full/high/spruce_full_02.spruce_full_02"), 1, 0.9f, 1.15f, ECover::Conifer, false, false },
			{ TEXT("/Game/PN_interactiveSpruceForest/Meshes/full/high/spruce_full_03.spruce_full_03"), 1, 0.9f, 1.15f, ECover::Conifer, false, false },
			{ TEXT("/Game/PN_interactiveSpruceForest/Meshes/half/high/spruce_half_01.spruce_half_01"), 1, 0.9f, 1.2f, ECover::Conifer, false, false },
			{ TEXT("/Game/PN_interactiveSpruceForest/Meshes/half/high/spruce_half_02.spruce_half_02"), 1, 0.9f, 1.2f, ECover::Conifer, false, false },
			{ TEXT("/Game/PN_interactiveSpruceForest/Meshes/half/high/spruce_half_03.spruce_half_03"), 1, 0.9f, 1.2f, ECover::Conifer, false, false },
			{ TEXT("/Game/PN_interactiveSpruceForest/Meshes/half/high/spruce_half_04.spruce_half_04"), 1, 0.9f, 1.2f, ECover::Conifer, false, false },
			{ TEXT("/Game/PN_interactiveSpruceForest/Meshes/small/spruce_small_05.spruce_small_05"), 1, 0.9f, 1.2f, ECover::Conifer, false, false },
			{ TEXT("/Game/PN_interactiveSpruceForest/Meshes/small/spruce_small_06.spruce_small_06"), 1, 0.9f, 1.2f, ECover::Conifer, false, false },
			{ TEXT("/Game/PN_interactiveSpruceForest/Meshes/small/spruce_small_07.spruce_small_07"), 1, 0.9f, 1.2f, ECover::Conifer, false, false },
			{ TEXT("/Game/PN_interactiveSpruceForest/Meshes/small/spruce_small_01.spruce_small_01"), 2, 0.9f, 1.4f, ECover::None, false, true },
			{ TEXT("/Game/PN_interactiveSpruceForest/Meshes/small/spruce_small_02.spruce_small_02"), 2, 0.9f, 1.4f, ECover::None, false, true },
			{ TEXT("/Game/PN_interactiveSpruceForest/Meshes/small/spruce_small_03.spruce_small_03"), 2, 0.9f, 1.4f, ECover::None, false, true },
			{ TEXT("/Game/PN_interactiveSpruceForest/Meshes/small/spruce_small_04.spruce_small_04"), 2, 0.9f, 1.4f, ECover::None, false, true },
		};
		Groves.Add(FGrove{ TEXT("SPRUCE FOREST"), TEXT("Project Nature, Fab"), TArray<FKind>(S, UE_ARRAY_COUNT(S)), 31 });
		const FKind D[] = {   // DZ Sample Trees: aspen (gold in autumn), cork oak, pine, and its two palms
			{ TEXT("/Game/DZ_Assets/DZ_Trees/Meshes/Aspen/SM_Columnar_Aspen_1.SM_Columnar_Aspen_1"), 3, 0.85f, 1.15f, ECover::Broad, true, false },
			{ TEXT("/Game/DZ_Assets/DZ_Trees/Meshes/Cork_Oak/SM_Cork_Oak_1.SM_Cork_Oak_1"), 2, 0.85f, 1.15f, ECover::Broad, true, false },
			{ TEXT("/Game/DZ_Assets/DZ_Trees/Meshes/Pine/SM_Pine_1.SM_Pine_1"), 3, 0.85f, 1.15f, ECover::Conifer, false, false },
			{ TEXT("/Game/DZ_Assets/DZ_Trees/Meshes/Coconut_Tree/SM_Coconut_Tree_1.SM_Coconut_Tree_1"), 1, 1.f, 1.f, ECover::None, false, false },
			{ TEXT("/Game/DZ_Assets/DZ_Trees/Meshes/Windmill_Palm/SM_Windmill_Palm_1.SM_Windmill_Palm_1"), 1, 1.f, 1.f, ECover::None, false, false },
		};
		Groves.Add(FGrove{ TEXT("DZ SAMPLE TREES"), TEXT("DZ Assets, Fab"), TArray<FKind>(D, UE_ARRAY_COUNT(D)), 32 });
		// (The Megaplants English Oak and Hornbeam groves went 2026-11-16, owner: keep only DZ and the Spruce Forest; the pack is deleted.)
	}
	const int32 NumGroves = Groves.Num() + 1;   // (+ the world's own trees, the last grove)

	// ------------------------------------------------------------------ where the row goes
	// A line of groves running north-south (+X), centred on a spot east of Oak Hill's trees; the first candidate with every grove on good meadow wins
	const FVector2D Candidates[] = {
		FVector2D(-2000.f, 31000.f), FVector2D(-1000.f, 33000.f), FVector2D(-4000.f, 33000.f), FVector2D(0.f, 35000.f), FVector2D(-3000.f, 36000.f),
		FVector2D(1500.f, 32000.f), FVector2D(-5000.f, 31000.f), FVector2D(2500.f, 36000.f),
	};
	int32 BestBad = INT32_MAX, Best = 0;
	for (int32 c = 0; c < UE_ARRAY_COUNT(Candidates) && BestBad > 0; ++c)
	{
		int32 Bad = 0;
		for (int32 g = 0; g < NumGroves; ++g)
		{
			float Rise;
			Bad += SpotProblems(Candidates[c] + FVector2D((g - (NumGroves - 1) * 0.5f) * Pitch, 0.f), GroveR, Rise);
		}
		if (Bad < BestBad) { BestBad = Bad; Best = c; }
	}
	const FVector2D Row = Candidates[Best];
	UE_LOG(LogTemp, Display, TEXT("JEEBGROVE site: row centre %.0f %.0f (candidate %d, %d problem samples), %d groves %.0f cm apart, signs on the west side"), Row.X, Row.Y, Best, BestBad, NumGroves, Pitch);

	AActor* Owner = World->SpawnActor<AActor>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!Owner) return;
	USceneComponent* Root = NewObject<USceneComponent>(Owner, TEXT("GroveRoot"));
	Owner->SetRootComponent(Root);
	Root->RegisterComponent();
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UMaterialInterface* Basic = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	auto Sign = [&](const FVector& At, const FString& Title, const FString& Detail)
	{
		if (bServer) return;
		auto Text = [&](const FString& S, float Z, float Size, const FColor& Col)
		{
			UTextRenderComponent* T = NewObject<UTextRenderComponent>(Owner);
			T->SetupAttachment(Root);
			T->SetRelativeLocation(At + FVector(0.f, -14.f, Z));
			T->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));   // (faces -Y: read from the west, walking in from the town)
			T->SetHorizontalAlignment(EHTA_Center);
			T->SetVerticalAlignment(EVRTA_TextCenter);
			T->SetWorldSize(Size);
			T->SetTextRenderColor(Col);
			T->SetText(FText::FromString(S));
			T->SetCastShadow(false);
			T->RegisterComponent();
		};
		Text(Title, 238.f, 58.f, FColor(255, 238, 130));
		Text(Detail, 176.f, 34.f, FColor(255, 255, 255));
		if (Cube && Basic)
		{
			auto Block = [&](const FVector& Loc, const FVector& Size, const FLinearColor& Col)
			{
				UStaticMeshComponent* B = NewObject<UStaticMeshComponent>(Owner);
				B->SetStaticMesh(Cube);
				B->SetupAttachment(Root);
				B->SetRelativeLocation(Loc);
				B->SetRelativeScale3D(Size / 100.f);
				B->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				B->SetCastShadow(false);
				B->RegisterComponent();
				if (UMaterialInstanceDynamic* M = B->CreateDynamicMaterialInstance(0, Basic)) M->SetVectorParameterValue(TEXT("Color"), Col);
			};
			Block(At + FVector(0.f, 0.f, 215.f), FVector(430.f, 8.f, 120.f), FLinearColor(0.07f, 0.04f, 0.02f));   // the board
			Block(At + FVector(-170.f, 8.f, 80.f), FVector(14.f, 14.f, 160.f), FLinearColor(0.10f, 0.06f, 0.03f));   // its two posts
			Block(At + FVector(170.f, 8.f, 80.f), FVector(14.f, 14.f, 160.f), FLinearColor(0.10f, 0.06f, 0.03f));
		}
	};

	// ------------------------------------------------------------------ plant each grove
	for (int32 g = 0; g < NumGroves; ++g)
	{
		const FVector2D C = Row + FVector2D((g - (NumGroves - 1) * 0.5f) * Pitch, 0.f);
		const bool bOwn = g == NumGroves - 1;
		FRandomStream R(bOwn ? 35 : Groves[g].Seed);
		const float GroundC = JeebTerrain::HeightAt(C.X, C.Y);
		int32 Planted = 0, Skipped = 0, Falls = 0;
		TMap<FString, int32> Names;
		FString Title, Detail;

		if (bOwn)
		{
			// The world's own trees, as the woods plant them (broad-leaf oaks in the autumn colours and pines; the redwoods stay in their own groves)
			Title = TEXT("OUR OWN TREES");
			Detail = TEXT("the world's woods today");
			if (Forest)
			{
				TArray<FVector2D> At;
				const int32 Want = 11;
				for (int32 i = 0; i < Want; ++i)
				{
					const bool bPine = i >= 7;
					FVector2D P;
					bool bOk = false;
					for (int32 t = 0; t < 200 && !bOk; ++t)
					{
						const float A = R.FRandRange(0.f, 2.f * PI), D = GroveR * FMath::Sqrt(R.FRand()) * 0.9f;
						P = C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * D;
						bOk = true;
						for (const FVector2D& Q : At) if (FVector2D::Distance(P, Q) < (bPine ? 330.f : 480.f)) { bOk = false; break; }
					}
					if (!bOk) { ++Skipped; continue; }
					At.Add(P);
					const float S = R.FRandRange(0.85f, 1.25f);
					Forest->AddTree(bPine ? ETreeKind::Pine : ETreeKind::Oak, FVector(P.X, P.Y, JeebTerrain::HeightAt(P.X, P.Y) - 20.f * S), S, R);
					++Planted;
					Names.FindOrAdd(bPine ? TEXT("pine") : TEXT("oak")) += 1;
				}
			}
		}
		else
		{
			const FGrove& G = Groves[g];
			Title = G.Title;
			TArray<FPlan> Plans;
			for (int32 pass = 0; pass < 2; ++pass)   // (big trees first, then the understory)
			{
				for (const FKind& K : G.Kinds)
				{
					if (K.bUnder != (pass == 1)) continue;
					UObject* Asset = LoadObject<UObject>(nullptr, K.Path, nullptr, LOAD_Quiet | LOAD_NoWarn);
					FBoxSphereBounds B(ForceInit);
					if (UStaticMesh* Sm = Cast<UStaticMesh>(Asset)) B = Sm->GetBounds();
					else if (USkeletalMesh* Sk = Cast<USkeletalMesh>(Asset)) B = Sk->GetBounds();
					else { UE_LOG(LogTemp, Warning, TEXT("JEEBGROVE %s: %s did not load"), G.Title, K.Path); Skipped += K.Count; continue; }
					for (int32 n = 0; n < K.Count; ++n)
					{
						FPlan Pl;
						Pl.Asset = Asset; Pl.K = &K;
						Pl.Scale = R.FRandRange(K.S0, K.S1);
						Pl.Yaw = R.FRandRange(0.f, 360.f);
						Pl.Crown = FMath::Max(40.f, 0.8f * FMath::Min(B.BoxExtent.X, B.BoxExtent.Y) * Pl.Scale);
						Pl.Autumn = K.bAutumn ? R.RandRange(0, UE_ARRAY_COUNT(GAutumn) - 2) + (R.FRand() < 0.1f ? 1 : 0) : -1;
						// a spot in the grove, not touching the others (crowns may overlap a little, as trees do)
						float Gap = K.bUnder ? 0.5f : 0.8f;
						bool bOk = false;
						for (int32 t = 0; t < 400 && !bOk; ++t)
						{
							if (t > 0 && t % 100 == 0) Gap *= 0.8f;
							const float A = R.FRandRange(0.f, 2.f * PI), D = GroveR * FMath::Sqrt(R.FRand()) * (Plans.Num() == 0 ? 0.3f : 0.92f);
							Pl.P = C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * D;
							bOk = true;
							for (const FPlan& Q : Plans) if (FVector2D::Distance(Pl.P, Q.P) < Gap * (Pl.Crown + (Q.K->bUnder ? 0.5f * Q.Crown : Q.Crown))) { bOk = false; break; }
						}
						if (!bOk) { ++Skipped; continue; }
						Plans.Add(Pl);
						// the fallen leaves and thinner grass under the crown (the world's own canopy record: Canopy.h)
						if (Pl.K->Cover != ECover::None)
							JeebCanopy::Add(Pl.P, FMath::Clamp(Pl.Crown * (Pl.K->Cover == ECover::Broad ? 1.f : 0.7f), 160.f, 1100.f), Pl.K->Cover == ECover::Broad ? (Pl.Autumn >= 0 ? GAutumn[Pl.Autumn].Leaf : 3) : -1);
					}
				}
			}
			Detail = FString::Printf(TEXT("%s, %d trees"), G.Source, Plans.Num());
			if (!bServer)
			{
				for (const FPlan& Pl : Plans)
				{
					const float Ground = JeebTerrain::HeightAt(Pl.P.X, Pl.P.Y);
					UMeshComponent* Comp = nullptr;
					if (UStaticMesh* Sm = Cast<UStaticMesh>(Pl.Asset))
					{
						UStaticMeshComponent* M = NewObject<UStaticMeshComponent>(Owner);
						M->SetStaticMesh(Sm);
						Comp = M;
					}
					else if (USkeletalMesh* Sk = Cast<USkeletalMesh>(Pl.Asset))
					{
						USkeletalMeshComponent* M = NewObject<USkeletalMeshComponent>(Owner);
						M->SetSkeletalMeshAsset(Sk);
						Comp = M;
					}
					if (!Comp) continue;
					Comp->SetupAttachment(Root);
					Comp->SetRelativeLocation(FVector(Pl.P.X, Pl.P.Y, Ground - 4.f));   // (every pack puts the trunk base on the pivot)
					Comp->SetRelativeRotation(FRotator(0.f, Pl.Yaw, 0.f));
					Comp->SetRelativeScale3D(FVector(Pl.Scale));
					Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
					Comp->RegisterComponent();
					if (G.Seed == 31) if (UStaticMeshComponent* Sp = Cast<UStaticMeshComponent>(Comp)) SpruceFix(Sp);   // (the Spruce Forest: lower LODs without needles)
					if (Pl.Autumn >= 0) Falls += ApplyFall(Comp, GAutumn[Pl.Autumn]);
				}
			}
			for (const FPlan& Pl : Plans) Names.FindOrAdd(FPaths::GetBaseFilename(Pl.K->Path)) += 1;
			Planted = Plans.Num();
		}
		Sign(FVector(C.X, C.Y - GroveR - 120.f, GroundC), Title, bOwn ? Detail : Detail);
		FString List;
		for (const TPair<FString, int32>& N : Names) List += FString::Printf(TEXT(" %s x%d"), *N.Key, N.Value);
		UE_LOG(LogTemp, Display, TEXT("JEEBGROVE %d %s at %.0f %.0f (ground %.0f): %d trees, %d skipped, %d autumn leaf materials:%s"), g, *Title, C.X, C.Y, GroundC, Planted, Skipped, Falls, *List);
	}
}

#endif

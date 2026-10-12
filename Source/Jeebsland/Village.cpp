#include "Village.h"
#include "Cottage.h"
#include "Terrain.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/World.h"
#include "Npc.h"

AVillage::AVillage()
{
	PrimaryActorTick.bCanEverTick = true;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	ConstructorHelpers::FObjectFinder<UStaticMesh> CylMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	ConstructorHelpers::FObjectFinder<UStaticMesh> ConeMesh(TEXT("/Engine/BasicShapes/Cone.Cone"));

	Log       = MakeISM(TEXT("Log"),       CylMesh.Object,  FLinearColor(0.28f, 0.16f, 0.07f), true);
	Tip       = MakeISM(TEXT("Tip"),       ConeMesh.Object, FLinearColor(0.22f, 0.12f, 0.05f), true);
	Beam      = MakeISM(TEXT("Beam"),      CubeMesh.Object, FLinearColor(0.20f, 0.11f, 0.05f), true);
	Stone     = MakeISM(TEXT("Stone"),     CubeMesh.Object, FLinearColor(0.30f, 0.30f, 0.33f), true);
	StoneRing = MakeISM(TEXT("StoneRing"), CylMesh.Object,  FLinearColor(0.34f, 0.34f, 0.37f), true);
	Roof      = MakeISM(TEXT("Roof"),      ConeMesh.Object, FLinearColor(0.38f, 0.10f, 0.06f), true);
	Water     = MakeISM(TEXT("Water"),     CylMesh.Object,  FLinearColor(0.04f, 0.10f, 0.18f), false);
	Plank     = MakeISM(TEXT("Plank"),     CubeMesh.Object, FLinearColor(0.36f, 0.22f, 0.10f), true);
	Awning    = MakeISM(TEXT("Awning"),    CubeMesh.Object, FLinearColor(0.60f, 0.10f, 0.07f), true);
	Gold      = MakeISM(TEXT("Gold"),      CubeMesh.Object, FLinearColor(0.95f, 0.70f, 0.10f), true);
	Ember     = MakeISM(TEXT("Ember"),     CubeMesh.Object, FLinearColor(5.0f, 1.5f, 0.2f), false);      // bright: glows
	Drops     = MakeISM(TEXT("Drops"),     CylMesh.Object,  FLinearColor(0.7f, 1.0f, 1.5f), false);
	Drops->SetMobility(EComponentMobility::Movable);
	Iron      = MakeISM(TEXT("Iron"),      CubeMesh.Object, FLinearColor(0.10f, 0.10f, 0.12f), true);
}

UInstancedStaticMeshComponent* AVillage::MakeISM(const TCHAR* Name, UStaticMesh* Mesh, const FLinearColor& Color, bool bShadow)
{
	UInstancedStaticMeshComponent* C = CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
	C->SetupAttachment(RootComponent);
	C->SetStaticMesh(Mesh);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision); // solid parts use the invisible boxes below
	C->SetCanEverAffectNavigation(false);
	C->SetCastShadow(bShadow);
	C->bAffectDynamicIndirectLighting = false;
	C->bAffectDistanceFieldLighting = false;
	Tints.Add(TPair<UInstancedStaticMeshComponent*, FLinearColor>(C, Color));
	return C;
}

void AVillage::AddSolid(const FVector& Center, const FVector& HalfExtent, const FQuat& Rot)
{
	UBoxComponent* B = NewObject<UBoxComponent>(this);
	B->SetupAttachment(RootComponent);
	B->SetBoxExtent(HalfExtent);
	B->SetRelativeLocationAndRotation(Center, Rot);
	B->SetCollisionProfileName(TEXT("BlockAll"));
	B->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	B->RegisterComponent();
}

void AVillage::BeginPlay()
{
	Super::BeginPlay();
	SetActorLocation(FVector::ZeroVector);

	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"));
	// Logs, stakes, beams and planks use real-looking wood (grain up for the logs, along for the beams), if the materials are built
	UMaterialInterface* WoodV = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_WoodV.M_WoodV"));
	UMaterialInterface* WoodH = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_WoodH.M_WoodH"));
	for (const TPair<UInstancedStaticMeshComponent*, FLinearColor>& T : Tints)
	{
		UMaterialInterface* Mat = Base;
		if (T.Key == Log || T.Key == Tip) { if (WoodV) Mat = WoodV; }
		else if (T.Key == Beam || T.Key == Plank) { if (WoodH) Mat = WoodH; }
		if (UMaterialInstanceDynamic* M = T.Key->CreateDynamicMaterialInstance(0, Mat))
		{
			M->SetVectorParameterValue(TEXT("Color"), T.Value);
		}
	}

	TMap<UInstancedStaticMeshComponent*, TArray<FTransform>> Batch;
	auto Put = [&](UInstancedStaticMeshComponent* C, const FTransform& T) { Batch.FindOrAdd(C).Add(T); };
	auto Ground = [](float X, float Y) { return JeebTerrain::HeightAt(X, Y); };
	FRandomStream R(31337);

	const FVector2D VC = JeebTerrain::VillageCenter();
	const float Half = JeebTerrain::VillageHalf(); // the wall is a square, 4000 wide
	const float GateHalf = 170.f;  // gate openings are 340 wide

	// ---------------- Palisade: one side at a time ----------------
	auto Side = [&](FVector2D C, FVector2D Dir, FVector2D In, bool bGate)
	{
		const FQuat Q = FRotator(0.f, FVector(Dir.X, Dir.Y, 0.f).Rotation().Yaw, 0.f).Quaternion();

		for (float U = -Half; U <= Half; U += 44.f)
		{
			if (bGate && FMath::Abs(U) < GateHalf) continue;
			const FVector2D P = C + Dir * U;
			const float Z = Ground(P.X, P.Y);
			const float H = R.FRandRange(330.f, 400.f);
			Put(Log, FTransform(FQuat::Identity, FVector(P.X, P.Y, Z + H * 0.5f), FVector(0.4f, 0.4f, H / 100.f)));
			Put(Tip, FTransform(FQuat::Identity, FVector(P.X, P.Y, Z + H + 18.f), FVector(0.4f, 0.4f, 0.4f)));
		}

		TArray<TPair<float, float>> Pieces;
		if (bGate)
		{
			Pieces.Add(TPair<float, float>(-Half, -GateHalf));
			Pieces.Add(TPair<float, float>(GateHalf, Half));
		}
		else
		{
			Pieces.Add(TPair<float, float>(-Half, Half));
		}
		for (const TPair<float, float>& Pc : Pieces)
		{
			const float Len = Pc.Value - Pc.Key;
			const FVector2D Mid = C + Dir * ((Pc.Key + Pc.Value) * 0.5f);
			const float Z = Ground(Mid.X, Mid.Y);
			const FVector2D RailPos = Mid + In * 26.f;
			Put(Beam, FTransform(Q, FVector(RailPos.X, RailPos.Y, Z + 130.f), FVector(Len / 100.f, 0.16f, 0.2f)));
			Put(Beam, FTransform(Q, FVector(RailPos.X, RailPos.Y, Z + 270.f), FVector(Len / 100.f, 0.16f, 0.2f)));
			AddSolid(FVector(Mid.X, Mid.Y, Z + 200.f), FVector(Len * 0.5f, 25.f, 200.f), Q);
		}

		if (bGate)
		{
			for (int32 s = -1; s <= 1; s += 2)
			{
				const FVector2D P = C + Dir * (s * GateHalf);
				const float Z = Ground(P.X, P.Y);
				Put(Log, FTransform(FQuat::Identity, FVector(P.X, P.Y, Z + 260.f), FVector(0.8f, 0.8f, 5.2f)));
				Put(Tip, FTransform(FQuat::Identity, FVector(P.X, P.Y, Z + 540.f), FVector(0.8f, 0.8f, 0.8f)));
				AddSolid(FVector(P.X, P.Y, Z + 260.f), FVector(40.f, 40.f, 260.f), Q);
			}
			const float Zc = Ground(C.X, C.Y);
			Put(Beam, FTransform(Q, FVector(C.X, C.Y, Zc + 470.f), FVector((2.f * GateHalf + 90.f) / 100.f, 0.4f, 0.4f))); // lintel over the gate
		}

		// ----- Walkway along the inside of the wall (you walk under it through the gates) -----
		{
			const float DeckTop = JeebTerrain::HeightAt(VC.X, VC.Y) + JeebTerrain::DeckHeight();
			const float DeckLen = 2.f * Half - 270.f; // stops at the corner towers
			// One run of deck, or two with a break over each gate (nothing spans the doorway)
			const float DeckEnd = DeckLen * 0.5f, GateBreak = 300.f;
			TArray<TPair<float, float>> Runs;
			if (bGate) { Runs.Add(TPair<float, float>(-DeckEnd, -GateBreak)); Runs.Add(TPair<float, float>(GateBreak, DeckEnd)); }
			else Runs.Add(TPair<float, float>(-DeckEnd, DeckEnd));
			for (const TPair<float, float>& Run : Runs)
			{
				const float RunLen = Run.Value - Run.Key;
				const FVector2D DC = C + Dir * ((Run.Key + Run.Value) * 0.5f) + In * 115.f;
				Put(Plank, FTransform(Q, FVector(DC.X, DC.Y, DeckTop - 10.f), FVector(RunLen / 100.f, 1.7f, 0.2f)));
				AddSolid(FVector(DC.X, DC.Y, DeckTop - 10.f), FVector(RunLen * 0.5f, 85.f, 10.f), Q);
			}
			for (float U = -Half + 250.f; U <= Half - 250.f; U += 400.f)
			{
				if (bGate && FMath::Abs(U) < 300.f) continue; // keep the gate road clear
				const FVector2D PP = C + Dir * U + In * 190.f;
				const float Zg = Ground(PP.X, PP.Y);
				Put(Log, FTransform(FQuat::Identity, FVector(PP.X, PP.Y, Zg + (DeckTop - Zg) * 0.5f - 10.f), FVector(0.3f, 0.3f, (DeckTop - Zg - 20.f) / 100.f)));
			}
		}
	};
	Side(VC + FVector2D( Half, 0.f), FVector2D(0, 1), FVector2D(-1, 0), true);   // east wall, gate toward the start
	Side(VC + FVector2D(0.f,  Half), FVector2D(1, 0), FVector2D(0, -1), true);   // north wall, gate toward the graveyard
	Side(VC + FVector2D(-Half, 0.f), FVector2D(0, 1), FVector2D(1, 0),  false);  // west wall
	Side(VC + FVector2D(0.f, -Half), FVector2D(1, 0), FVector2D(0, 1),  false);  // south wall

	// ---------------- Ladders up to the walkway ----------------
	for (const JeebTerrain::FLadder& L : JeebTerrain::Ladders())
	{
		const FQuat LQ = FRotator(0.f, FVector(-L.Out.Y, L.Out.X, 0.f).Rotation().Yaw, 0.f).Quaternion(); // local X runs sideways along the wall
		const FVector2D Side2(-L.Out.Y, L.Out.X);
		const float Zg = Ground(L.Pos.X, L.Pos.Y);
		const float Top = JeebTerrain::HeightAt(VC.X, VC.Y) + JeebTerrain::DeckHeight() + 40.f;
		const float Hgt = Top - Zg;
		for (int32 sd = -1; sd <= 1; sd += 2)
		{
			const FVector2D RP = L.Pos + Side2 * (sd * 28.f);
			Put(Beam, FTransform(LQ, FVector(RP.X, RP.Y, Zg + Hgt * 0.5f), FVector(0.07f, 0.07f, Hgt / 100.f)));
		}
		for (float Z = 30.f; Z < Hgt - 10.f; Z += 38.f)
		{
			Put(Beam, FTransform(LQ, FVector(L.Pos.X, L.Pos.Y, Zg + Z), FVector(0.56f, 0.05f, 0.05f)));
		}
	}

	// ---------------- Corner towers ----------------
	for (int32 sx = -1; sx <= 1; sx += 2)
	{
		for (int32 sy = -1; sy <= 1; sy += 2)
		{
			const FVector2D P = VC + FVector2D(sx * Half, sy * Half);
			const float Z = Ground(P.X, P.Y);
			Put(Stone, FTransform(FQuat::Identity, FVector(P.X, P.Y, Z + 230.f), FVector(2.4f, 2.4f, 4.6f)));
			Put(Roof,  FTransform(FQuat::Identity, FVector(P.X, P.Y, Z + 460.f + 110.f), FVector(3.4f, 3.4f, 2.2f)));
			AddSolid(FVector(P.X, P.Y, Z + 230.f), FVector(120.f, 120.f, 230.f), FQuat::Identity);
		}
	}

	// ---------------- The fountain in the plaza ----------------
	{
		const float Z = Ground(VC.X, VC.Y);
		auto At = [&](float Dz) { return FVector(VC.X, VC.Y, Z + Dz); };
		Put(StoneRing, FTransform(FQuat::Identity, At(25.f), FVector(6.0f, 6.0f, 0.5f)));      // wide low basin wall
		Put(Water,     FTransform(FQuat::Identity, At(47.f), FVector(5.4f, 5.4f, 0.02f)));     // basin water
		Put(StoneRing, FTransform(FQuat::Identity, At(30.f), FVector(1.5f, 1.5f, 0.6f)));      // pedestal foot
		Put(StoneRing, FTransform(FQuat::Identity, At(95.f), FVector(0.7f, 0.7f, 1.3f)));      // pillar
		Put(StoneRing, FTransform(FQuat::Identity, At(165.f), FVector(2.3f, 2.3f, 0.28f)));    // upper bowl
		Put(Water,     FTransform(FQuat::Identity, At(181.f), FVector(1.95f, 1.95f, 0.02f)));  // upper water
		Put(StoneRing, FTransform(FQuat::Identity, At(215.f), FVector(0.3f, 0.3f, 0.7f)));     // spout column
		Put(Water,     FTransform(FQuat::Identity, At(265.f), FVector(0.12f, 0.12f, 1.3f)));   // rising jet
		Put(Roof,      FTransform(FQuat::Identity, At(245.f), FVector(0.5f, 0.5f, 0.35f)));   // cap
		// Falling streams from the upper bowl down into the basin
		for (int32 k = 0; k < 10; ++k)
		{
			const float Ang = k * 2.f * PI / 10.f;
			const FVector Dir(FMath::Cos(Ang), FMath::Sin(Ang), 0.f);
			const FVector A = At(172.f) + Dir * 112.f;
			const FVector B = At(50.f) + Dir * 190.f;
			const FVector D = B - A;
			const float Len = D.Size();
			const FQuat Q = FRotationMatrix::MakeFromZ(D / Len).ToQuat();
			Put(Water, FTransform(Q, (A + B) * 0.5f, FVector(0.07f, 0.07f, Len / 100.f)));
		}
		FountainBase = At(0.f);
		bFountain = true;
		for (int32 k = 0; k < NumJetDrops + NumFallDrops; ++k) Drops->AddInstance(FTransform(FQuat::Identity, FountainBase, FVector(0.001f)));
		AddSolid(At(30.f), FVector(285.f, 285.f, 32.f), FQuat::Identity);
		AddSolid(At(30.f), FVector(285.f, 285.f, 32.f), FRotator(0.f, 45.f, 0.f).Quaternion());
		AddSolid(At(150.f), FVector(80.f, 80.f, 150.f), FQuat::Identity);
	}

	// ---------------- The market in front of the gate: the store (Brindle) and the smithy (Hobb) ----------------
	{
		const float ZRoof = 330.f;
		const FQuat SlopeFront = FRotator(0.f, 0.f, 24.f).Quaternion();    // +Y side drops
		const FQuat SlopeBack = FRotator(0.f, 0.f, -24.f).Quaternion();    // -Y side drops

		// ---------- The store: open front toward +Y, a counter across it ----------
		{
			const FVector2D S = JeebTerrain::ShopPos();
			const float Z = Ground(S.X, S.Y);
			auto At = [&](float Dx, float Dy, float Dz) { return FVector(S.X + Dx, S.Y + Dy, Z + Dz); };
			Put(Plank, FTransform(FQuat::Identity, At(0.f, 0.f, 5.f), FVector(4.8f, 4.6f, 0.1f)));                  // floor boards
			for (float Dx : { -230.f, 230.f })
				for (float Dy : { -210.f, 210.f })
				{
					Put(Log, FTransform(FQuat::Identity, At(Dx, Dy, 125.f), FVector(0.22f, 0.22f, 2.5f)));
					AddSolid(At(Dx, Dy, 125.f), FVector(12.f, 12.f, 125.f), FQuat::Identity);
				}
			Put(Beam, FTransform(FQuat::Identity, At(0.f, -215.f, 140.f), FVector(4.7f, 0.14f, 2.8f)));            // back wall
			AddSolid(At(0.f, -215.f, 140.f), FVector(235.f, 8.f, 140.f), FQuat::Identity);
			for (float Sx : { -232.f, 232.f })
			{
				Put(Beam, FTransform(FQuat::Identity, At(Sx, -30.f, 95.f), FVector(0.14f, 3.6f, 1.9f)));            // low side walls
				AddSolid(At(Sx, -30.f, 95.f), FVector(8.f, 180.f, 95.f), FQuat::Identity);
			}
			// Roof: two sloping boards of red cloth with a ridge beam
			Put(Awning, FTransform(SlopeFront, At(0.f, 118.f, ZRoof - 52.f), FVector(5.1f, 2.65f, 0.1f)));
			Put(Plank,  FTransform(SlopeBack,  At(0.f, -118.f, ZRoof - 52.f), FVector(5.1f, 2.65f, 0.1f)));
			Put(Log,    FTransform(FRotator(90.f, 0.f, 0.f).Quaternion(), At(0.f, 0.f, ZRoof), FVector(0.2f, 0.2f, 5.2f)));
			// Counter across the front, with a top board
			Put(Beam, FTransform(FQuat::Identity, At(0.f, 125.f, 48.f), FVector(4.0f, 0.55f, 0.96f)));
			Put(Beam, FTransform(FQuat::Identity, At(0.f, 125.f, 99.f), FVector(4.2f, 0.8f, 0.08f)));
			AddSolid(At(0.f, 125.f, 52.f), FVector(210.f, 30.f, 52.f), FQuat::Identity);
			// Sign above the counter: a board with a golden ingot emblem
			Put(Plank, FTransform(FQuat::Identity, At(0.f, 215.f, 215.f), FVector(2.6f, 0.08f, 0.55f)));
			Put(Gold,  FTransform(FQuat::Identity, At(0.f, 221.f, 215.f), FVector(1.5f, 0.05f, 0.22f)));
			Put(Gold,  FTransform(FQuat::Identity, At(0.f, 221.f, 238.f), FVector(1.0f, 0.05f, 0.1f)));
			// Shelves on the back wall with goods
			for (float Sz : { 70.f, 135.f, 200.f })
				Put(Beam, FTransform(FQuat::Identity, At(0.f, -195.f, Sz), FVector(4.0f, 0.4f, 0.06f)));
			for (int32 k = 0; k < 7; ++k)
			{
				const float Dx = -165.f + k * 55.f;
				Put((k % 3 == 0) ? Gold : ((k % 3 == 1) ? Awning : Stone), FTransform(FQuat::Identity, At(Dx, -190.f, 90.f), FVector(0.3f, 0.26f, 0.36f)));
				Put((k % 2 == 0) ? Stone : Gold, FTransform(FQuat::Identity, At(Dx + 20.f, -190.f, 155.f), FVector(0.3f, 0.26f, 0.36f)));
				Put((k % 2 == 0) ? Awning : Plank, FTransform(FQuat::Identity, At(Dx, -190.f, 222.f), FVector(0.3f, 0.26f, 0.36f)));
			}
			// Ingots stacked on the counter, crates and barrels around the stall
			for (int32 k = 0; k < 6; ++k)
			{
				const int32 Row = k < 3 ? 0 : 1;
				const float Dx = (k < 3 ? (k - 1) * 34.f : (k - 4) * 34.f + 17.f);
				Put(Gold, FTransform(FQuat::Identity, At(Dx + 130.f, 125.f, 106.f + Row * 12.f), FVector(0.3f, 0.3f, 0.12f)));
			}
			for (int32 k = 0; k < 3; ++k)
			{
				Put(Beam, FTransform(FQuat::Identity, At(-290.f, 60.f + k * 62.f, 30.f), FVector(0.55f, 0.55f, 0.6f)));
				Put(Log, FTransform(FQuat::Identity, At(290.f, 50.f + k * 70.f, 40.f), FVector(0.55f, 0.55f, 0.8f)));
			}
			AddSolid(At(-290.f, 120.f, 40.f), FVector(30.f, 100.f, 40.f), FQuat::Identity);
			AddSolid(At(290.f, 120.f, 40.f), FVector(30.f, 100.f, 40.f), FQuat::Identity);
		}

		// ---------- The bank: a stone counter-house with a vault door, east of the smithy ----------
		{
			const FVector2D S = JeebTerrain::BankPos();
			const float Z = Ground(S.X, S.Y);
			auto At = [&](float Dx, float Dy, float Dz) { return FVector(S.X + Dx, S.Y + Dy, Z + Dz); };
			Put(Stone, FTransform(FQuat::Identity, At(0.f, 0.f, 4.f), FVector(4.8f, 4.6f, 0.08f)));                  // flagstone floor
			for (float Dx : { -230.f, 230.f })
				for (float Dy : { -210.f, 210.f })
				{
					Put(Stone, FTransform(FQuat::Identity, At(Dx, Dy, 125.f), FVector(0.34f, 0.34f, 2.5f)));
					AddSolid(At(Dx, Dy, 125.f), FVector(17.f, 17.f, 125.f), FQuat::Identity);
				}
			Put(Stone, FTransform(FQuat::Identity, At(0.f, -215.f, 140.f), FVector(4.7f, 0.25f, 2.8f)));              // stone back wall
			AddSolid(At(0.f, -215.f, 140.f), FVector(235.f, 12.f, 140.f), FQuat::Identity);
			for (float Sx : { -232.f, 232.f })
			{
				Put(Stone, FTransform(FQuat::Identity, At(Sx, -30.f, 140.f), FVector(0.25f, 3.6f, 2.8f)));          // full side walls
				AddSolid(At(Sx, -30.f, 140.f), FVector(12.f, 180.f, 140.f), FQuat::Identity);
			}
			Put(Plank, FTransform(SlopeFront, At(0.f, 118.f, ZRoof - 52.f), FVector(5.1f, 2.65f, 0.1f)));
			Put(Plank, FTransform(SlopeBack,  At(0.f, -118.f, ZRoof - 52.f), FVector(5.1f, 2.65f, 0.1f)));
			Put(Iron,  FTransform(FRotator(90.f, 0.f, 0.f).Quaternion(), At(0.f, 0.f, ZRoof), FVector(0.2f, 0.2f, 5.2f)));
			// The vault door on the back wall: a big iron wheel with a gold hub
			Put(Iron, FTransform(FRotator(0.f, 0.f, 90.f).Quaternion(), At(0.f, -198.f, 130.f), FVector(1.9f, 1.9f, 0.18f)));
			Put(Gold, FTransform(FRotator(0.f, 0.f, 90.f).Quaternion(), At(0.f, -184.f, 130.f), FVector(0.5f, 0.5f, 0.1f)));
			for (int32 k = 0; k < 4; ++k)
			{
				const FQuat Q = FRotator(0.f, 0.f, 45.f * k).Quaternion();
				Put(Gold, FTransform(Q, At(0.f, -182.f, 130.f), FVector(1.5f, 0.06f, 0.06f)));
			}
			// Counter across the front with an iron grille above it (a gap in the middle to talk through)
			Put(Stone, FTransform(FQuat::Identity, At(0.f, 125.f, 48.f), FVector(4.0f, 0.55f, 0.96f)));
			Put(Beam,  FTransform(FQuat::Identity, At(0.f, 125.f, 99.f), FVector(4.2f, 0.8f, 0.08f)));
			AddSolid(At(0.f, 125.f, 52.f), FVector(210.f, 30.f, 52.f), FQuat::Identity);
			for (int32 k = 0; k < 11; ++k)
			{
				const float Dx = -190.f + k * 38.f;
				if (FMath::Abs(Dx) < 60.f) continue;
				Put(Iron, FTransform(FQuat::Identity, At(Dx, 130.f, 160.f), FVector(0.05f, 0.05f, 1.2f)));
			}
			Put(Iron, FTransform(FQuat::Identity, At(0.f, 130.f, 224.f), FVector(4.0f, 0.06f, 0.06f)));
			// Sign: a stone board with a gold ingot emblem
			Put(Stone, FTransform(FQuat::Identity, At(0.f, 215.f, 215.f), FVector(2.6f, 0.08f, 0.55f)));
			Put(Gold,  FTransform(FQuat::Identity, At(-30.f, 221.f, 215.f), FVector(0.7f, 0.05f, 0.2f)));
			Put(Gold,  FTransform(FQuat::Identity, At(30.f, 221.f, 215.f), FVector(0.7f, 0.05f, 0.2f)));
			Put(Gold,  FTransform(FQuat::Identity, At(0.f, 221.f, 235.f), FVector(0.7f, 0.05f, 0.2f)));
			// Ingot stacks and sacks around the floor
			for (int32 k = 0; k < 8; ++k)
				Put(Gold, FTransform(FQuat::Identity, At(-150.f + (k % 4) * 34.f, -120.f - (k / 4) * 30.f, 15.f + 0.f), FVector(0.3f, 0.3f, 0.12f)));
			for (int32 k = 0; k < 3; ++k)
				Put(Plank, FTransform(FQuat::Identity, At(150.f, -140.f + k * 20.f, 25.f + k * 18.f), FVector(0.55f, 0.55f, 0.4f)));
			AddSolid(At(150.f, -120.f, 40.f), FVector(32.f, 50.f, 40.f), FQuat::Identity);
		}

		// ---------- The smithy: forge, chimney, anvil, weapon rack ----------
		{
			const FVector2D S = JeebTerrain::SmithyPos();
			const float Z = Ground(S.X, S.Y);
			auto At = [&](float Dx, float Dy, float Dz) { return FVector(S.X + Dx, S.Y + Dy, Z + Dz); };
			Put(Stone, FTransform(FQuat::Identity, At(0.f, 0.f, 4.f), FVector(4.8f, 4.6f, 0.08f)));                  // flagstone floor
			for (float Dx : { -230.f, 230.f })
				for (float Dy : { -210.f, 210.f })
				{
					Put(Log, FTransform(FQuat::Identity, At(Dx, Dy, 125.f), FVector(0.24f, 0.24f, 2.5f)));
					AddSolid(At(Dx, Dy, 125.f), FVector(12.f, 12.f, 125.f), FQuat::Identity);
				}
			Put(Stone, FTransform(FQuat::Identity, At(0.f, -215.f, 140.f), FVector(4.7f, 0.2f, 2.8f)));              // stone back wall
			AddSolid(At(0.f, -215.f, 140.f), FVector(235.f, 10.f, 140.f), FQuat::Identity);
			for (float Sx : { -232.f, 232.f })
			{
				Put(Stone, FTransform(FQuat::Identity, At(Sx, -90.f, 90.f), FVector(0.2f, 2.4f, 1.8f)));             // low side walls
				AddSolid(At(Sx, -90.f, 90.f), FVector(10.f, 120.f, 90.f), FQuat::Identity);
			}
			Put(Plank, FTransform(SlopeFront, At(0.f, 118.f, ZRoof - 52.f), FVector(5.1f, 2.65f, 0.1f)));
			Put(Plank, FTransform(SlopeBack,  At(0.f, -118.f, ZRoof - 52.f), FVector(5.1f, 2.65f, 0.1f)));
			Put(Log,   FTransform(FRotator(90.f, 0.f, 0.f).Quaternion(), At(0.f, 0.f, ZRoof), FVector(0.2f, 0.2f, 5.2f)));
			// Forge with a glowing mouth and a tall chimney through the roof
			Put(Stone, FTransform(FQuat::Identity, At(-130.f, -150.f, 60.f), FVector(1.8f, 1.3f, 1.2f)));
			Put(Ember, FTransform(FQuat::Identity, At(-130.f, -83.f, 52.f), FVector(1.1f, 0.06f, 0.4f)));
			Put(Ember, FTransform(FQuat::Identity, At(-130.f, -150.f, 122.f), FVector(1.2f, 0.9f, 0.04f)));
			Put(Stone, FTransform(FQuat::Identity, At(-130.f, -165.f, 270.f), FVector(0.8f, 0.8f, 3.4f)));
			AddSolid(At(-130.f, -150.f, 60.f), FVector(90.f, 65.f, 60.f), FQuat::Identity);
			AddSolid(At(-130.f, -165.f, 270.f), FVector(40.f, 40.f, 170.f), FQuat::Identity);
			// Anvil on a log stump (the smith hammers it)
			Put(Log,  FTransform(FQuat::Identity, At(40.f, 65.f, 22.f), FVector(0.7f, 0.7f, 0.44f)));
			Put(Iron, FTransform(FQuat::Identity, At(40.f, 65.f, 56.f), FVector(0.9f, 0.34f, 0.2f)));
			Put(Iron, FTransform(FQuat::Identity, At(40.f, 65.f, 42.f), FVector(0.4f, 0.28f, 0.18f)));
			Put(Iron, FTransform(FQuat::Identity, At(78.f, 65.f, 58.f), FVector(0.3f, 0.14f, 0.1f)));               // horn
			AddSolid(At(40.f, 65.f, 36.f), FVector(48.f, 24.f, 38.f), FQuat::Identity);
			// Weapon rack on the back wall and a quench barrel
			Put(Beam, FTransform(FQuat::Identity, At(90.f, -200.f, 120.f), FVector(1.8f, 0.08f, 0.1f)));
			Put(Beam, FTransform(FQuat::Identity, At(90.f, -200.f, 200.f), FVector(1.8f, 0.08f, 0.1f)));
			for (int32 k = 0; k < 5; ++k)
			{
				Put(Iron, FTransform(FQuat::Identity, At(30.f + k * 30.f, -198.f, 160.f), FVector(0.06f, 0.03f, 1.0f)));
				Put(Gold, FTransform(FQuat::Identity, At(30.f + k * 30.f, -198.f, 205.f), FVector(0.18f, 0.04f, 0.04f)));
			}
			Put(Log,   FTransform(FQuat::Identity, At(170.f, -150.f, 45.f), FVector(0.7f, 0.7f, 0.9f)));
			Put(Water, FTransform(FQuat::Identity, At(170.f, -150.f, 88.f), FVector(0.58f, 0.58f, 0.02f)));
			AddSolid(At(170.f, -150.f, 45.f), FVector(35.f, 35.f, 45.f), FQuat::Identity);
			// Coal pile and a sign (an anvil emblem)
			for (int32 k = 0; k < 6; ++k)
				Put(Iron, FTransform(FRotator(0.f, 20.f * k, 0.f).Quaternion(), At(-30.f + 18.f * (k % 3), -165.f + 14.f * (k / 3), 14.f + 10.f * (k / 3)), FVector(0.22f, 0.22f, 0.2f)));
			Put(Plank, FTransform(FQuat::Identity, At(0.f, 215.f, 215.f), FVector(2.2f, 0.08f, 0.55f)));
			Put(Iron,  FTransform(FQuat::Identity, At(0.f, 221.f, 218.f), FVector(1.0f, 0.05f, 0.14f)));
			Put(Iron,  FTransform(FQuat::Identity, At(0.f, 221.f, 236.f), FVector(0.5f, 0.05f, 0.1f)));
		}
	}

	// ---------------- Benches around the fountain (press E next to one to sit) ----------------
	for (const JeebTerrain::FSeat& Seat : JeebTerrain::Seats())
	{
		const float Z = Ground(Seat.Pos.X, Seat.Pos.Y);
		const FQuat Q = FRotator(0.f, Seat.Yaw, 0.f).Quaternion(); // local +X = the way a sitter faces
		auto L = [&](FVector Off, FVector Size) { return FTransform(Q, FVector(Seat.Pos.X, Seat.Pos.Y, Z) + Q.RotateVector(Off), Size / 100.f); };
		Put(Beam, L(FVector(0, 0, 28), FVector(44, 170, 8)));       // seat board
		Put(Beam, L(FVector(-18, 0, 52), FVector(8, 170, 36)));     // backrest
		Put(Beam, L(FVector(0, 70, 12), FVector(36, 12, 24)));      // legs
		Put(Beam, L(FVector(0, -70, 12), FVector(36, 12, 24)));
		AddSolid(FVector(Seat.Pos.X, Seat.Pos.Y, Z + 16.f), FVector(22.f, 85.f, 16.f), Q);
	}

	for (TPair<UInstancedStaticMeshComponent*, TArray<FTransform>>& Pair : Batch)
	{
		Pair.Key->AddInstances(Pair.Value, false, false, false);
	}

	// ---------------- Huts ----------------
	UWorld* World = GetWorld();
	auto Hut = [&](float X, float Y, float YawDeg, int32 LockLevel = 0, int32 Loot = 0)
	{
		const float S = R.FRandRange(0.92f, 1.06f);
		if (ACottage* H = World->SpawnActor<ACottage>(FVector(X, Y, Ground(X, Y)), FRotator(0.f, YawDeg, 0.f)))
		{
			H->SetActorScale3D(FVector(S));
			if (LockLevel > 0) H->Configure(LockLevel, Loot);
		}
	};

	// Inside the walls, doors facing the plaza (at least 5 m apart)
	for (const JeebTerrain::FHutDef& HD : JeebTerrain::VillageHuts())
	{
		Hut(HD.Pos.X, HD.Pos.Y, HD.Yaw);
	}

	// Outside, each one facing the nearest road
	// Four of the eight are locked, from easy to hard, each with a stash of ingots inside
	{
		static const int32 LockLv[8]   = { 0, 1, 0, 5, 0, 0, 10, 15 };
		static const int32 LootAmt[8]  = { 0, 60, 0, 150, 0, 0, 350, 600 };
		int32 Idx = 0;
		for (const FVector2D& Site : JeebTerrain::HutSites())
		{
			const FVector2D Near = JeebTerrain::NearestPathPoint(Site.X, Site.Y);
			const float Yaw = FVector(Near.X - Site.X, Near.Y - Site.Y, 0.f).Rotation().Yaw;
			Hut(Site.X, Site.Y, Yaw, LockLv[Idx % 8], LootAmt[Idx % 8]);
			++Idx;
		}
	}

	// ---------------- The shopkeeper and the blacksmith ----------------
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const FVector2D SP = JeebTerrain::ShopNpcPos();
		if (ANpc* N = World->SpawnActor<ANpc>(FVector(SP.X, SP.Y, Ground(SP.X, SP.Y) + 92.f), FRotator(0.f, 90.f, 0.f), Params))
		{
			N->Setup(ENpcRole::Shopkeeper);
		}
		const FVector2D MP = JeebTerrain::SmithNpcPos();
		if (ANpc* N = World->SpawnActor<ANpc>(FVector(MP.X, MP.Y, Ground(MP.X, MP.Y) + 92.f), FRotator(0.f, 90.f, 0.f), Params))
		{
			N->Setup(ENpcRole::Blacksmith);
		}
		const FVector2D BP = JeebTerrain::BankNpcPos();
		if (ANpc* N = World->SpawnActor<ANpc>(FVector(BP.X, BP.Y, Ground(BP.X, BP.Y) + 92.f), FRotator(0.f, 90.f, 0.f), Params))
		{
			N->Setup(ENpcRole::Banker);
		}
		// Sir Aldous patrols the grass outside the east gate
		const FVector2D GK(-780.f, -110.f);
		if (ANpc* N = World->SpawnActor<ANpc>(FVector(GK.X, GK.Y, Ground(GK.X, GK.Y) + 92.f), FRotator(0.f, -90.f, 0.f), Params))
		{
			N->Setup(ENpcRole::GateKnight);
			TArray<FVector2D> Beat;
			Beat.Add(FVector2D(-780.f, -110.f));
			Beat.Add(FVector2D(-780.f, -480.f));
			N->SetPatrol(Beat);
		}

		// Ten villagers, spread around the realm: three stroll the plaza and huts, the rest keep to their own corners
		// of the map (the graveyard, the pond, a hut on the hills, the road, the castle forecourt, outside the pig pen)
		const FVector2D VCp = JeebTerrain::VillageCenter();
		const FVector2D HutFronts[5] = { FVector2D(-900.f, 880.f), FVector2D(900.f, 880.f), FVector2D(-900.f, -880.f), FVector2D(900.f, -880.f), FVector2D(-780.f, 0.f) };
		auto RingAround = [](const FVector2D& Centre, float Radius, float StartDeg, float Dir)
		{
			TArray<FVector2D> Pts;
			for (int32 k = 0; k < 6; ++k)
			{
				const float A = FMath::DegreesToRadians(StartDeg + Dir * k * 60.f);
				Pts.Add(Centre + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Radius);
			}
			return Pts;
		};
		auto RoadWalk = [](float Frac)
		{
			const TArray<FVector2D>& Road = JeebTerrain::PathDefs()[0].Pts;   // the ring road
			const int32 N = Road.Num();
			const int32 I0 = FMath::Clamp(int32(Frac * N), 0, N - 9);
			TArray<FVector2D> Pts;
			Pts.Add(Road[I0]); Pts.Add(Road[I0 + 5]); Pts.Add(Road[I0 + 9]); Pts.Add(Road[I0 + 5]);
			return Pts;
		};
		const FVector2D Pen = JeebTerrain::PenCenter();
		const FVector2D Cas = JeebTerrain::CastlePos();
		for (int32 i = 0; i < 10; ++i)
		{
			TArray<FVector2D> Beat;
			switch (i)
			{
			case 3: Beat = RingAround(JeebTerrain::HutSites()[0], 620.f, 40.f, 1.f); break;                               // Garrick, near the copper hills
			case 4: Beat = RingAround(JeebTerrain::HutSites()[6], 620.f, 200.f, -1.f); break;                              // Wren, at a hut in the north
			case 5: Beat = RingAround(FVector2D(JeebTerrain::GraveyardPos().X, JeebTerrain::GraveyardPos().Y), 520.f, 0.f, 1.f); break;   // Fenwick, among the graves
			case 6: Beat = RingAround(JeebTerrain::PondCenter(), 830.f, 100.f, -1.f); break;                               // Sable, round the pond
			case 9: Beat = { Pen + FVector2D(-500.f, -900.f), Pen + FVector2D(0.f, -960.f), Pen + FVector2D(500.f, -900.f), Pen + FVector2D(0.f, -960.f) }; break;   // Hettie, outside the pen
			case 1: Beat = RoadWalk(0.30f); break;                                                                         // Tobin, on the road
			case 8: Beat = { Cas + FVector2D(-1100.f, -300.f), Cas + FVector2D(-950.f, 0.f), Cas + FVector2D(-1100.f, 300.f), Cas + FVector2D(-950.f, 0.f) }; break;   // Peregrine, at the castle
			default:
			{
				const float A0 = FMath::DegreesToRadians(i * 36.f);
				const float Dir = (i % 2 == 0) ? 1.f : -1.f;
				for (int32 k = 0; k < 6; ++k)
				{
					const float A = A0 + Dir * FMath::DegreesToRadians(k * 55.f);
					Beat.Add(VCp + FVector2D(FMath::Cos(A), FMath::Sin(A)) * 650.f);
				}
				if (i % 3 != 2) Beat.Insert(VCp + HutFronts[i % 5], 3);   // an errand to a hut door
				break;
			}
			}
			const FVector2D S = Beat[0];
			if (ANpc* N = World->SpawnActor<ANpc>(FVector(S.X, S.Y, Ground(S.X, S.Y) + 92.f), FRotator(0.f, i * 36.f + 90.f, 0.f), Params))
			{
				N->Setup(ENpcRole::Villager, i);
				N->SetPatrol(Beat);
			}
		}
	}
}

// Fountain droplets: some leap from the spout and arc into the upper bowl, the rest pour over the rim into the basin
void AVillage::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bFountain || !Drops) return;
	const float T = GetWorld()->GetTimeSeconds();
	TArray<FTransform> Out;
	Out.Reserve(NumJetDrops + NumFallDrops);
	for (int32 k = 0; k < NumJetDrops; ++k)
	{
		const float Life = 1.1f;
		const float Ph = FMath::Frac(T / Life + k / (float)NumJetDrops);
		const float Tm = Ph * Life;
		const float Ang = k * 2.39996f;
		const float Vr = 55.f + 45.f * FMath::Frac(k * 0.618f);
		const float R = Vr * Tm;
		const float Z = 275.f + 420.f * Tm - 450.f * Tm * Tm;
		Out.Add(FTransform(FQuat::Identity, FountainBase + FVector(FMath::Cos(Ang) * R, FMath::Sin(Ang) * R, Z), FVector(0.045f, 0.045f, 0.09f)));
	}
	for (int32 k = 0; k < NumFallDrops; ++k)
	{
		const float Ph = FMath::Frac(T * 0.9f + k * 0.37f);
		const float Ang = (k % 10) * 2.f * PI / 10.f + 0.12f * (k / 10);
		const float R = 112.f + 78.f * Ph;
		const float Z = 172.f - 122.f * Ph * Ph;
		Out.Add(FTransform(FQuat::Identity, FountainBase + FVector(FMath::Cos(Ang) * R, FMath::Sin(Ang) * R, Z), FVector(0.05f, 0.05f, 0.12f + 0.1f * Ph)));
	}
	Drops->BatchUpdateInstancesTransforms(0, Out, false, true, true);
}

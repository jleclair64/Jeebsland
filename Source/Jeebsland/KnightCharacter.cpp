#include "KnightCharacter.h"
#include "Messages.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "InputModifiers.h"
#include "EnhancedInputComponent.h"
#include "SkillComponent.h"
#include "ResourceNode.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Terrain.h"
#include "Pig.h"
#include "Inventory.h"
#include "Npc.h"
#include "Horse.h"
#include "Cottage.h"
#include "Loot.h"
#include "Pond.h"
#include "HealthComponent.h"
#include "Projectile.h"
#include "Combat.h"
#include "WeaponDesign.h"
#include "ProceduralMeshComponent.h"
#include "Misc/ConfigCacheIni.h"

AKnightCharacter::AKnightCharacter()
{
	// Hide the (empty) skeletal mesh slot; the knight is made of shapes instead
	GetMesh()->SetVisibility(false);

	PrimaryActorTick.bCanEverTick = true;

	Skills = CreateDefaultSubobject<USkillComponent>(TEXT("Skills"));

	// Movement feel
	GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;
	GetCharacterMovement()->RotationRate = FRotator(0.f, 480.f, 0.f);
	GetCharacterMovement()->JumpZVelocity *= 0.894f; // about 20% lower jump (height grows with the square of launch speed)

	// Camera: a bit further back and higher, with gentle lag for a smooth follow
	if (USpringArmComponent* Boom = GetCameraBoom())
	{
		Boom->TargetArmLength = 540.f;   // 20% further back than before
		Boom->SocketOffset = FVector(0.f, 0.f, 60.f);
		Boom->bEnableCameraLag = true;
		Boom->CameraLagSpeed = 8.f;
	}

	// Dust puffs live in world space, so they stay where they were kicked up
	{
		ConstructorHelpers::FObjectFinder<UStaticMesh> DustMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		DustISM = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Dust"));
		DustISM->SetupAttachment(RootComponent);
		DustISM->SetStaticMesh(DustMesh.Object);
		DustISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		DustISM->SetCastShadow(false);
		DustISM->SetUsingAbsoluteLocation(true);
		DustISM->SetUsingAbsoluteRotation(true);
		DustISM->SetUsingAbsoluteScale(true);
	}

	// Every body part hangs off a pivot so it can be animated. Positions below are given relative to the
	// capsule center (feet at Z = -96, forward = +X, right = +Y); the helpers convert them to pivot space.
	TMap<USceneComponent*, FVector> PivotPos;

	auto MakePivot = [&](const TCHAR* Name, USceneComponent* Parent, FVector CapsulePos) -> USceneComponent*
	{
		USceneComponent* P = CreateDefaultSubobject<USceneComponent>(Name);
		P->SetupAttachment(Parent ? Parent : RootComponent.Get());
		P->SetRelativeLocation(CapsulePos - (Parent ? PivotPos.FindChecked(Parent) : FVector::ZeroVector));
		PivotPos.Add(P, CapsulePos);
		return P;
	};

	// ---- Pivots (the skeleton) ----
	BodyRoot   = MakePivot(TEXT("BodyRoot"),   nullptr,   FVector(0, 0, 0));       // bounces up and down
	HipL       = MakePivot(TEXT("HipL"),       BodyRoot,  KnightDesign::PivotPos(KnightDesign::P_HipL));
	HipR       = MakePivot(TEXT("HipR"),       BodyRoot,  KnightDesign::PivotPos(KnightDesign::P_HipR));
	KneeL      = MakePivot(TEXT("KneeL"),      HipL,      KnightDesign::PivotPos(KnightDesign::P_KneeL));
	KneeR      = MakePivot(TEXT("KneeR"),      HipR,      KnightDesign::PivotPos(KnightDesign::P_KneeR));
	AnkleL     = MakePivot(TEXT("AnkleL"),     KneeL,     KnightDesign::PivotPos(KnightDesign::P_AnkleL));
	AnkleR     = MakePivot(TEXT("AnkleR"),     KneeR,     KnightDesign::PivotPos(KnightDesign::P_AnkleR));
	UpperPivot = MakePivot(TEXT("UpperPivot"), BodyRoot,  FVector(0, 0, -41.f));     // chest: leans and twists
	HeadPivot  = MakePivot(TEXT("HeadPivot"),  UpperPivot, FVector(0, 0, 23.f));
	PlumePivot = MakePivot(TEXT("PlumePivot"), HeadPivot, FVector(-2, 0, 71.f));
	// (the helm is modelled at its full size: no head scaling)
	CapeUpper  = MakePivot(TEXT("CapeUpper"),  UpperPivot, FVector(-26, 0, 13.f));
	CapeLower  = MakePivot(TEXT("CapeLower"),  CapeUpper, FVector(-26, 0, -19.f));
	ArmPivot   = MakePivot(TEXT("ArmPivotR"),  UpperPivot, KnightDesign::PivotPos(KnightDesign::P_ArmR));     // right shoulder (sword arm)
	ArmPivotL  = MakePivot(TEXT("ArmPivotL"),  UpperPivot, KnightDesign::PivotPos(KnightDesign::P_ArmL));    // left shoulder (shield arm)
	ElbowR     = MakePivot(TEXT("ElbowR"),     ArmPivot,  KnightDesign::PivotPos(KnightDesign::P_ElbowR));
	ElbowL     = MakePivot(TEXT("ElbowL"),     ArmPivotL, KnightDesign::PivotPos(KnightDesign::P_ElbowL));
}

// ---------------- The knight's body parts ----------------
void AKnightCharacter::BuildKnight()
{
	USceneComponent* Pivots[KnightDesign::P_Count] = {};
	Pivots[KnightDesign::P_Body] = BodyRoot;     Pivots[KnightDesign::P_Upper] = UpperPivot;  Pivots[KnightDesign::P_Head] = HeadPivot;
	Pivots[KnightDesign::P_Plume] = PlumePivot;  Pivots[KnightDesign::P_CapeU] = CapeUpper;   Pivots[KnightDesign::P_CapeL] = CapeLower;
	Pivots[KnightDesign::P_ArmL] = ArmPivotL;    Pivots[KnightDesign::P_ArmR] = ArmPivot;     Pivots[KnightDesign::P_ElbowL] = ElbowL;
	Pivots[KnightDesign::P_ElbowR] = ElbowR;     Pivots[KnightDesign::P_HipL] = HipL;         Pivots[KnightDesign::P_HipR] = HipR;
	Pivots[KnightDesign::P_KneeL] = KneeL;       Pivots[KnightDesign::P_KneeR] = KneeR;
	Pivots[KnightDesign::P_AnkleL] = AnkleL;     Pivots[KnightDesign::P_AnkleR] = AnkleR;

	TArray<KnightDesign::FDef> Defs;
	KnightDesign::Build(Defs);

	for (const KnightDesign::FDef& D : Defs)
	{
		if (D.Mesh.V.Num() == 0 || !Pivots[D.Pivot]) continue;
		UProceduralMeshComponent* C = MakeMeshComp(Pivots[D.Pivot], D.Pivot, D.Mesh);

		FArmorPart P;
		P.Comp = C;
		P.Slot = D.Slot; P.Role = D.Role; P.MinTier = D.MinTier; P.MaxTier = D.MaxTier;
		P.Fixed = D.Fixed; P.bFixedMetal = D.bFixedMetal; P.Shade = D.Shade; P.bBare = D.bBare;
		P.Style = D.Style; P.bFixedColor = D.bFixedColor;
		ArmorParts.Add(P);
	}
}

UProceduralMeshComponent* AKnightCharacter::MakeMeshComp(USceneComponent* Attach, int32 PivotId, const KM::FMD& Md)
{
	UProceduralMeshComponent* C = NewObject<UProceduralMeshComponent>(this);
	C->SetupAttachment(Attach);
	// The shape is modelled around the knight's centre; move it back so it sits right on its pivot
	C->SetRelativeLocation(-KnightDesign::PivotPos(PivotId));
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->RegisterComponent();

	TArray<FVector> Normals;
	KM::ComputeNormals(Md, Normals);
	TArray<int32> Tris;
	Tris.Reserve(Md.I.Num() * 2);
	for (int32 t = 0; t + 2 < Md.I.Num(); t += 3)
	{
		// Both windings, so the outside shows whichever way the engine treats as front
		Tris.Add(Md.I[t]); Tris.Add(Md.I[t + 1]); Tris.Add(Md.I[t + 2]);
		Tris.Add(Md.I[t]); Tris.Add(Md.I[t + 2]); Tris.Add(Md.I[t + 1]);
	}
	TArray<FVector2D> UV;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tangents;
	C->CreateMeshSection_LinearColor(0, Md.V, Tris, Normals, UV, Colors, Tangents, false);
	return C;
}

void AKnightCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Tick after the movement has moved us this frame: the cape (which lives in world space) is pinned to where the body
	// really is, not where it was a frame ago. Otherwise in a fast jump it lags one frame behind on some frames and not others.
	if (GetCharacterMovement()) AddTickPrerequisiteComponent(GetCharacterMovement());
	PrevYaw = GetActorRotation().Yaw;
	SpawnLoc = GetActorLocation();
	if (Skills) { MaxHealth = 100 + JeebItems::CombinedMods(Skills).MaxHealthBonus; Health = (float)MaxHealth; }
	if (Skills) Skills->FillPracticePack();   // TEMPORARY: a pack full of random gear for practising swaps (remove this line when done)
	if (Skills) Skills->AddInvItem(JeebItems::GearId(JeebItems::K_Crest, 0), 1);   // the Red Helm Brush, ready to wear

	// Saved choices (side panel). Graphics always start low: fancy lighting off, 60% resolution.
	if (GConfig)
	{
		
		// (resolution is not loaded either: the game always starts on low graphics, and you raise them in Settings)
		GConfig->GetBool(TEXT("Jeebsland"), TEXT("PanelAlways"), bPanelAlways, GGameUserSettingsIni);
	}
	bFancyLighting = false;   // fancy lighting starts off every time
	RenderScale = 0.6f;       // and a low render resolution
	ApplyGraphics();
	UpperBase = UpperPivot->GetRelativeLocation();
	LeftBase = ArmPivotL->GetRelativeLocation();

	Dust.SetNum(16);
	{
		TArray<FTransform> Hidden;
		for (int32 i = 0; i < Dust.Num(); ++i) Hidden.Add(FTransform(FQuat::Identity, FVector(0, 0, -9000.f), FVector(0.001f)));
		DustISM->AddInstances(Hidden, false, false, false);
		UMaterialInterface* DBase = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"));
		if (UMaterialInstanceDynamic* DM = DustISM->CreateDynamicMaterialInstance(0, DBase))
		{
			DM->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.50f, 0.40f, 0.26f));
		}
	}

	BuildKnight();

	// The cape is cloth: its points live in world space, so the mesh component sits at the world origin
	CapeCloth = NewObject<UProceduralMeshComponent>(this);
	CapeCloth->SetupAttachment(RootComponent);
	CapeCloth->SetUsingAbsoluteLocation(true);
	CapeCloth->SetUsingAbsoluteRotation(true);
	CapeCloth->SetUsingAbsoluteScale(true);
	CapeCloth->SetWorldLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
	CapeCloth->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CapeCloth->bCastContactShadow = false;           // the cloth's own small shadows flickered across it as it moved
	CapeCloth->bAffectDistanceFieldLighting = false;
	CapeCloth->RegisterComponent();
	{
		UMaterialInterface* ClothMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Cloth.M_Cloth"));
		if (!ClothMat) ClothMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"));
		CapeMid = UMaterialInstanceDynamic::Create(ClothMat, this);
	}

	RefreshEquipment();
}

// Where cape point i of the top row is pinned: across the back of the shoulders, curving round them a little
FVector AKnightCharacter::CapePin(int32 i) const
{
	const float Y = FMath::Lerp(-16.f, 16.f, float(i) / (CapeNX - 1));
	// across the tops of the shoulders (the upper body is modelled in body space, 41 cm above the chest pivot)
	return UpperPivot->GetComponentTransform().TransformPosition(FVector(-19.f + 0.028f * Y * Y, Y, 57.f));
}

// A little cloth simulation (Verlet points held together by distance links): pinned at the shoulders, pulled down by
// gravity, slowed by the air, pushed back and lifted by the wind of running, and kept off the knight's back.
void AKnightCharacter::TickCape(float Dt)
{
	if (!CapeCloth || !UpperPivot || !CapeCloth->IsVisible()) { bCapeInit = false; return; }
	const int32 N = CapeNX * CapeNY;
	const float Length = 68.f, WTop = 36.f, WBot = 56.f;   // from the shoulders to the upper thigh
	const float DV = Length / (CapeNY - 1);
	auto Idx = [](int32 i, int32 j) { return j * CapeNX + i; };
	const FTransform Up = UpperPivot->GetComponentTransform();
	const FVector Back = -Up.GetUnitAxis(EAxis::X);

	if (!bCapeInit || CapeP.Num() != N)
	{
		CapeP.SetNum(N); CapePrev.SetNum(N);
		for (int32 j = 0; j < CapeNY; ++j)
			for (int32 i = 0; i < CapeNX; ++i)
			{
				const FVector P = CapePin(i) - FVector(0.f, 0.f, j * DV) + Back * (j * 0.8f);
				CapeP[Idx(i, j)] = P; CapePrev[Idx(i, j)] = P;
			}
		bCapeInit = true;
		bCapeRootSet = false;
		CapeAccum = 0.f;
	}

	// Wind of moving: streams the cape back and lifts it when running, with a ripple running down it
	const FVector Vel = GetVelocity();
	const float Speed = FVector(Vel.X, Vel.Y, 0.f).Size();
	const FVector Fwd = GetActorForwardVector();
	const FVector Side = GetActorRightVector();
	CapeTime += Dt;

	constexpr float H = 1.f / 90.f;   // fixed small steps, so it behaves the same at any frame rate
	CapeAccum = FMath::Min(CapeAccum + Dt, 8.f * H);
	// The cloth is carried up and down with most of the body's own rise and fall (a jump, a landing), so it only has to
	// swing with what is left: no sudden stretching and snapping back when the knight leaps
	{
		const FVector Root = GetActorLocation();
		if (bCapeRootSet)
		{
			const FVector Carry(0.f, 0.f, (Root.Z - CapeRootPrev.Z) * 0.8f);   // up and down only: running still trails it behind
			for (int32 k = CapeNX; k < N; ++k) { CapeP[k] += Carry; CapePrev[k] += Carry; }
		}
		CapeRootPrev = Root; bCapeRootSet = true;
	}
	auto Link = [&](int32 a, int32 b, float Rest, float Stiff)
	{
		const FVector D = CapeP[b] - CapeP[a];
		const float L = D.Size();
		if (L < 1e-3f) return;
		const FVector Fix = D * ((L - Rest) / L) * 0.5f * Stiff;
		const bool bPinA = a < CapeNX, bPinB = b < CapeNX;
		if (bPinA && bPinB) return;
		if (bPinA) CapeP[b] -= Fix * 2.f;
		else if (bPinB) CapeP[a] += Fix * 2.f;
		else { CapeP[a] += Fix; CapeP[b] -= Fix; }
	};
	while (CapeAccum >= H)
	{
		CapeAccum -= H;
		for (int32 j = 1; j < CapeNY; ++j)
			for (int32 i = 0; i < CapeNX; ++i)
			{
				const int32 k = Idx(i, j);
				const float Down = float(j) / (CapeNY - 1);
				// A very heavy cloth (twice the old weight): slow, low ripples, and the wind of running barely lifts it (gravity wins)
				const FVector Ripple = (Side * FMath::Sin(CapeTime * 3.6f - j * 0.7f + i * 0.5f) * 0.6f + Back * FMath::Sin(CapeTime * 2.6f - j * 0.5f) * 0.4f)
				                     * (7.f + 0.17f * Speed) * Down;
				const FVector Force = FVector(0.f, 0.f, -2200.f) - Fwd * (0.4f * Speed) + FVector(0.f, 0.f, 0.06f * Speed) + Ripple;
				const FVector V = (CapeP[k] - CapePrev[k]) * 0.9f;   // more drag: it swings slowly and settles quickly
				CapePrev[k] = CapeP[k];
				CapeP[k] += V + Force * H * H;
			}
		// Keep its shape: links across, down, diagonally, and every other point down (stiffer, so it folds rather than crumples)
		for (int32 It = 0; It < 8; ++It)
		{
			for (int32 i = 0; i < CapeNX; ++i) CapeP[Idx(i, 0)] = CapePin(i);
			for (int32 j = 0; j < CapeNY; ++j)
			{
				const float W = FMath::Lerp(WTop, WBot, float(j) / (CapeNY - 1));
				const float DH = W / (CapeNX - 1);
				for (int32 i = 0; i < CapeNX; ++i)
				{
					if (i + 1 < CapeNX) Link(Idx(i, j), Idx(i + 1, j), DH, 1.f);
					if (j + 1 < CapeNY) Link(Idx(i, j), Idx(i, j + 1), DV, 1.f);
					if (i + 1 < CapeNX && j + 1 < CapeNY)
					{
						const float Diag = FMath::Sqrt(DH * DH + DV * DV);
						Link(Idx(i, j), Idx(i + 1, j + 1), Diag, 0.6f);
						Link(Idx(i + 1, j), Idx(i, j + 1), Diag, 0.6f);
					}
					if (j + 2 < CapeNY) Link(Idx(i, j), Idx(i, j + 2), 2.f * DV, 0.35f);
				}
			}
			// Never through the knight: stay behind his back and the backs of his legs
			for (int32 k = CapeNX; k < N; ++k)
			{
				FVector L = Up.InverseTransformPosition(CapeP[k]);
				float Lim = FMath::Lerp(-21.5f, -20.5f, FMath::Clamp((L.Z - 5.f) / 10.f, 0.f, 1.f));   // the back of the breastplate, easing into the mail skirt (no sudden step for points to jump across)
				if (bStowed && L.Z > -9.f) Lim = -34.f;   // and over the shield when it is on the back
				if (L.X > Lim) { L.X = Lim; CapeP[k] = Up.TransformPosition(L); }
			}
		}
	}

	// Draw it: positions, smooth normals facing away from the back, and two triangles per cell
	TArray<FVector> V; TArray<FVector> Nrm; TArray<FVector2D> UV;
	V.Reserve(N); Nrm.Reserve(N); UV.Reserve(N);
	for (int32 j = 0; j < CapeNY; ++j)
		for (int32 i = 0; i < CapeNX; ++i)
		{
			V.Add(CapeP[Idx(i, j)]);
			const FVector Dx = CapeP[Idx(FMath::Min(i + 1, CapeNX - 1), j)] - CapeP[Idx(FMath::Max(i - 1, 0), j)];
			const FVector Dy = CapeP[Idx(i, FMath::Min(j + 1, CapeNY - 1))] - CapeP[Idx(i, FMath::Max(j - 1, 0))];
			FVector Nm = FVector::CrossProduct(Dx, Dy).GetSafeNormal();
			if (FVector::DotProduct(Nm, Back) < 0.f) Nm = -Nm;
			// ease the shading normal from frame to frame: the folds still catch the light, but it no longer flickers as the
			// little ripples come and go
			{
				const int32 k = Idx(i, j);
				if (CapeNrmPrev.Num() != N) CapeNrmPrev.Init(Nm, N);
				Nm = FMath::Lerp(CapeNrmPrev[k], Nm, FMath::Clamp(Dt * 7.f, 0.f, 1.f)).GetSafeNormal();
				CapeNrmPrev[k] = Nm;
			}
			Nrm.Add(Nm);
			UV.Add(FVector2D(float(i) / (CapeNX - 1), float(j) / (CapeNY - 1)));
		}
	// a thick, heavy cloth: a second layer 1.6 cm inside, joined round the edges, so it never reads as a sheet of paper
	V.Reserve(2 * N); Nrm.Reserve(2 * N); UV.Reserve(2 * N);
	for (int32 k = 0; k < N; ++k) { const FVector P = V[k] - Nrm[k] * 3.0f, Nk = -Nrm[k]; const FVector2D Uk = UV[k]; V.Add(P); Nrm.Add(Nk); UV.Add(Uk); }
	if (!bCapeMeshMade)
	{
		TArray<int32> Tris;
		for (int32 j = 0; j + 1 < CapeNY; ++j)
			for (int32 i = 0; i + 1 < CapeNX; ++i)
			{
				const int32 A = Idx(i, j), B = Idx(i + 1, j), C = Idx(i, j + 1), D = Idx(i + 1, j + 1);
				Tris.Append({ A, B, D, A, D, C });
				Tris.Append({ N + A, N + D, N + B, N + A, N + C, N + D });
			}
		auto Rim = [&](int32 P, int32 Q) { Tris.Append({ P, Q, N + Q, P, N + Q, N + P }); };
		for (int32 i = 0; i + 1 < CapeNX; ++i) { Rim(Idx(i, 0), Idx(i + 1, 0)); Rim(Idx(i + 1, CapeNY - 1), Idx(i, CapeNY - 1)); }
		for (int32 j = 0; j + 1 < CapeNY; ++j) { Rim(Idx(0, j + 1), Idx(0, j)); Rim(Idx(CapeNX - 1, j), Idx(CapeNX - 1, j + 1)); }
		CapeCloth->CreateMeshSection_LinearColor(0, V, Tris, Nrm, UV, TArray<FLinearColor>(), TArray<FProcMeshTangent>(), false);
		if (CapeMid) CapeCloth->SetMaterial(0, CapeMid);
		bCapeMeshMade = true;
	}
	else
	{
		CapeCloth->UpdateMeshSection_LinearColor(0, V, Nrm, UV, TArray<FLinearColor>(), TArray<FProcMeshTangent>());
	}
}

// ---------------- What you wear and hold ----------------
namespace
{
	bool PartIsMetal(int32 Tier, EArmorRole Role)
	{
		return Tier >= 1 && (Role == EArmorRole::Plate || Role == EArmorRole::Trim || Role == EArmorRole::BootBody);
	}
	// The kind of gear that belongs in each knight slot
	int32 KindForSlot(int32 Slot)
	{
		switch (Slot)
		{
		case KnightDesign::SlotHelmet:    return JeebItems::K_Helmet;
		case KnightDesign::SlotBody:      return JeebItems::K_Chest;
		case KnightDesign::SlotShoulders: return JeebItems::K_Shoulders;
		case KnightDesign::SlotBoots:     return JeebItems::K_Boots;
		case KnightDesign::SlotCape:      return JeebItems::K_Cape;
		case KnightDesign::SlotGloves:    return JeebItems::K_Gloves;
		case KnightDesign::SlotBrush:     return JeebItems::K_Crest;
		case KnightDesign::SlotRingR: case KnightDesign::SlotRingL: return JeebItems::K_Ring;
		case KnightDesign::SlotAmulet:    return JeebItems::K_Amulet;
		default:                          return JeebItems::K_Shield;
		}
	}
}

void AKnightCharacter::ApplyPartMaterial(FArmorPart& P, const FLinearColor& Color, bool bMetal)
{
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"));
	// Metal parts use the shiny M_Metal if it exists (made once by MakeMetal.bat); otherwise plain colour
	UMaterialInterface* Metal = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Metal.M_Metal"));
	UMaterialInterface* Mat = (bMetal && Metal) ? Metal : Base;
	if (UMaterialInstanceDynamic* M = P.Comp->CreateDynamicMaterialInstance(0, Mat))
	{
		M->SetVectorParameterValue(TEXT("Color"), Color);
		if (bMetal && Metal)
		{
			M->SetScalarParameterValue(TEXT("Metallic"), 1.0f);
			M->SetScalarParameterValue(TEXT("Roughness"), 0.2f);   // polished plate
		}
	}
}

bool AKnightCharacter::ShieldActive() const
{
	if (!Skills || Skills->Worn[JeebItems::K_Shield] == 0) return false;
	const int32 K = SelectedKind();
	return K < 0 || JeebItems::AllowsShield(K);
}

int32 AKnightCharacter::SelectedKind() const
{
	if (!Skills) return -1;
	const int32 Id = Skills->SelectedItem();
	return JeebItems::IsGear(Id) ? JeebItems::KindOf(Id) : -1;
}

float AKnightCharacter::GetStaminaMult() const
{
	return JeebItems::StaminaMult(Skills);
}

void AKnightCharacter::ApplyArmorSlot(int32 Slot)
{
	if (!Skills) return;
	const int32 Kind = KindForSlot(Slot);
	int32 Id = (Slot == KnightDesign::SlotShield) ? (ShieldActive() ? Skills->Worn[Kind] : 0) : Skills->Worn[Kind];
	if (Slot == KnightDesign::SlotBrush)   // the brush is worn in one of the three miscellaneous slots
	{
		Id = 0;
		for (int32 M : { Skills->Worn[JeebItems::K_Ring], Skills->RingB, Skills->Worn[JeebItems::K_Amulet] })
			if (JeebItems::IsGear(M) && JeebItems::KindOf(M) == JeebItems::K_Crest) Id = M;
	}
	if (Slot == KnightDesign::SlotRingR || Slot == KnightDesign::SlotRingL || Slot == KnightDesign::SlotAmulet)   // jewellery in the miscellaneous slots
	{
		Id = 0;
		int32 Rings = 0;
		for (int32 M : { Skills->Worn[JeebItems::K_Ring], Skills->RingB, Skills->Worn[JeebItems::K_Amulet] })
		{
			if (!JeebItems::IsGear(M)) continue;
			const int32 MK = JeebItems::KindOf(M);
			if (MK == JeebItems::K_Amulet && Slot == KnightDesign::SlotAmulet) Id = M;
			if (MK == JeebItems::K_Ring) { ++Rings; if ((Rings == 1 && Slot == KnightDesign::SlotRingR) || (Rings == 2 && Slot == KnightDesign::SlotRingL)) Id = M; }
		}
	}
	const bool bWorn = Id != 0;
	const int32 Var = bWorn ? JeebItems::VariantOf(Id) : 0;   // tier (or cape colour)
	for (FArmorPart& P : ArmorParts)
	{
		if (P.Slot != Slot) continue;

		// Underclothes, belt and boots are always there; everything else shows only while worn
		bool bAlways = false;
		if (Slot == KnightDesign::SlotBody) bAlways = (P.Role == EArmorRole::Under || P.Role == EArmorRole::Leather);
		if (Slot == KnightDesign::SlotBoots) bAlways = (P.Role == EArmorRole::BootBody || P.Role == EArmorRole::Under);
		bool bVisible;
		if (P.bBare) bVisible = !bWorn;
		else if (bAlways) bVisible = true;
		else bVisible = bWorn && (Slot == KnightDesign::SlotCape || (Var >= P.MinTier && Var <= P.MaxTier));
		if (Slot == KnightDesign::SlotShield && bWorn && P.Style != JeebItems::StyleOf(Id)) bVisible = false;   // only the worn shield style
		P.Comp->SetVisibility(bVisible);
		if (!bVisible) continue;

		FLinearColor Color;
		bool bMetal = false;
		if (P.bBare || P.bFixedColor) Color = P.Fixed;
		else if (Slot == KnightDesign::SlotCape)
		{
			Color = JeebItems::CapeColor(Var);
			Color.R *= P.Shade; Color.G *= P.Shade; Color.B *= P.Shade;
		}
		else
		{
			Color = JeebItems::Palette(Var, static_cast<int32>(P.Role));
			Color.R *= P.Shade; Color.G *= P.Shade; Color.B *= P.Shade;
			bMetal = PartIsMetal(Var, P.Role);
		}
		ApplyPartMaterial(P, Color, bMetal);
	}
	if (Slot == KnightDesign::SlotCape && CapeCloth)
	{
		CapeCloth->SetVisibility(bWorn);
		if (CapeMid && bWorn) CapeMid->SetVectorParameterValue(TEXT("Color"), JeebItems::CapeColor(Var));
	}
}

void AKnightCharacter::RefreshEquipment()
{
	for (int32 Slot = 0; Slot < KnightDesign::NumSlots; ++Slot) ApplyArmorSlot(Slot);
	RebuildHeld();
}

void AKnightCharacter::RebuildHeld()
{
	for (UProceduralMeshComponent* C : HeldComps)
	{
		if (C) C->DestroyComponent();
	}
	HeldComps.Reset();
	EaseComps.Reset();
	HeldElbow = 25.f;
	if (!Skills) return;
	const int32 Id = Skills->SelectedItem();
	if (!JeebItems::IsGear(Id)) return;
	const int32 K = JeebItems::KindOf(Id), T = JeebItems::VariantOf(Id);
	if (!JeebItems::IsWeaponKind(K)) return;
	HeldElbow = WeaponDesign::IdleElbow(K);

	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"));
	UMaterialInterface* Metal = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Metal.M_Metal"));

	auto Make = [&](USceneComponent* Attach, int32 PivotId, const KM::FXf& Place)
	{
		TArray<WeaponDesign::FWPart> Parts;
		WeaponDesign::Build(K, Parts, Place);
		for (const WeaponDesign::FWPart& WP : Parts)
		{
			if (WP.Mesh.V.Num() == 0) continue;
			UProceduralMeshComponent* C = MakeMeshComp(Attach, PivotId, WP.Mesh);
			HeldComps.Add(C);
			FLinearColor Color;
			bool bMetal = false;
			if (WP.bFixed)
			{
				Color = WP.Fixed;
				Color.R *= WP.Glow; Color.G *= WP.Glow; Color.B *= WP.Glow;
			}
			else
			{
				Color = JeebItems::Palette(T, WP.Role);
				bMetal = PartIsMetal(T, static_cast<EArmorRole>(WP.Role));
			}
			UMaterialInterface* Mat = (bMetal && Metal) ? Metal : Base;
			if (UMaterialInstanceDynamic* M = C->CreateDynamicMaterialInstance(0, Mat))
			{
				M->SetVectorParameterValue(TEXT("Color"), Color);
				if (bMetal && Metal) { M->SetScalarParameterValue(TEXT("Metallic"), 0.9f); M->SetScalarParameterValue(TEXT("Roughness"), 0.28f); }
			}
		}
	};

	if (bStowed)
	{
		// On the back, diagonally: the grip over the right shoulder and the blade down to the left hip, lying flat against
		// the back (under the shield). Built at the grip, then turned flat (width across the back) and tilted 35 degrees.
		const int32 First = HeldComps.Num();
		Make(UpperPivot, KnightDesign::P_Upper, KM::Xf(FVector::ZeroVector));
		const FQuat Q = FQuat(FVector::XAxisVector, FMath::DegreesToRadians(-35.f)) * FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(90.f));
		for (int32 i = First; i < HeldComps.Num(); ++i)
			HeldComps[i]->SetRelativeLocationAndRotation(FVector(-21.5f, 14.f, 41.f + 16.f), Q);   // chest pivot sits 41 below the body's centre
		return;
	}
	const KM::FXf Tilt = KM::Xf(KnightDesign::GripPos(false), FVector(0, (K == JeebItems::K_Bow) ? -28.0 : -20.0, 0));
	{
		const int32 First = HeldComps.Num();
		Make(ElbowR, KnightDesign::P_ElbowR, Tilt);
		if (K == JeebItems::K_Sword || K == JeebItems::K_Broadsword || K == JeebItems::K_Katana)   // swords can be held at ease
			for (int32 i = First; i < HeldComps.Num(); ++i) EaseComps.Add(HeldComps[i]);
	}
	if (K == JeebItems::K_Katana) Make(ElbowL, KnightDesign::P_ElbowL, KM::Xf(KnightDesign::GripPos(true), FVector(0, -20, 0)));
}

// The shield on the arm, or on the back: turned to face backwards, its inner face just behind the sword on the back,
// centred on the spine with its point down by the hips (the cape is kept outside it, see TickCape)
void AKnightCharacter::PlaceShield(bool bOnBack)
{
	for (FArmorPart& P : ArmorParts)
	{
		if (P.Slot != KnightDesign::SlotShield || !P.Comp) continue;
		if (bOnBack)
		{
			P.Comp->AttachToComponent(UpperPivot, FAttachmentTransformRules::KeepRelativeTransform);
			P.Comp->SetRelativeLocationAndRotation(FVector(-10.f, -41.f, 41.f), FRotator(0.f, 180.f, 0.f));
		}
		else
		{
			P.Comp->AttachToComponent(ElbowL, FAttachmentTransformRules::KeepRelativeTransform);   // strapped to the forearm: HoldShield poses it
		}
	}
}

void AKnightCharacter::HoldShield()
{
	if (bStowed || !ElbowL || !ArmPivotL) return;
	// At rest: strapped along the outside of the left forearm, at the elbow and at the hand, its face turned out and forward
	// (diagonal to the body), hanging down at the side with its point near the knee. All in the forearm's own frame.
	static const FTransform Rest = []()
	{
		const FVector E = KnightDesign::PivotPos(KnightDesign::P_ElbowL), W(6.0, -34.6, -24.5);
		const FVector Fore = (E - W).GetSafeNormal();                       // along the forearm, towards the elbow
		const FVector Up = FVector(0.573, 0.0, 0.819);                       // the shield's top: forward of the shoulder, its point behind (top tipped 20 degrees forward)
		const FVector Want = FVector(0.676, -0.70, -0.181).GetSafeNormal();  // face turned to the front-left (diagonal), upright
		const FVector N = (Want - Up * FVector::DotProduct(Want, Up)).GetSafeNormal();
		const FQuat Q = FRotationMatrix::MakeFromXZ(N, Up).ToQuat();         // the shield's face (+X) and its top (+Z)
		const FVector Strap(14.0, -41.0, -8.0);                               // a point on the shield's back, a third of the way down
		const FVector Target = -Fore * 2.0 + N * 9.5 + FVector(5.0, 4.0, 0.0);   // and drawn a little round to the front                         // just below the elbow, outside the forearm
		return FTransform(Q, Target - Q.RotateVector(Strap));
	}();
	// Raised: exactly where the guard always put it (in front of the arm, turning with the shoulder)
	const FTransform Guard = (FTransform(-KnightDesign::PivotPos(KnightDesign::P_ArmL)) * ArmPivotL->GetComponentTransform()).GetRelativeTransform(ElbowL->GetComponentTransform());
	FTransform Now;
	Now.Blend(Rest, Guard, BlockAlpha);
	for (FArmorPart& P : ArmorParts)
	{
		if (P.Slot != KnightDesign::SlotShield || !P.Comp) continue;
		if (P.Comp->GetAttachParent() != ElbowL) P.Comp->AttachToComponent(ElbowL, FAttachmentTransformRules::KeepRelativeTransform);
		P.Comp->SetRelativeTransform(Now);
	}
}

void AKnightCharacter::HoldWeaponAtEase(float Dt, bool bBusy)
{
	EaseAlpha = FMath::FInterpTo(EaseAlpha, (bBusy || bStowed || EaseComps.Num() == 0) ? 0.f : 1.f, Dt, 9.f);
	if (EaseComps.Num() == 0) return;
	// The blade leaves the hand pointing down and forward (built that way); at ease it turns in so the tip rests in front
	// of the knight, between his feet (worked out for the arm's relaxed bend)
	static const FQuat EaseQ = []()
	{
		const FVector Blade = KM::RotateEuler(FVector(0, 0, -1), FVector(0, -20, 0));
		const FVector Want = FVector(0.32, 0.196, -0.923).GetSafeNormal();   // tip out in front, the blade turned 40 degrees from slanting in to hanging nearly straight
		return FQuat::FindBetweenNormals(Blade.GetSafeNormal(), Want);
	}();
	const FQuat Q = FQuat::Slerp(FQuat::Identity, EaseQ, EaseAlpha);
	const FVector G = KnightDesign::GripPos(false), PE = KnightDesign::PivotPos(KnightDesign::P_ElbowR);
	for (UProceduralMeshComponent* C : EaseComps)
		if (C) C->SetRelativeLocationAndRotation(G - PE - Q.RotateVector(G), Q);
}

void AKnightCharacter::EquipFromInventory(int32 Idx)
{
	if (!Skills) return;
	if (Skills->EquipFromSlot(Idx)) RefreshEquipment();
}

void AKnightCharacter::UnequipHotbar(int32 H)
{
	if (!Skills) return;
	if (Skills->UnequipHotbar(H)) RefreshEquipment();
	else if (GEngine) JeebMsg::Post(2.f, FColor::Orange, TEXT("No room in your inventory."));
}

void AKnightCharacter::UnequipWorn(int32 Kind, bool bSecondRing)
{
	if (!Skills) return;
	if (Skills->UnequipWorn(Kind, bSecondRing)) RefreshEquipment();
	else if (GEngine) JeebMsg::Post(2.f, FColor::Orange, TEXT("No room in your inventory."));
}

void AKnightCharacter::CycleHotbar(int32 Dir)
{
	if (!Skills || IsUIOpen() || bClimbing) return;
	for (int32 Step = 1; Step <= 5; ++Step)
	{
		const int32 Next = ((Skills->Selected + Dir * Step) % 5 + 5) % 5;
		if (Skills->Hotbar[Next] != 0) { SelectHotbar(Next); return; }
	}
}

void AKnightCharacter::SelectHotbar(int32 H)
{
	if (!Skills || H < 0 || H > 4 || Skills->Selected == H) return;
	Skills->Selected = H;
	bAttacking = false;
	bCharging = false;
	RefreshEquipment();
}

bool AKnightCharacter::PrepareTool(ESkillType Skill, float& OutTimeMult)
{
	OutTimeMult = 1.f;
	if (!Skills) return true;
	int32 Need;
	const TCHAR* Name;
	if (Skill == ESkillType::Mining) { Need = JeebItems::K_Pickaxe; Name = TEXT("pickaxe"); }
	else if (Skill == ESkillType::Woodcutting) { Need = JeebItems::K_Axe; Name = TEXT("axe"); }
	else if (Skill == ESkillType::Fishing) { Need = JeebItems::K_Rod; Name = TEXT("fishing pole"); }
	else return true;

	int32 Best = -1, BestTier = -1;
	for (int32 i = 0; i < 5; ++i)
	{
		const int32 Id = Skills->Hotbar[i];
		if (!JeebItems::IsGear(Id) || JeebItems::KindOf(Id) != Need) continue;
		// Keep the one already in hand if it is a tool of this kind
		const int32 Tier = JeebItems::VariantOf(Id) + (i == Skills->Selected ? 100 : 0);
		if (Tier > BestTier) { BestTier = Tier; Best = i; }
	}
	if (Best < 0)
	{
		if (GEngine) JeebMsg::Post(3.f, FColor::Orange, FString::Printf(TEXT("You need a %s on your weapon bar. Buy or smith one."), Name));
		return false;
	}
	if (Skills->Selected != Best) SelectHotbar(Best);
	OutTimeMult = JeebItems::ToolTimeMult(JeebItems::VariantOf(Skills->Hotbar[Best]));
	return true;
}

void AKnightCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	// Build the input actions and key bindings in code (WASD, mouse look, space to jump)
	JumpAction = NewObject<UInputAction>(this, TEXT("KnightJump"));
	JumpAction->ValueType = EInputActionValueType::Boolean;

	MoveAction = NewObject<UInputAction>(this, TEXT("KnightMove"));
	MoveAction->ValueType = EInputActionValueType::Axis2D;

	LookAction = NewObject<UInputAction>(this, TEXT("KnightLook"));
	LookAction->ValueType = EInputActionValueType::Axis2D;

	MouseLookAction = NewObject<UInputAction>(this, TEXT("KnightMouseLook"));
	MouseLookAction->ValueType = EInputActionValueType::Axis2D;

	KnightContext = NewObject<UInputMappingContext>(this, TEXT("KnightContext"));

	auto MakeSwizzle = [this]()
	{
		UInputModifierSwizzleAxis* S = NewObject<UInputModifierSwizzleAxis>(this);
		S->Order = EInputAxisSwizzle::YXZ;
		return S;
	};
	auto MakeNegate = [this]()
	{
		return NewObject<UInputModifierNegate>(this);
	};

	SprintAction = NewObject<UInputAction>(this, TEXT("KnightSprint"));
	SprintAction->ValueType = EInputActionValueType::Boolean;

	KnightContext->MapKey(JumpAction, EKeys::SpaceBar);
	KnightContext->MapKey(SprintAction, EKeys::LeftShift);

	InteractAction = NewObject<UInputAction>(this, TEXT("KnightInteract"));
	InteractAction->ValueType = EInputActionValueType::Boolean;
	KnightContext->MapKey(InteractAction, EKeys::LeftMouseButton);

	auto MakeBool = [this](const TCHAR* Name)
	{
		UInputAction* A = NewObject<UInputAction>(this, Name);
		A->ValueType = EInputActionValueType::Boolean;
		return A;
	};
	CrouchAction = MakeBool(TEXT("KnightCrouch"));
	BlockAction  = MakeBool(TEXT("KnightBlock"));
	SlideAction  = MakeBool(TEXT("KnightSlide"));
	RollAction   = MakeBool(TEXT("KnightRoll"));
	SitAction    = MakeBool(TEXT("KnightSit"));
	KnightContext->MapKey(CrouchAction, EKeys::LeftControl);
	KnightContext->MapKey(BlockAction, EKeys::RightMouseButton);
	KnightContext->MapKey(SlideAction, EKeys::C);
	KnightContext->MapKey(RollAction, EKeys::V);
	KnightContext->MapKey(RollAction, EKeys::LeftAlt);
	DodgeLAction = MakeBool(TEXT("KnightDodgeL"));
	KnightContext->MapKey(DodgeLAction, EKeys::A);
	DodgeRAction = MakeBool(TEXT("KnightDodgeR"));
	KnightContext->MapKey(DodgeRAction, EKeys::D);
	DodgeFAction = MakeBool(TEXT("KnightDodgeF"));
	KnightContext->MapKey(DodgeFAction, EKeys::W);
	DodgeBAction = MakeBool(TEXT("KnightDodgeB"));
	KnightContext->MapKey(DodgeBAction, EKeys::S);
	PotionAction = MakeBool(TEXT("KnightPotion"));
	KnightContext->MapKey(PotionAction, EKeys::Q);
	KnightContext->MapKey(SitAction, EKeys::E);

	{
		static const FKey HotbarKeys[5] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five };
		for (int32 i = 0; i < 5; ++i)
		{
			HotbarActions[i] = MakeBool(*FString::Printf(TEXT("KnightHotbar%d"), i + 1));
			KnightContext->MapKey(HotbarActions[i], HotbarKeys[i]);
		}
	}
		ScrollUpAction = MakeBool(TEXT("KnightScrollUp"));
	KnightContext->MapKey(ScrollUpAction, EKeys::MouseScrollUp);
	ScrollDownAction = MakeBool(TEXT("KnightScrollDown"));
	KnightContext->MapKey(ScrollDownAction, EKeys::MouseScrollDown);
	InventoryAction = MakeBool(TEXT("KnightInventory"));
	KnightContext->MapKey(InventoryAction, EKeys::Tab);
	PanelTabAction = MakeBool(TEXT("KnightPanelTab"));
	KnightContext->MapKey(PanelTabAction, EKeys::CapsLock);
	SkillsAction = MakeBool(TEXT("KnightSkills"));
	KnightContext->MapKey(SkillsAction, EKeys::K);
	CallHorseAction = MakeBool(TEXT("KnightCallHorse"));
	KnightContext->MapKey(CallHorseAction, EKeys::T);

	SettingsAction = NewObject<UInputAction>(this, TEXT("KnightSettings"));
	SettingsAction->ValueType = EInputActionValueType::Boolean;
	KnightContext->MapKey(SettingsAction, EKeys::O);

	KnightContext->MapKey(MoveAction, EKeys::D); // right = +X
	KnightContext->MapKey(MoveAction, EKeys::A).Modifiers.Add(MakeNegate());
	KnightContext->MapKey(MoveAction, EKeys::W).Modifiers.Add(MakeSwizzle()); // forward = +Y
	{
		FEnhancedActionKeyMapping& Back = KnightContext->MapKey(MoveAction, EKeys::S);
		Back.Modifiers.Add(MakeSwizzle());
		Back.Modifiers.Add(MakeNegate());
	}

	{
		UInputModifierNegate* FlipY = NewObject<UInputModifierNegate>(this);
		FlipY->bX = false;
		FlipY->bY = true;
		FlipY->bZ = false;
		GConfig->GetFloat(TEXT("Jeebsland"), TEXT("LookSensitivity"), LookSensitivity, GGameUserSettingsIni);
		FEnhancedActionKeyMapping& MouseMap = KnightContext->MapKey(MouseLookAction, EKeys::Mouse2D);
		MouseMap.Modifiers.Add(FlipY);
	}

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (ULocalPlayer* LP = PC->GetLocalPlayer())
		{
			if (UEnhancedInputLocalPlayerSubsystem* Subsystem = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
			{
				Subsystem->AddMappingContext(KnightContext, 0);
			}
		}
	}

	// The template's code now binds these actions to movement, looking, and jumping
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	// Hold Shift to sprint
	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		EIC->BindAction(SprintAction, ETriggerEvent::Started, this, &AKnightCharacter::StartSprint);
		EIC->BindAction(SprintAction, ETriggerEvent::Completed, this, &AKnightCharacter::StopSprint);
		EIC->BindAction(InteractAction, ETriggerEvent::Started, this, &AKnightCharacter::Interact);
		EIC->BindAction(InteractAction, ETriggerEvent::Completed, this, &AKnightCharacter::InteractReleased);
		EIC->BindAction(SettingsAction, ETriggerEvent::Started, this, &AKnightCharacter::OnSettingsKey);
		EIC->BindAction(CrouchAction, ETriggerEvent::Started, this, &AKnightCharacter::StartCrouch);
		EIC->BindAction(CrouchAction, ETriggerEvent::Completed, this, &AKnightCharacter::StopCrouch);
		EIC->BindAction(BlockAction, ETriggerEvent::Started, this, &AKnightCharacter::StartBlock);
		EIC->BindAction(BlockAction, ETriggerEvent::Completed, this, &AKnightCharacter::StopBlock);
		EIC->BindAction(SlideAction, ETriggerEvent::Started, this, &AKnightCharacter::StartSlide);
		EIC->BindAction(RollAction, ETriggerEvent::Started, this, &AKnightCharacter::StartRoll);
		EIC->BindAction(SitAction, ETriggerEvent::Started, this, &AKnightCharacter::OnInteractKey);
		EIC->BindAction(InventoryAction, ETriggerEvent::Started, this, &AKnightCharacter::OnInventoryKey);
		EIC->BindAction(PanelTabAction, ETriggerEvent::Started, this, &AKnightCharacter::CyclePanelTab);
		EIC->BindAction(DodgeLAction, ETriggerEvent::Started, this, &AKnightCharacter::DodgeLeft);
		EIC->BindAction(DodgeRAction, ETriggerEvent::Started, this, &AKnightCharacter::DodgeRight);
		EIC->BindAction(DodgeFAction, ETriggerEvent::Started, this, &AKnightCharacter::DodgeFwd);
		EIC->BindAction(DodgeBAction, ETriggerEvent::Started, this, &AKnightCharacter::DodgeBack);
		EIC->BindAction(PotionAction, ETriggerEvent::Started, this, &AKnightCharacter::DrinkPotion);
		EIC->BindAction(CallHorseAction, ETriggerEvent::Started, this, &AKnightCharacter::CallHorse);
		EIC->BindAction(SkillsAction, ETriggerEvent::Started, this, &AKnightCharacter::OnSkillsKey);
				EIC->BindAction(ScrollUpAction, ETriggerEvent::Started, this, &AKnightCharacter::ScrollUp);
		EIC->BindAction(ScrollDownAction, ETriggerEvent::Started, this, &AKnightCharacter::ScrollDown);
		EIC->BindAction(HotbarActions[0], ETriggerEvent::Started, this, &AKnightCharacter::Select1);
		EIC->BindAction(HotbarActions[1], ETriggerEvent::Started, this, &AKnightCharacter::Select2);
		EIC->BindAction(HotbarActions[2], ETriggerEvent::Started, this, &AKnightCharacter::Select3);
		EIC->BindAction(HotbarActions[3], ETriggerEvent::Started, this, &AKnightCharacter::Select4);
		EIC->BindAction(HotbarActions[4], ETriggerEvent::Started, this, &AKnightCharacter::Select5);
	}
}

void AKnightCharacter::StartSprint()
{
	bSprinting = true;
	bCrouched = false; // sprinting stands you up
}

void AKnightCharacter::StopSprint()
{
	bSprinting = false;
}

void AKnightCharacter::Interact()
{
	if (IsUIOpen() || bPanelCursor || bSitting || bRolling || bSliding || bClimbing) return; // clicks belong to the open screen (or the side panel); no attacks mid-move
	if (ActiveNode.IsValid()) return;                               // already working
	if (bAttacking) { QueuedClick = GetWorld()->GetTimeSeconds(); return; }   // swinging: remember the click for the combo

	// Gather from the closest rock or tree within reach that the knight is facing
		AResourceNode* Best = nullptr;
	float BestDist = 1.0e9f;
	for (TActorIterator<AResourceNode> It(GetWorld()); It; ++It)
	{
		const FVector ToNode = It->GetActorLocation() - GetActorLocation();
		const float D = ToNode.Size2D();
		if (D >= It->ReachRadius()) continue;
		const float FacingDot = FVector::DotProduct(GetActorForwardVector().GetSafeNormal2D(), ToNode.GetSafeNormal2D());
		if (FacingDot < It->MinFacing()) continue; // outside about 60 degrees (wider for fishing)
		if (D < BestDist)
		{
			BestDist = D;
			Best = *It;
		}
	}

	const bool bHandled = Best && Best->StartGathering(this);

	// Nothing to gather: just swing whatever is in your hand
	if (!ActiveNode.IsValid() && !bHandled)
	{
		const int32 K = SelectedKind();
		const float Now = GetWorld()->GetTimeSeconds();
		if (K == JeebItems::K_Spear)
		{
			bCharging = true;       // hold to charge, let go to thrust
			ChargeT = 0.f;
		}
		else if (K == JeebItems::K_Bow)
		{
			const int32 Tier = Skills ? Skills->TakeBestArrow() : -1;
			if (Tier < 0)
			{
				if (GEngine) JeebMsg::Post(2.f, FColor::Orange, TEXT("You have no arrows."));
				return;
			}
			PendingArrowTier = Tier;
			StartAttack(EAtkStyle::Cast, 1, 0.f);
		}
		else if (K == JeebItems::K_Wand || K == JeebItems::K_Staff)
		{
			StartAttack(EAtkStyle::Cast, 1, 0.f);
		}
		else
		{
			// A second swing within 0.3 s of the first sweeps back left-to-right, a little faster
			const bool bCombo = (LastStyle == EAtkStyle::Slash && LastSwingDir > 0 && Now - LastSwingEnd <= 0.3f);
			StartAttack(EAtkStyle::Slash, bCombo ? -1 : 1, 0.f);
		}
	}
}

void AKnightCharacter::InteractReleased()
{
	if (!bCharging) return;
	bCharging = false;
	if (IsUIOpen() || bAttacking || bSitting || bRolling || bSliding || bClimbing) return;
	const float C = FMath::Clamp(ChargeT / 1.0f, 0.f, 1.f);
	StartAttack(EAtkStyle::Thrust, 1, C);
	if (!GetCharacterMovement()->IsFalling())
	{
		LaunchCharacter(GetActorForwardVector().GetSafeNormal2D() * (200.f + 700.f * C), true, false);   // lunge forward
	}
}

void AKnightCharacter::StartAttack(EAtkStyle Style, int32 Dir, float Charge)
{
	const int32 K = SelectedKind();
	const float Cd = (K >= 0) ? JeebItems::WeaponBase(K).Cooldown : 0.5f;
	AtkStyle = Style;
	AttackDir = Dir;
	AtkCharge = Charge;
	AttackDur = (Style == EAtkStyle::Thrust) ? 0.45f : (Style == EAtkStyle::Cast ? FMath::Max(0.45f, Cd * 1.1f) : Cd * 1.25f);
	if (Dir < 0) AttackDur *= 0.85f;           // the follow-up swing is 15% faster
	bAttacking = true;
	bBlocking = false;   // the shield drops for the swing; Tick raises it again afterwards if the block button is still held
	bAttackHit = false;
	AttackT = 0.f;
	QueuedClick = -10.f;
	if (AController* C = GetController()) SetActorRotation(FRotator(0.f, C->GetControlRotation().Yaw, 0.f));   // face where you aim
}

void AKnightCharacter::StartCrouch()
{
	if (bSitting || bRolling || bSliding || bClimbing || bMounted) return;
	bCrouched = true;
}

void AKnightCharacter::StopCrouch()
{
	bCrouched = false;
}

void AKnightCharacter::DealDamage(UHealthComponent* Target, int32 Dmg, bool bCrit, ESkillType Skill)
{
	if (!Target || !Target->IsAlive()) return;
	const int32 Dealt = Target->ApplyDamage(Dmg, this, bCrit);
	if (Dealt > 0 && Skills) Skills->AddXP(Skill, 3 + Dealt / 3);
}

void AKnightCharacter::AttackLands()
{
	if (!Skills) return;
	const int32 Id = Skills->SelectedItem();
	const int32 K = JeebItems::IsGear(Id) ? JeebItems::KindOf(Id) : -1;
	const int32 Tier = (K >= 0) ? JeebItems::VariantOf(Id) : 0;
	const ESkillType Sk = JeebItems::CombatSkillFor(K < 0 ? JeebItems::K_Sword : K);

	if (AtkStyle == EAtkStyle::Cast)
	{
		FRotator Aim = GetActorRotation();
		if (AController* C = GetController()) Aim = C->GetControlRotation();
		const FVector Dir = Aim.Vector();
		const bool bBow = (K == JeebItems::K_Bow);
		bool bCrit = false;
		const int32 Dmg = JeebItems::RollDamage(Skills, K, Tier, bBow ? PendingArrowTier : 0, bCrit);
		const float Range = JeebItems::WeaponBase(K).Range;
		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const FVector Start = GetActorLocation() + GetActorForwardVector() * 70.f + FVector(0.f, 0.f, 30.f);
		if (AProjectile* P = GetWorld()->SpawnActor<AProjectile>(AProjectile::StaticClass(), Start, Dir.Rotation(), SP))
		{
			P->Launch(this, Dir, bBow ? 3500.f : (K == JeebItems::K_Staff ? 1800.f : 2200.f), Range, Dmg, bCrit, Sk, !bBow,
				bBow ? PendingArrowTier : (K == JeebItems::K_Staff ? 100 : 0));
		}
		return;
	}

	const bool bThrust = (AtkStyle == EAtkStyle::Thrust);
	const float Range = ((K >= 0) ? JeebItems::WeaponBase(K).Range : 200.f) + (bThrust ? 140.f * AtkCharge : 0.f);
	const float MinDot = bThrust ? 0.8f : 0.2f;
	const FVector Fwd = GetActorForwardVector().GetSafeNormal2D();

	TArray<UHealthComponent*> Targets;
	UHealthComponent* Nearest = nullptr;
	float NearestD = 1.0e9f;
	for (UHealthComponent* H : UHealthComponent::All())
	{
		if (!H || !H->IsAlive() || H->GetOwner() == this) continue;
		const FVector To = H->GetOwner()->GetActorLocation() - GetActorLocation();
		const float D = To.Size2D();
		if (D > Range + H->HitRadius * 0.4f) continue;
		if (D > 40.f && FVector::DotProduct(Fwd, To.GetSafeNormal2D()) < MinDot) continue;
		if (FMath::Abs(To.Z) > 260.f) continue;
		Targets.Add(H);
		if (D < NearestD) { NearestD = D; Nearest = H; }
	}
	if (bThrust && Nearest) { Targets.Reset(); Targets.Add(Nearest); }
	for (UHealthComponent* H : Targets)
	{
		bool bCrit = false;
		const int32 Dmg = JeebItems::RollDamage(Skills, K, Tier, 0, bCrit, bThrust ? (1.f + AtkCharge) : 1.f);
		DealDamage(H, Dmg, bCrit, Sk);
	}
}

void AKnightCharacter::Hurt(int32 Raw, AActor* From, bool bEnvironmental)
{
	if (Health <= 0.f || Raw <= 0 || !Skills) return;
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now < InvulnUntil) return;

	bool bFront = false;
	if (!bEnvironmental && bBlocking && ShieldActive() && From)
	{
		const FVector To = (From->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
		bFront = FVector::DotProduct(GetActorForwardVector().GetSafeNormal2D(), To) > 0.1f;
	}
	const int32 Armor = bEnvironmental ? 0 : JeebItems::TotalDefense(Skills, false) / 3;   // smoke seeps through armour
	const int32 FromSkill = bEnvironmental ? 0 : Skills->GetLevel(ESkillType::Defense) / 5;
	// The shield takes the blow first. With its guard up it stops damage until the guard (its Defence) is spent; a broken
	// guard stops half of each blow until it has recovered. Whatever gets past the shield is then softened by armour.
	int32 ShieldStop = 0;
	if (bFront)
	{
		if (!bGuardBroken)
		{
			ShieldStop = FMath::Min(Raw, FMath::FloorToInt(GuardPool));
			GuardPool -= ShieldStop;
			if (GuardPool < 1.f)
			{
				GuardPool = 0.f;
				bGuardBroken = true;
				JeebMsg::Post(2.5f, FColor::Orange, TEXT("Guard broken! Your shield stops only half a blow until it recovers."));
			}
		}
		else ShieldStop = Raw / 2;
		LastGuardHitTime = Now;
	}
	const int32 Through = Raw - ShieldStop;
	const int32 Taken = (Through <= 0) ? 0 : FMath::Max(1, Through - (Armor + FromSkill));

	const FVector Head = GetActorLocation() + FVector(0.f, 0.f, 120.f);
	if (ShieldStop > 0) JeebCombat::AddNumber(GetWorld(), Head + FVector(0.f, 0.f, 25.f), ShieldStop, JeebCombat::N_Blocked);
	if (Taken > 0) JeebCombat::AddNumber(GetWorld(), Head, Taken, JeebCombat::N_Damage);
	Health -= Taken;
	LastHurtTime = Now;
	InvulnUntil = Now + 0.4f;
	// Defence trains only from blows you block with a raised shield, facing the attacker.
	// Armour soaking a hit trains nothing, nor do hazards like poison smoke, nor damage that got through.
	if (!bEnvironmental && ShieldStop > 0) Skills->AddXP(ESkillType::Defense, 2 + ShieldStop / 2);
	if (Health <= 0.f) Die();   // any death: the sack, and a fresh start in Blackrock
}

void AKnightCharacter::TryStartClimb()
{
	if (bSitting || bRolling || bSliding || bMounted || IsUIOpen()) return;
	UCharacterMovementComponent* Move = GetCharacterMovement();
	const FVector Acc = Move->GetCurrentAcceleration();
	if (Acc.SizeSquared2D() < 1.f) return;
	const FVector2D WishDir = FVector2D(Acc.X, Acc.Y).GetSafeNormal();
	const FVector Loc = GetActorLocation();
	const FVector2D P2(Loc.X, Loc.Y);
	const float FeetZ = Loc.Z - 90.f;
	const FVector2D VC = JeebTerrain::VillageCenter();
	const float VillageDeckTop = JeebTerrain::HeightAt(VC.X, VC.Y) + JeebTerrain::DeckHeight();

	static TArray<JeebTerrain::FLadder> AllLadders;
	if (AllLadders.Num() == 0)
	{
		AllLadders.Append(JeebTerrain::Ladders());
		AllLadders.Append(JeebTerrain::FortLadders());   // rope ladders up to the redwood forts
		AllLadders.Append(JeebTerrain::CastleLadders());   // and up the castle wall
	}
	for (const JeebTerrain::FLadder& L : AllLadders)
	{
		const float DeckTop = (L.Top > 0.f) ? L.Top : VillageDeckTop;
		const FVector2D Rel = P2 - L.Pos;
		const FVector2D Side(-L.Out.Y, L.Out.X);
		if (FMath::Abs(FVector2D::DotProduct(Rel, Side)) > 70.f) continue;
		const float Along = FVector2D::DotProduct(Rel, L.Out);
				const float Ground = (L.Bottom > 0.f) ? L.Bottom : JeebTerrain::HeightAt(L.Pos.X, L.Pos.Y);
		const bool bAtBottom = !Move->IsFalling() && FeetZ < Ground + 60.f && Along > 0.f && Along < 150.f;
		const bool bOnDeck = !Move->IsFalling() && FeetZ > DeckTop - 40.f && Along < 0.f && Along > -150.f;
		int32 Dir = 0;
		if (bAtBottom && FVector2D::DotProduct(WishDir, -L.Out) > 0.6f) Dir = 1;
		else if (bOnDeck && FVector2D::DotProduct(WishDir, L.Out) > 0.6f) Dir = -1;
		if (Dir == 0) continue;

		bClimbing = true;
		bClimbIgnoreUp = (Dir < 0);
		bCrouched = false;
		bBlocking = false;
		bAttacking = false;
		ClimbPos = L.Pos;
		ClimbOut = L.Out;
				ClimbGround = Ground;
		ClimbT = 0.f;
		ClimbDeckTop = DeckTop;
		Move->StopMovementImmediately();
		Move->DisableMovement();
		const FVector2D Hold = L.Pos + L.Out * 55.f;
		SetActorLocationAndRotation(FVector(Hold.X, Hold.Y, Loc.Z), FRotator(0.f, FVector(-L.Out.X, -L.Out.Y, 0.f).Rotation().Yaw, 0.f));
		return;
	}
}

void AKnightCharacter::UpdateClimb(float Dt)
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	const bool bUp = PC && (PC->IsInputKeyDown(EKeys::W) || PC->IsInputKeyDown(EKeys::Up));
	const bool bDown = PC && (PC->IsInputKeyDown(EKeys::S) || PC->IsInputKeyDown(EKeys::Down));
	if (!bUp) bClimbIgnoreUp = false;
	const float Dir = (bUp && !bClimbIgnoreUp) ? 1.f : (bDown ? -1.f : 0.f);

			ClimbT += Dt;
	{
		// Nothing else may turn gravity back on while you are on the rope
		UCharacterMovementComponent* Cm = GetCharacterMovement();
		if (Cm->MovementMode != MOVE_None) Cm->DisableMovement();
		Cm->Velocity = FVector::ZeroVector;
	}
	FVector Loc = GetActorLocation();
	const FVector2D Hold = ClimbPos + ClimbOut * 55.f;
	Loc.X = FMath::FInterpTo(Loc.X, Hold.X, Dt, 15.f);
	Loc.Y = FMath::FInterpTo(Loc.Y, Hold.Y, Dt, 15.f);
	Loc.Z += Dir * 280.f * Dt;
	SetActorLocation(Loc, false);
	ClimbPhase += Dir * Dt * 7.f;

	const float FeetZ = Loc.Z - 90.f;
	if (Dir > 0.f && FeetZ >= ClimbDeckTop - 5.f) ExitClimb(true, false);
	else if (Dir < 0.f && FeetZ <= ClimbGround + 4.f) ExitClimb(false, true);
}

void AKnightCharacter::ExitClimb(bool bAtTop, bool bAtBottom)
{
	if (!bClimbing) return;
	bClimbing = false;
	UCharacterMovementComponent* Move = GetCharacterMovement();
	if (bAtTop)
	{
		const FVector2D P = ClimbPos - ClimbOut * 85.f;
		SetActorLocation(FVector(P.X, P.Y, ClimbDeckTop + 95.f), false);
	}
	else if (bAtBottom)
	{
				const bool bFromDeck = ClimbGround > JeebTerrain::HeightAt(ClimbPos.X, ClimbPos.Y) + 40.f;   // a ladder between two terraces
		const FVector2D P = ClimbPos + ClimbOut * (bFromDeck ? 60.f : 115.f);
		SetActorLocation(FVector(P.X, P.Y, (bFromDeck ? ClimbGround : JeebTerrain::HeightAt(P.X, P.Y)) + 95.f), false);
	}
	Move->SetMovementMode(MOVE_Falling);
	if (!bAtTop && !bAtBottom)
	{
		Move->Velocity = FVector(ClimbOut.X * 250.f, ClimbOut.Y * 250.f, 120.f); // let go: hop off the ladder
	}
}

void AKnightCharacter::StartBlock()
{
	bBlockHeld = true;
	if (bSitting || bRolling || bClimbing || IsUIOpen() || !ShieldActive() || bAttacking) return;   // no blocking mid-swing
	bBlocking = true;
}

void AKnightCharacter::StopBlock()
{
	bBlockHeld = false;
	bBlocking = false;
}

void AKnightCharacter::StartSlide()
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	const float Speed = GetVelocity().Size2D();
	if (bSliding || bRolling || bSitting || bClimbing || bMounted || SlideCooldown > 0.f || Move->IsFalling()) return;
	if (!bSprinting || Speed < 480.f || Stamina < 10.f) return; // slides start from a sprint

	SlideDir = GetVelocity().GetSafeNormal2D();
	SlideSpeed = FMath::Max(Speed, 750.f) * 1.15f;
	SlideT = 0.f;
	SlideDustTimer = 0.f;
	Stamina -= 10.f;
	StaminaDelay = 1.0f;
	bCrouched = false;
	bBlocking = false;
	bSavedOrient = Move->bOrientRotationToMovement;
	Move->bOrientRotationToMovement = false;
	SetActorRotation(FRotator(0.f, SlideDir.Rotation().Yaw, 0.f));
	bSliding = true;
}

void AKnightCharacter::StartRoll()
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	if (bRolling || bSliding || bSitting || bClimbing || bMounted || RollCooldown > 0.f || Move->IsFalling() || Stamina < 20.f) return;

	// Roll the way you are steering, or straight ahead if you are standing still
	FVector Dir = PendingRollDir.IsNearlyZero() ? Move->GetCurrentAcceleration().GetSafeNormal2D() : PendingRollDir;
	PendingRollDir = FVector::ZeroVector;
	if (Dir.IsNearlyZero()) Dir = GetActorForwardVector().GetSafeNormal2D();
	RollDir = Dir;
	RollT = 0.f;
	Stamina -= 20.f;
	StaminaDelay = 1.0f;
	bCrouched = false;
	bBlocking = false;
	bSavedOrient = Move->bOrientRotationToMovement;
	Move->bOrientRotationToMovement = false;
	SetActorRotation(FRotator(0.f, RollDir.Rotation().Yaw, 0.f));
	bRolling = true;
	SpawnDust(GetActorLocation() + FVector(0, 0, -85.f), 55.f);
}

AKnightCharacter::EInteract AKnightCharacter::FindInteractable(int32& OutIndex, ANpc*& OutNpc, AHorse*& OutHorse) const
{
	OutIndex = -1;
	OutNpc = nullptr;
	OutHorse = nullptr;
	const FVector2D Me(GetActorLocation().X, GetActorLocation().Y);
	// Your horse, if it is standing right beside you
	for (TActorIterator<AHorse> It(GetWorld()); It; ++It)
	{
		if (It->HasRider()) continue;
		if (FVector2D::Distance(Me, FVector2D(It->GetActorLocation().X, It->GetActorLocation().Y)) < 200.f) { OutHorse = *It; return EInteract::Horse; }
	}
	// Villagers (store, smithy, gate knight): talk to the nearest one in reach
	float NpcD = 230.f;
	for (TActorIterator<ANpc> It(GetWorld()); It; ++It)
	{
		const float D = FVector2D::Distance(Me, FVector2D(It->GetActorLocation().X, It->GetActorLocation().Y));
		if (D < NpcD) { NpcD = D; OutNpc = *It; }
	}
		if (OutNpc) return EInteract::Npc;
	// Things left on the ground by what died
	FoundDrop = nullptr;
	{
		float BestDrop = 190.f;
		for (TActorIterator<ALootDrop> It(GetWorld()); It; ++It)
		{
			const float D = FVector2D::Distance(Me, FVector2D(It->GetActorLocation().X, It->GetActorLocation().Y));
			if (D < BestDrop && FMath::Abs(GetActorLocation().Z - 90.f - It->GetActorLocation().Z) < 150.f) { BestDrop = D; FoundDrop = *It; }
		}
		if (FoundDrop.IsValid()) return EInteract::Drop;
	}
	// Doors of huts, and the ingot stash inside the locked ones
	FoundCottage = nullptr;
	{
		const FVector Me3 = GetActorLocation();
		ACottage* LootHut = nullptr;
		ACottage* DoorHut = nullptr;
		float BestLoot = 170.f, BestDoor = 210.f;
		for (TActorIterator<ACottage> It(GetWorld()); It; ++It)
		{
			if (It->HasLoot())
			{
				const FVector LP = It->LootPos();
				const float D = FVector::Dist2D(Me3, LP);
				if (D < BestLoot && FMath::Abs(Me3.Z - 90.f - LP.Z) < 150.f) { BestLoot = D; LootHut = *It; }
			}
			const FVector DP = It->DoorPos();
			const float D = FVector::Dist2D(Me3, DP);
			if (D < BestDoor && FMath::Abs(Me3.Z - 90.f - DP.Z) < 200.f) { BestDoor = D; DoorHut = *It; }
		}
		if (LootHut) { FoundCottage = LootHut; return EInteract::Loot; }
		if (DoorHut) { FoundCottage = DoorHut; return EInteract::Door; }
	}
	// Benches
	float BestD = 170.f;
	const TArray<JeebTerrain::FSeat>& Seats = JeebTerrain::Seats();
	for (int32 i = 0; i < Seats.Num(); ++i)
	{
		const float D = FVector2D::Distance(Me, Seats[i].Pos);
		if (D < BestD) { BestD = D; OutIndex = i; }
	}
	return OutIndex >= 0 ? EInteract::Bench : EInteract::None;
}

bool AKnightCharacter::GetInteractPrompt(FString& OutText) const
{
	if (IsUIOpen() || bRolling || bSliding || bClimbing) return false;
	if (bSitting || bMounted) return false;   // E to stand up or dismount still works; it just shows no prompt
	if (GetCharacterMovement()->IsFalling()) return false;
	int32 Idx = -1;
	ANpc* Npc = nullptr;
	AHorse* Horse = nullptr;
	switch (FindInteractable(Idx, Npc, Horse))
	{
	case EInteract::Horse: OutText = TEXT("Press E to mount the horse"); return true;
	case EInteract::Npc:
		OutText = FString::Printf(TEXT("Press E to talk to %s"), *Npc->GetNpcName());
		return true;
	case EInteract::Bench: OutText = TEXT("Press E to sit"); return true;
		case EInteract::Loot:  OutText = TEXT("Press E to take the ingots"); return true;
	case EInteract::Drop:
		if (const ALootDrop* Drop = FoundDrop.Get()) { OutText = Drop->IsSack() ? FString(TEXT("Press E to open the sack")) : Drop->Describe(); return true; }
		return false;
	case EInteract::Door:
	{
		const ACottage* Hut = FoundCottage.Get();
		if (!Hut) return false;
		if (Hut->IsLocked())
		{
			const int32 Need = Hut->GetLockLevel();
			OutText = (Skills && Skills->GetLevel(ESkillType::Lockpicking) >= Need)
				? FString::Printf(TEXT("Press E to pick the lock (Lockpicking %d)"), Need)
				: FString::Printf(TEXT("Locked. Needs Lockpicking level %d"), Need);
		}
				else OutText = Hut->IsDoorOpen() ? TEXT("Press E to close the door") : TEXT("Press E to open the door");
		return true;
	}
	default: break;
	}
	// Facing a place to fish
	if (!bFishing)
	{
		for (TActorIterator<AFishingSpot> It(GetWorld()); It; ++It)
		{
			const FVector To = It->GetActorLocation() - GetActorLocation();
			if (To.Size2D() < It->ReachRadius() && FVector::DotProduct(GetActorForwardVector().GetSafeNormal2D(), To.GetSafeNormal2D()) > It->MinFacing())
			{
				OutText = It->PromptText();
				return true;
			}
		}
	}
	return false;
}

bool AKnightCharacter::GetInteractAnchor(FVector& Out, bool& bOutE) const
{
	if (IsUIOpen() || bRolling || bSliding || bClimbing || bSitting || bMounted || GetCharacterMovement()->IsFalling()) return false;
	const FVector Me = GetActorLocation();
	auto Float = [&](const FVector& P, float Up)   // a hand's breadth toward the knight and up, so it hangs just in front of the thing
	{
		FVector To = Me - P;
		To.Z = 0.f;
		Out = P + To.GetSafeNormal() * 10.f + FVector(0.f, 0.f, Up);   // right against the thing (the prompt is drawn on screen, so it can never sink into a wall)
	};
	bOutE = true;
	int32 Idx = -1;
	ANpc* Npc = nullptr;
	AHorse* Horse = nullptr;
	switch (FindInteractable(Idx, Npc, Horse))
	{
	case EInteract::Horse: Float(Horse->GetActorLocation(), 150.f); return true;
	case EInteract::Npc:   Float(Npc->GetActorLocation(), 130.f); return true;
	case EInteract::Bench:
	{
		const FVector2D P = JeebTerrain::Seats()[Idx].Pos;
		Float(FVector(P.X, P.Y, JeebTerrain::HeightAt(P.X, P.Y)), 90.f);
		return true;
	}
	case EInteract::Loot:
		if (const ACottage* Hut = FoundCottage.Get()) { Float(Hut->LootPos(), 70.f); return true; }
		return false;
	case EInteract::Drop:
		if (const ALootDrop* Drop = FoundDrop.Get()) { Float(Drop->GetActorLocation(), 60.f); return true; }
		return false;
	case EInteract::Door:
	{
		const ACottage* Hut = FoundCottage.Get();
		if (!Hut) return false;
		Float(Hut->DoorPos(), 105.f);   // door handle height
		if (Hut->IsLocked() && !(Skills && Skills->GetLevel(ESkillType::Lockpicking) >= Hut->GetLockLevel())) bOutE = false;
		return true;
	}
	default: break;
	}
	if (!bFishing)
	{
		for (TActorIterator<AFishingSpot> It(GetWorld()); It; ++It)
		{
			const FVector To = It->GetActorLocation() - Me;
			if (To.Size2D() < It->ReachRadius() && FVector::DotProduct(GetActorForwardVector().GetSafeNormal2D(), To.GetSafeNormal2D()) > It->MinFacing())
			{
				Float(It->GetActorLocation(), 60.f);
				bOutE = false;   // fishing is a click, not an E
				return true;
			}
		}
	}
	return false;
}


void AKnightCharacter::OnInteractKey()
{
	if (bSitting) { StandUp(); return; }
	if (bMounted && !IsUIOpen()) { Dismount(); return; }
	if (bDialogueOpen) { AdvanceDialogue(); return; }
	if (bShopOpen) { CloseShop(); return; }
	if (bSmithOpen) { CloseSmith(); return; }
	if (bBankOpen) { CloseBank(); return; }
	if (IsUIOpen() || bRolling || bSliding || bClimbing || GetCharacterMovement()->IsFalling()) return;
	int32 Idx = -1;
	ANpc* Npc = nullptr;
	AHorse* Horse = nullptr;
	switch (FindInteractable(Idx, Npc, Horse))
	{
	case EInteract::Horse: Mount(Horse); break;
	case EInteract::Npc:   StartDialogue(Npc); break;
	case EInteract::Bench: SitOnSeat(Idx); break;
		case EInteract::Door:  UseDoor(FoundCottage.Get()); break;
	case EInteract::Drop:
		if (ALootDrop* Drop = FoundDrop.Get())
		{
			if (Drop->IsSack()) OpenLoot(Drop);   // a sack opens its own windows beside yours, with the cursor out
			else Drop->TakeAll(this);
		}
		break;
	case EInteract::Loot:
		if (ACottage* Hut = FoundCottage.Get())
		{
			const int32 Got = Hut->TakeLoot();
			if (Got > 0 && Skills)
			{
				Skills->AddIngots(Got);
				if (GEngine) JeebMsg::Post(3.f, FColor::Yellow, FString::Printf(TEXT("You take %d ingots."), Got));
			}
		}
		break;
	default: break;
	}
}

void AKnightCharacter::UseDoor(ACottage* Hut)
{
	if (!Hut || !Skills) return;
	if (!Hut->IsLocked()) { Hut->ToggleDoor(); return; }

	const float Now = GetWorld()->GetTimeSeconds();
	const int32 Need = Hut->GetLockLevel();
	const int32 Lv = Skills->GetLevel(ESkillType::Lockpicking);
	if (Lv < Need)
	{
		if (GEngine) JeebMsg::Post(3.f, FColor::Orange, FString::Printf(TEXT("The lock is too fine for you. Lockpicking level %d needed."), Need));
		return;
	}
	if (Now < PickCooldownUntil) return;
	PickCooldownUntil = Now + 1.0f;
	// Better than the lock's level: easier. Always at least a one-in-three chance.
	const float Chance = FMath::Clamp(0.35f + 0.06f * (Lv - Need), 0.35f, 0.95f);
	if (FMath::FRand() < Chance)
	{
		Hut->Unlock();
		Skills->AddXP(ESkillType::Lockpicking, 15 + 6 * Need);
		if (GEngine) JeebMsg::Post(3.f, FColor::Green, TEXT("Click. The lock gives way."));
	}
	else
	{
		Skills->AddXP(ESkillType::Lockpicking, 3);
		if (GEngine) JeebMsg::Post(2.f, FColor::Orange, TEXT("The pick slips. Try again."));
	}
}

void AKnightCharacter::CallHorse()
{
	if (IsUIOpen() || bMounted) return;
	AHorse* Best = nullptr;
	for (TActorIterator<AHorse> It(GetWorld()); It; ++It) { if (!It->HasRider()) { Best = *It; break; } }
	if (!Best) return;
	Best->CallTo(this);
	if (GEngine) JeebMsg::Post(2.5f, FColor::Yellow, TEXT("You whistle. Your horse comes galloping."));
}

void AKnightCharacter::Mount(AHorse* H)
{
	if (!H || bMounted || bSitting || bClimbing) return;
	bMounted = true;
	MyHorse = H;
	H->SetRider(this);
	bCrouched = false;
	bBlocking = false;
	bAttacking = false;
	HorseEnergy = 100.f;
	bHorseTired = false;
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->MaxWalkSpeed = WalkSpeed * 2.f;
	bSavedOrientMount = GetCharacterMovement()->bOrientRotationToMovement;
	GetCharacterMovement()->bOrientRotationToMovement = false;   // the horse faces where it steers, not where it is pushed
}

void AKnightCharacter::Dismount()
{
	if (!bMounted) return;
	bMounted = false;
	GetCharacterMovement()->bOrientRotationToMovement = bSavedOrientMount;
	if (AHorse* H = MyHorse.Get()) H->SetRider(nullptr);
	// Hop off to the right of the horse
	SetActorLocation(GetActorLocation() + GetActorRightVector() * 190.f + FVector(0.f, 0.f, 20.f));
	GetCharacterMovement()->SetMovementMode(MOVE_Falling);
}

void AKnightCharacter::SitOnSeat(int32 SeatIdx)
{
	const JeebTerrain::FSeat* Best = &JeebTerrain::Seats()[SeatIdx];

	UCharacterMovementComponent* Move = GetCharacterMovement();
	bCrouched = false;
	bBlocking = false;
	Move->StopMovementImmediately();
	Move->DisableMovement();
	const float Ground = JeebTerrain::HeightAt(Best->Pos.X, Best->Pos.Y);
	SeatForward = FRotator(0.f, Best->Yaw, 0.f).Vector();
	SetActorLocationAndRotation(FVector(Best->Pos.X, Best->Pos.Y, Ground + 79.f), FRotator(0.f, Best->Yaw, 0.f));
	bSitting = true;
}

void AKnightCharacter::StandUp()
{
	if (!bSitting) return;
	bSitting = false;
	// Step forward off the bench and drop back to the ground
	SetActorLocation(GetActorLocation() + SeatForward * 85.f + FVector(0.f, 0.f, 25.f));
	GetCharacterMovement()->SetMovementMode(MOVE_Falling);
}

void AKnightCharacter::Jump()
{
		if (bClimbing)
	{
		if (ClimbT > 0.6f) ExitClimb(false, false);   // letting go takes a deliberate press, not a stray one
		return;
	}
	if (bSitting)
	{
		StandUp();
		return;
	}
	if (bSliding)
	{
		bSliding = false;
		GetCharacterMovement()->bOrientRotationToMovement = bSavedOrient;
	}
	Super::Jump();
}

void AKnightCharacter::SpawnDust(const FVector& Pos, float Size)
{
	if (Dust.Num() == 0) return;
	FDust& D = Dust[DustNext];
	DustNext = (DustNext + 1) % Dust.Num();
	D.Pos = Pos;
	D.Age = 0.f;
	D.Life = FMath::FRandRange(0.4f, 0.6f);
	D.Size = Size * 0.45f; // small, subtle puffs
}

void AKnightCharacter::RefreshUIMode()
{
	const bool bOpen = IsUIOpen() || bPanelCursor;   // the side panel's cursor works like an open screen for the mouse
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		PC->SetShowMouseCursor(bOpen);
		PC->ResetIgnoreLookInput();
		if (bOpen) PC->SetIgnoreLookInput(true);
		if (bOpen)
		{
			FInputModeGameAndUI Mode;
			Mode.SetHideCursorDuringCapture(false);
			PC->SetInputMode(Mode);
		}
		else
		{
			PC->SetInputMode(FInputModeGameOnly());
		}
	}
}

void AKnightCharacter::ToggleSettings()
{
	bSettingsOpen = !bSettingsOpen;
	if (bSettingsOpen) { bInventoryOpen = false; bBankOpen = false; bSkillsOpen = false; bShopOpen = false; bSmithOpen = false; CloseDialogue(); }
	RefreshUIMode();
	if (!bSettingsOpen && GConfig)
	{
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}

void AKnightCharacter::ToggleSkills()
{
	if (bSettingsOpen || bShopOpen || bSmithOpen || bDialogueOpen || bBankOpen) return;
	bInventoryOpen = false;
	bSkillsOpen = !bSkillsOpen;
	GetCharacterMovement()->StopMovementImmediately();
	RefreshUIMode();
}

void AKnightCharacter::ShowPackTab(bool bSkills)
{
	if (!bInventoryOpen && !bSkillsOpen) return;
	bInventoryOpen = !bSkills;
	bSkillsOpen = bSkills;
	RefreshUIMode();
}

void AKnightCharacter::ToggleInventory()
{
	if (bSettingsOpen) return;
	if (bSkillsOpen) { bSkillsOpen = false; RefreshUIMode(); return; }
	if (bDialogueOpen) { CloseDialogue(); return; }
	if (bShopOpen) { CloseShop(); return; }
	if (bSmithOpen) { CloseSmith(); return; }
	if (bBankOpen) { CloseBank(); return; }
	bInventoryOpen = !bInventoryOpen;
	RefreshUIMode();
}

void AKnightCharacter::OpenShop()
{
	if (bSettingsOpen || bShopOpen) return;
	bInventoryOpen = false;
	bSmithOpen = false;
	bShopOpen = true;
	GetCharacterMovement()->StopMovementImmediately();
	RefreshUIMode();
}

void AKnightCharacter::CloseShop()
{
	bShopOpen = false;
	if (ANpc* N = TradeNpc.Get()) N->SetTalking(false);
	RefreshUIMode();
}

void AKnightCharacter::OpenBank()
{
	if (bSettingsOpen || bBankOpen) return;
	bInventoryOpen = false;
	bShopOpen = false;
	bSmithOpen = false;
	bSkillsOpen = false;
	bBankOpen = true;
	GetCharacterMovement()->StopMovementImmediately();
	RefreshUIMode();
}

void AKnightCharacter::CloseBank()
{
	bBankOpen = false;
	if (ANpc* N = TradeNpc.Get()) N->SetTalking(false);
	RefreshUIMode();
}

void AKnightCharacter::OpenSmith()
{
	if (bSettingsOpen || bSmithOpen) return;
	bInventoryOpen = false;
	bShopOpen = false;
	bSmithOpen = true;
	GetCharacterMovement()->StopMovementImmediately();
	RefreshUIMode();
}

void AKnightCharacter::CloseSmith()
{
	bSmithOpen = false;
	if (ANpc* N = TradeNpc.Get()) N->SetTalking(false);
	RefreshUIMode();
}

void AKnightCharacter::StartDialogue(ANpc* Npc)
{
	if (!Npc || IsUIOpen()) return;
	TradeNpc = Npc;
	DialogueLines = Npc->MakeConversation();
	DialogueIdx = 0;
	bDialogueOpen = true;
	bCrouched = false;
	bBlocking = false;
	Npc->SetTalking(true);
	GetCharacterMovement()->StopMovementImmediately();
	// Turn to face them
	const FVector To = Npc->GetActorLocation() - GetActorLocation();
	SetActorRotation(FRotator(0.f, FVector(To.X, To.Y, 0.f).Rotation().Yaw, 0.f));
	RefreshUIMode();
}

void AKnightCharacter::AdvanceDialogue()
{
	if (!bDialogueOpen) return;
	++DialogueIdx;
	if (DialogueIdx < DialogueLines.Num()) return;

	// The conversation is over: on to the trade screen if they have one
	bDialogueOpen = false;
	ANpc* Npc = TradeNpc.Get();
	if (Npc && Npc->OffersTrade())
	{
		if (Npc->GetRole() == ENpcRole::Blacksmith) OpenSmith();
		else if (Npc->GetRole() == ENpcRole::Banker) OpenBank();
		else OpenShop();
	}
	else
	{
		if (Npc) Npc->SetTalking(false);
		RefreshUIMode();
	}
}

void AKnightCharacter::CloseDialogue()
{
	if (!bDialogueOpen) return;
	bDialogueOpen = false;
	if (ANpc* N = TradeNpc.Get()) N->SetTalking(false);
	RefreshUIMode();
}

bool AKnightCharacter::GetDialogue(FString& OutName, FString& OutLine, bool& bOutLast) const
{
	if (!bDialogueOpen || !DialogueLines.IsValidIndex(DialogueIdx)) return false;
	const ANpc* N = TradeNpc.Get();
	OutName = N ? N->GetNpcName() : FString();
	OutLine = DialogueLines[DialogueIdx];
	bOutLast = (DialogueIdx == DialogueLines.Num() - 1);
	return true;
}

void AKnightCharacter::AddControllerYawInput(float Val)
{
	Super::AddControllerYawInput(Val * LookSensitivity);
}

void AKnightCharacter::AddControllerPitchInput(float Val)
{
	Super::AddControllerPitchInput(Val * LookSensitivity);
}

void AKnightCharacter::ApplyGraphics()
{
	if (!GEngine || !GetWorld()) return;
	UWorld* W = GetWorld();
	// 1 = Lumen (pretty, heavy); 0 = off, with simple sky ambient light (much faster)
	GEngine->Exec(W, *FString::Printf(TEXT("r.DynamicGlobalIlluminationMethod %d"), bFancyLighting ? 1 : 0));
	GEngine->Exec(W, *FString::Printf(TEXT("r.ReflectionMethod %d"), bFancyLighting ? 1 : 2));
	GEngine->Exec(W, TEXT("r.VolumetricCloud 1"));   // the sky's clouds are always on (drawn at half resolution, so they stay affordable)
	GEngine->Exec(W, *FString::Printf(TEXT("r.VolumetricFog %d"), bFancyLighting ? 1 : 0));
	GEngine->Exec(W, *FString::Printf(TEXT("r.ScreenPercentage %d"), FMath::RoundToInt(RenderScale * 100.f)));
}

void AKnightCharacter::SetFancyLighting(bool bOn)
{
	bFancyLighting = bOn;
	if (GConfig) GConfig->SetBool(TEXT("Jeebsland"), TEXT("FancyLighting"), bOn, GGameUserSettingsIni);
	ApplyGraphics();
}

void AKnightCharacter::SetRenderScale(float Value)
{
	const float New = FMath::Clamp(Value, 0.5f, 1.0f);
	if (FMath::IsNearlyEqual(New, RenderScale, 0.005f)) return;
	RenderScale = New;
	if (GConfig) GConfig->SetFloat(TEXT("Jeebsland"), TEXT("RenderScale"), RenderScale, GGameUserSettingsIni);
	ApplyGraphics();
}

void AKnightCharacter::SetLookSensitivity(float Value)
{
	LookSensitivity = FMath::Clamp(Value, 0.05f, 2.0f);
	if (GConfig)
	{
		GConfig->SetFloat(TEXT("Jeebsland"), TEXT("LookSensitivity"), LookSensitivity, GGameUserSettingsIni);
	}
}

void AKnightCharacter::BeginGathering(AResourceNode* Node, ESkillType Skill)
{
	ActiveNode = Node;
	GatherStart = GetActorLocation();
	if (Skill == ESkillType::Fishing)
	{
		// Face the water and cast: no swinging, just the pole held out
		const FVector To = Node->GetActorLocation() - GetActorLocation();
		SetActorRotation(FRotator(0.f, FVector(To.X, To.Y, 0.f).Rotation().Yaw, 0.f));
		bFishing = true;
		bSwinging = false;
		return;
	}
	bFishing = false;
	bSwinging = true;
	SwingTime = 0.f;

	if (Skill == ESkillType::Mining)
	{
		// Pickaxe: big overhead swings, two in a 2-second mine
		bSideways = false;
		SwingPeriod = 1.0f;
		WindupAngle = 150.f;
		StrikeAngle = 30.f;
	}
	else
	{
		// Axe: the arm is held out in front and sweeps sideways, right to left, about four chops in 5 seconds
		bSideways = true;
		SwingPeriod = 1.25f;
		WindupAngle = 85.f;    // arm pulled out to the right
		StrikeAngle = -40.f;   // arm swept across the front
	}
}

void AKnightCharacter::EndGathering()
{
	ActiveNode.Reset();
	bSwinging = false;
	bFishing = false;
}

void AKnightCharacter::Tick(float DeltaSeconds)
{
	// The camera can't swing all the way overhead or underfoot: from looking 60 degrees down to 35 degrees up
	if (!bPitchLimitsSet)
	{
		if (APlayerController* PCtl = Cast<APlayerController>(GetController()))
		{
			if (PCtl->PlayerCameraManager)
			{
				PCtl->PlayerCameraManager->ViewPitchMin = -60.f;
				PCtl->PlayerCameraManager->ViewPitchMax = 35.f;
				bPitchLimitsSet = true;
			}
		}
	}

	if (bShopOpen || bSmithOpen || bDialogueOpen || bBankOpen)
	{
		// Walking (or being pushed) away from whoever you are trading with ends the conversation
		const ANpc* N = TradeNpc.Get();
		if (!N || FVector::Dist2D(GetActorLocation(), N->GetActorLocation()) > 650.f)
		{
			CloseDialogue();
			if (bShopOpen) CloseShop();
			if (bSmithOpen) CloseSmith();
			if (bBankOpen) CloseBank();
		}
	}

	Super::Tick(DeltaSeconds);

	// Working continues while you move; it only stops if you drift about a body length away
	// from where you started, or turn so you are no longer facing the tree or rock
	if (AResourceNode* Node = ActiveNode.Get())
	{
				const bool bTooFar = FVector::Dist2D(GetActorLocation(), GatherStart) > (bFishing ? 320.f : 200.f);
		const FVector ToNode = Node->GetActorLocation() - GetActorLocation();
		const bool bLookedAway = !bFishing && FVector::DotProduct(GetActorForwardVector().GetSafeNormal2D(), ToNode.GetSafeNormal2D()) < 0.34f; // beyond about 70 degrees
		if (bTooFar || bLookedAway)
		{
			Node->CancelGathering();
		}
	}

	const float Dt = FMath::Max(DeltaSeconds, 0.0001f);
	const FVector Vel = GetVelocity();
	const float Speed = Vel.Size2D();
	const bool bFalling = GetCharacterMovement()->IsFalling();

	// ---------- Ladders ----------
	if (bClimbing) UpdateClimb(Dt); else TryStartClimb();

	// ---------- Special moves: roll, slide ----------
	UCharacterMovementComponent* Move = GetCharacterMovement();
	SlideCooldown = FMath::Max(0.f, SlideCooldown - Dt);
	RollCooldown = FMath::Max(0.f, RollCooldown - Dt);
	float SpecialSpeed = 0.f;

	if (bRolling)
	{
		RollT += Dt;
		const float K = FMath::Clamp(RollT / RollDur, 0.f, 1.f);
		SpecialSpeed = FMath::Lerp(900.f, 380.f, K * K);
		Move->Velocity.X = RollDir.X * SpecialSpeed;
		Move->Velocity.Y = RollDir.Y * SpecialSpeed;
		if (RollT >= RollDur || bFalling)
		{
			bRolling = false;
			RollCooldown = 1.0f;   // one second before the next roll (every kind of roll)
			Move->bOrientRotationToMovement = bSavedOrient;
		}
	}
	if (bSliding)
	{
		SlideT += Dt;
		// Downhill keeps the slide going; flat ground slows it
		float Downhill = 0.f;
		if (Move->CurrentFloor.IsWalkableFloor())
		{
			Downhill = FMath::Max(0.f, FVector::DotProduct(Move->CurrentFloor.HitResult.ImpactNormal, SlideDir));
		}
		SlideSpeed = FMath::Clamp(SlideSpeed + (-650.f + 2600.f * Downhill) * Dt, 0.f, 1100.f);
		SpecialSpeed = SlideSpeed + 50.f;
		Move->Velocity.X = SlideDir.X * SlideSpeed;
		Move->Velocity.Y = SlideDir.Y * SlideSpeed;
		if (SlideSpeed < 190.f || SlideT > 3.f || AirAlpha > 0.8f)
		{
			bSliding = false;
			SlideCooldown = 0.9f;
			Move->bOrientRotationToMovement = bSavedOrient;
		}
	}
	const bool bSpecial = bRolling || bSliding;

	if (bAttacking)
	{
		AttackT += Dt;
		const float HitFrac = (AtkStyle == EAtkStyle::Slash) ? 0.43f : (AtkStyle == EAtkStyle::Thrust ? 0.5f : 0.3f);
		if (!bAttackHit && AttackT >= AttackDur * HitFrac)
		{
			bAttackHit = true;
			AttackLands();
		}
		if (AttackT >= AttackDur)
		{
			bAttacking = false;
			const float Now = GetWorld()->GetTimeSeconds();
			LastSwingEnd = Now;
			LastSwingDir = AttackDir;
			LastStyle = AtkStyle;
			// Clicked during the swing (within 0.3 s of it ending): go straight into the left-to-right follow-up
			if (AtkStyle == EAtkStyle::Slash && AttackDir > 0 && Now - QueuedClick <= 0.3f && !IsUIOpen())
			{
				StartAttack(EAtkStyle::Slash, -1, 0.f);
			}
		}
	}

	// Spear: charging builds up while the button is held
	if (bCharging)
	{
		ChargeT += Dt;
		if (IsUIOpen() || bRolling || bSliding || bClimbing || bSitting || SelectedKind() != JeebItems::K_Spear) bCharging = false;
	}

	// The horse heals the same way: 10 s after its last hit, a full bar takes 10 s. (Not in PvP areas, once those exist.)
	if (HorseHealth < HorseMaxHealth && GetWorld()->GetTimeSeconds() - LastHorseHurtTime > 10.f)
		HorseHealth = FMath::Min(HorseMaxHealth, HorseHealth + HorseMaxHealth * 0.1f * Dt);
	// Health: your maximum grows with Defence and the Vitality amulet; you heal when left alone for 10 s
	if (Skills)
	{
		int32 NewMax = 100 + (Skills->GetLevel(ESkillType::Defense) - 1) * 3 + JeebItems::CombinedMods(Skills).MaxHealthBonus;
		if (GetWorld()->GetTimeSeconds() < VigourUntil) NewMax = FMath::RoundToInt(NewMax * 1.5f);   // vigour potion: a bigger bar for a minute
		if (NewMax != MaxHealth)
		{
			if (NewMax > MaxHealth) Health += NewMax - MaxHealth;   // a bigger bar comes with the extra already full
			else Health = FMath::Clamp(Health, 1.f, (float)NewMax);   // a smaller one only trims what does not fit
			MaxHealth = NewMax;
		}
		if (Health < MaxHealth && GetWorld()->GetTimeSeconds() - LastHurtTime > 10.f)   // 10 s after the last hit it starts to heal
		{
			Health = FMath::Min((float)MaxHealth, Health + MaxHealth * 0.1f * Dt);   // a full bar takes 10 s
		}
	}

	// ---------- Sprint stamina ----------
	const bool bWantSprint = !bMounted && bSprinting && !bCrouched && !bBlocking && !bExhausted;
	const bool bDrain = bWantSprint && Speed > 330.f && !bFalling && !bSpecial;
	if (bDrain)
	{
		Stamina = FMath::Max(0.f, Stamina - 16.f * Dt / GetStaminaMult());   // better boots: it lasts longer...
		StaminaDelay = 1.0f;
		if (Stamina <= 0.f) bExhausted = true;
	}
	else
	{
		StaminaDelay = FMath::Max(0.f, StaminaDelay - Dt);
		if (StaminaDelay <= 0.f) Stamina = FMath::Min(MaxStamina, Stamina + 16.f * Dt * GetStaminaMult());   // ...and comes back faster
	}
	if (bExhausted && Stamina >= 4.f) bExhausted = false;   // sprint again as soon as a little has come back

	// An open sack closes when you walk away from it, or put the cursor away
	if (LootTarget.IsValid() && (!bPanelCursor || FVector::Dist2D(GetActorLocation(), LootTarget->GetActorLocation()) > 400.f)) LootTarget = nullptr;

	// Climbing, riding or sitting: the shield and the weapon go on the back; they come back to the hands afterwards
	{
		const bool bWantStow = bClimbing || bMounted || bSitting;
		if (bWantStow != bStowed)
		{
			bStowed = bWantStow;
			PlaceShield(bStowed);
			RebuildHeld();
			if (bStowed) bBlocking = false;
		}
	}

	// Shield guard: its size is the shield's Defence; a new or changed shield starts full. It recovers like health:
	// 10 s after the last blow it took, a full bar in 10 s. A broken guard is mended only once it is full again.
	{
		const int32 ShieldId = (Skills && ShieldActive()) ? Skills->Worn[JeebItems::K_Shield] : 0;
		const float NewMax = ShieldId ? float(JeebItems::ShieldGuard(ShieldId)) : 0.f;
		const float RecoverDelay = ShieldId ? JeebItems::ShieldRecoverDelay(JeebItems::StyleOf(ShieldId)) : 10.f;   // the round shield recovers sooner
		if (!FMath::IsNearlyEqual(NewMax, GuardMax)) { GuardMax = NewMax; GuardPool = NewMax; bGuardBroken = false; }
		if (GuardMax > 0.f && GuardPool < GuardMax && GetWorld()->GetTimeSeconds() - LastGuardHitTime > RecoverDelay)
		{
			GuardPool = FMath::Min(GuardMax, GuardPool + GuardMax * 0.1f * Dt);
			if (GuardPool >= GuardMax && bGuardBroken)
			{
				bGuardBroken = false;
				JeebMsg::Post(2.5f, FColor(140, 200, 255), TEXT("Your shield guard is back to full."));
			}
		}
	}

	// The shield and the sword never work together: it is down during a swing, and comes back up once the swing is over if the button is still held
	if (bAttacking) bBlocking = false;
	else if (bBlockHeld && !bBlocking && ShieldActive() && !bRolling && !bSliding && !bClimbing && !bSitting && !bMounted && !IsUIOpen()) bBlocking = true;

	// ---------- Ground speed (crouching and blocking are slow); slower in the air with a smooth slide down to it ----------
	const float AirMul = 0.69f;
	// Riding: twice the walking speed, and twice the sprint. The horse sprints for about four seconds, then needs
	// its 100-point bar to refill completely before the next gallop.
	bool bHorseSprint = false;
	if (bMounted)
	{
		const bool bAsk = bSprinting && Speed > 200.f && !bHorseTired && !bFalling && (GetWorld()->GetTimeSeconds() - LastBackTime > 0.15);
		if (bAsk && HorseEnergy > 0.f)
		{
			// the horse never tires: its gallop energy does not drain
			bHorseSprint = true;
			if (HorseEnergy <= 0.f) bHorseTired = true;
		}
		else
		{
			HorseEnergy = FMath::Min(100.f, HorseEnergy + (bHorseTired ? 10.f : 16.f) * Dt);
			if (bHorseTired && HorseEnergy >= 4.f) bHorseTired = false;
		}
	}
	// The horse is 20% slower than before, and builds up to a gallop: it does not jump to full speed when you sprint
	float HorseTarget = 0.8f * (bHorseSprint ? 2.f * SprintSpeed : 2.f * WalkSpeed);
	if (!bMounted) HorseSpeedNow = 0.f;
	else
	{
		if (HorseSpeedNow < 1.f) HorseSpeedNow = 0.8f * 2.f * WalkSpeed;
		HorseSpeedNow = FMath::FInterpConstantTo(HorseSpeedNow, HorseTarget, Dt, HorseTarget > HorseSpeedNow ? 330.f : 900.f);   // speeds up over about 1.7 s, eases off quicker
	}
	// The horse backs up slowly with S (never sprinting). On foot, a raised shield is a combat stance: you face where the camera
	// points and sidestep or back up instead of turning round, a little slower than walking (forward 60%, sidestep 50%, back 40%).
	const bool bBackStep = bMounted && (GetWorld()->GetTimeSeconds() - LastBackTime < 0.15);
	const bool bStance = bBlockHeld && !bMounted && !bRolling && !bSliding && !bClimbing && !bSitting;
	if (bStance && !bBackOrient) { bBackOrient = true; bBackSavedOrient = Move->bOrientRotationToMovement; Move->bOrientRotationToMovement = false; }
	else if (!bStance && bBackOrient && !bRolling && !bSliding) { bBackOrient = false; Move->bOrientRotationToMovement = bBackSavedOrient; }
	if (bStance && GetController())   // turn to the camera quickly but smoothly (about 0.15 s for a quarter turn)
		SetActorRotation(FMath::RInterpConstantTo(GetActorRotation(), FRotator(0.f, GetController()->GetControlRotation().Yaw, 0.f), Dt, 640.f));
	const float StanceFactor = StanceFwd < -0.1f ? 0.4f : (StanceFwd > 0.1f ? 0.6f : (FMath::Abs(StanceRight) > 0.1f ? 0.5f : 0.6f));
	const float ShieldMove = (Skills && Skills->Worn[JeebItems::K_Shield]) ? JeebItems::ShieldMoveMul(JeebItems::StyleOf(Skills->Worn[JeebItems::K_Shield])) : 1.f;   // round: quicker, tower: slower
	const float BaseSpeed = bBackStep ? 0.5f * WalkSpeed : bMounted ? HorseSpeedNow
	                                 : (bStance ? WalkSpeed * StanceFactor * ShieldMove : ((bCrouched || bBlocking) ? CrouchSpeed : (bWantSprint ? SprintSpeed : WalkSpeed))) * ((!bMounted && GetWorld()->GetTimeSeconds() < SpeedUntil) ? 1.5f : 1.f)   // swiftness potion
	                                 * ((!bMounted && Skills) ? JeebItems::CombinedMods(Skills).SpeedMul : 1.f);   // the Red Helm Brush
	if (bFalling && !bSpecial)
	{
		AirSpeedMul = AirMul;
		const float AirCap = BaseSpeed * AirMul;
		FVector Horizontal(Move->Velocity.X, Move->Velocity.Y, 0.f);
		const float HSpeed = Horizontal.Size();
		if (HSpeed > AirCap)
		{
			const float NewSpeed = FMath::FInterpConstantTo(HSpeed, AirCap, Dt, 1500.f); // about 0.2 s to go from a sprint to the air speed
			Horizontal *= NewSpeed / HSpeed;
			Move->Velocity.X = Horizontal.X;
			Move->Velocity.Y = Horizontal.Y;
		}
	}
	else if (!bFalling)
	{
		AirSpeedMul = FMath::FInterpTo(AirSpeedMul, 1.f, Dt, 6.f); // speeding back up after landing, as before
	}
	Move->MaxWalkSpeed = bSpecial ? SpecialSpeed : BaseSpeed * AirSpeedMul;

	// ---------- Landing detection ----------
	if (bFalling)
	{
		if (!bWasFalling) LastFallVz = 0.f;
		LastFallVz = FMath::Min(LastFallVz, Vel.Z);
	}
	else if (bWasFalling)
	{
		LandImpact = FMath::Clamp(-LastFallVz / 900.f, 0.3f, 1.f); // harder landings squash more
		LandTimer = LandDuration;
	}
	bWasFalling = bFalling;
	LandTimer = FMath::Max(0.f, LandTimer - Dt);
	const float Squash = LandImpact * (LandTimer / LandDuration);

	// ---------- Blends (everything eases in and out) ----------
	MoveBlend = FMath::FInterpTo(MoveBlend, FMath::Clamp(Speed / 150.f, 0.f, 1.f), Dt, 10.f);
	RunAlpha  = FMath::FInterpTo(RunAlpha, FMath::Clamp((Speed - 250.f) / 500.f, 0.f, 1.f), Dt, 6.f); // 0 = walk, 1 = full sprint
	AirAlpha  = FMath::FInterpTo(AirAlpha, bFalling ? 1.f : 0.f, Dt, 14.f);
	CrouchAlpha = FMath::FInterpTo(CrouchAlpha, bCrouched ? 1.f : 0.f, Dt, 10.f);
	BlockAlpha  = FMath::FInterpTo(BlockAlpha, bBlocking ? 1.f : 0.f, Dt, 14.f);
	SlideAlpha  = FMath::FInterpTo(SlideAlpha, bSliding ? 1.f : 0.f, Dt, 12.f);
	SitAlpha    = FMath::FInterpTo(SitAlpha, bSitting ? 1.f : 0.f, Dt, 8.f);
	MountAlpha  = FMath::FInterpTo(MountAlpha, bMounted ? 1.f : 0.f, Dt, 8.f);
	TuckAlpha   = FMath::FInterpTo(TuckAlpha, bRolling ? 1.f : 0.f, Dt, 18.f);
	ClimbAlpha  = FMath::FInterpTo(ClimbAlpha, bClimbing ? 1.f : 0.f, Dt, 12.f);
	const float CS = FMath::Sin(ClimbPhase);
	const float Air = AirAlpha;
	const float Ground = 1.f - Air;
	const float Duck = FMath::Max(CrouchAlpha, 0.8f * BlockAlpha) * Ground; // how low the knight is sitting
	const float Stream = RunAlpha * MoveBlend; // how hard the wind pushes the cape and plume

	// ---------- Gait clock: faster strides when running ----------
	const float Hz = FMath::Lerp(1.3f, 2.1f, RunAlpha) * FMath::Lerp(1.f, 0.7f, CrouchAlpha); // crouch-walking is about 40% slower
	GaitPhase = FMath::Fmod(GaitPhase + 2.f * PI * Hz * Dt, 2.f * PI);
	IdleTime += Dt;
	FlutterTime += Dt * (1.f + 1.5f * Stream);
	const float S = FMath::Sin(GaitPhase); // right leg forward when positive
	const float C = FMath::Cos(GaitPhase);

	// ---------- Strength of each motion ----------
	const float StepScale  = 1.f - 0.4f * CrouchAlpha; // smaller steps when crouched
	const float HipAmp     = FMath::Lerp(32.f, 80.f, RunAlpha) * MoveBlend * StepScale;
	const float KneeAmp    = FMath::Lerp(40.f, 105.f, RunAlpha) * MoveBlend * StepScale;
	const float ArmAmp     = FMath::Lerp(22.f, 55.f, RunAlpha) * MoveBlend;
	const float ElbowBase  = FMath::Lerp(12.f, 70.f, RunAlpha) * MoveBlend;
	const float ElbowSwing = FMath::Lerp(8.f, 25.f, RunAlpha) * MoveBlend;
	const float Lean       = FMath::Lerp(3.f, 20.f, RunAlpha) * MoveBlend;
	const float Twist      = FMath::Lerp(5.f, 12.f, RunAlpha) * MoveBlend;

	// ---------- Jump pose: where are we in the arc? 0 = rising fast ... 1 = falling fast ----------
	const float F01 = FMath::Clamp(0.5f - Vel.Z / 1000.f, 0.f, 1.f);
	const float Asym = FMath::Clamp(Speed / 250.f, 0.f, 1.f); // running leap (one knee up) vs standing jump (both tucked)

	// ---------- Legs ----------
	// On the ground: legs swing opposite each other; the knee bends most as a leg passes under the body
	// (smooth everywhere: no sharp corners in the curve, so no jerks)
	const float SwingR = 0.1f + 0.9f * FMath::Pow(0.5f * (1.f + C), 2.5f) + 0.22f * RunAlpha * FMath::Square(0.5f * (1.f - C));
	const float SwingL = 0.1f + 0.9f * FMath::Pow(0.5f * (1.f - C), 2.5f) + 0.22f * RunAlpha * FMath::Square(0.5f * (1.f + C));
	// Shield-up stance: the legs step the way you are actually moving. Backing up runs the stride in reverse; a sidestep
	// swings the legs out to the side instead (the leading leg reaches out, the other closes up after it).
	{
		const FVector LV = GetActorRotation().UnrotateVector(FVector(Vel.X, Vel.Y, 0.f));
		float Fw = 1.f, Sd = 0.f;
		if (bBlockHeld && !bMounted && LV.Size() > 30.f) { Fw = LV.X / LV.Size(); Sd = LV.Y / LV.Size(); }
		StepFw = FMath::FInterpTo(StepFw, Fw, Dt, 8.f);
		StepSd = FMath::FInterpTo(StepSd, Sd, Dt, 8.f);
	}
	const float HipRg = HipAmp * S * StepFw;
	const float HipLg = -HipAmp * S * StepFw;
	const float SideAmp = 24.f * MoveBlend * Ground * (1.f - SlideAlpha) * (1.f - SitAlpha) * (1.f - MountAlpha) * (1.f - ClimbAlpha) * (1.f - TuckAlpha);
	const float HipRroll = SideAmp * StepSd * (0.5f + 0.5f * S);   // positive roll swings a hanging leg to the right
	const float HipLroll = SideAmp * StepSd * (0.5f - 0.5f * S);
	const float KneeRg = -KneeAmp * SwingR;
	const float KneeLg = -KneeAmp * SwingL;
	// In the air: knees tuck on the way up, legs reach forward on the way down
	const float HipRa  = FMath::Lerp(FMath::Lerp(30.f, 60.f, Asym), FMath::Lerp(15.f, 35.f, Asym), F01);
	const float HipLa  = FMath::Lerp(FMath::Lerp(30.f, -15.f, Asym), FMath::Lerp(15.f, 0.f, Asym), F01);
	const float KneeRa = -FMath::Lerp(FMath::Lerp(55.f, 85.f, Asym), 25.f, F01);
	const float KneeLa = -FMath::Lerp(FMath::Lerp(55.f, 40.f, Asym), 20.f, F01);

	float HipRf  = FMath::Lerp(HipRg, HipRa, Air) + 35.f * Squash + 55.f * Duck;
	float HipLf  = FMath::Lerp(HipLg, HipLa, Air) + 35.f * Squash + 55.f * Duck;
	float KneeRf = FMath::Lerp(KneeRg, KneeRa, Air) - 70.f * Squash - 95.f * Duck;
	float KneeLf = FMath::Lerp(KneeLg, KneeLa, Air) - 70.f * Squash - 95.f * Duck;
	// Standing still: slow breathing (the chest rises, the knees ease a little), like the idle in Breath of the Wild
	const float IdleAmt = (1.f - MoveBlend) * Ground * (1.f - SlideAlpha) * (1.f - SitAlpha) * (1.f - MountAlpha) * (1.f - ClimbAlpha) * (1.f - TuckAlpha);
	const float BreathS = FMath::Sin(IdleTime * 1.65f);   // one breath every 3.8 s
	const float Stance = StanceHip - 1.5f * BreathS * IdleAmt;
	// A relaxed stance: on the ground the knees are always a little bent (eased out as he crouches, which bends them anyway)
	const float Relax = Ground * (1.f - Duck);
	HipRf += Stance * Relax;   HipLf += Stance * Relax;
	KneeRf -= 2.f * Stance * Relax;   KneeLf -= 2.f * Stance * Relax;
	// Sliding: one leg stretched out in front, the other folded under
	HipRf = FMath::Lerp(HipRf, 78.f, SlideAlpha);   KneeRf = FMath::Lerp(KneeRf, -4.f, SlideAlpha);
	HipLf = FMath::Lerp(HipLf, 45.f, SlideAlpha);   KneeLf = FMath::Lerp(KneeLf, -115.f, SlideAlpha);
	// Sitting: thighs level, shins hanging down
	HipRf = FMath::Lerp(HipRf, 90.f, SitAlpha);     KneeRf = FMath::Lerp(KneeRf, -90.f, SitAlpha);
	HipLf = FMath::Lerp(HipLf, 90.f, SitAlpha);     KneeLf = FMath::Lerp(KneeLf, -90.f, SitAlpha);
	// Riding: thighs forward and a little apart, shins hanging beside the horse
	HipRf = FMath::Lerp(HipRf, 62.f, MountAlpha);   KneeRf = FMath::Lerp(KneeRf, -42.f, MountAlpha);
	HipLf = FMath::Lerp(HipLf, 62.f, MountAlpha);   KneeLf = FMath::Lerp(KneeLf, -42.f, MountAlpha);
	// Climbing: legs step up the rungs alternately
	HipRf = FMath::Lerp(HipRf, 55.f + 45.f * CS, ClimbAlpha);   KneeRf = FMath::Lerp(KneeRf, -(65.f + 35.f * CS), ClimbAlpha);
	HipLf = FMath::Lerp(HipLf, 55.f - 45.f * CS, ClimbAlpha);   KneeLf = FMath::Lerp(KneeLf, -(65.f - 35.f * CS), ClimbAlpha);
	// Rolling: tucked into a ball
	HipRf = FMath::Lerp(HipRf, 105.f, TuckAlpha);   KneeRf = FMath::Lerp(KneeRf, -125.f, TuckAlpha);
	HipLf = FMath::Lerp(HipLf, 105.f, TuckAlpha);   KneeLf = FMath::Lerp(KneeLf, -125.f, TuckAlpha);

	HipR->SetRelativeRotation(FRotator(HipRf, 0.f, HipRroll));
	HipL->SetRelativeRotation(FRotator(HipLf, 0.f, HipLroll));
	KneeR->SetRelativeRotation(FRotator(KneeRf, 0.f, 0.f));
	KneeL->SetRelativeRotation(FRotator(KneeLf, 0.f, 0.f));
	// Feet stay flat on the ground (a running stride lets them tip a little); in the air, sitting, riding, climbing,
	// sliding and rolling they follow the shins as before
	const float Level = Ground * (1.f - SlideAlpha) * (1.f - SitAlpha) * (1.f - MountAlpha) * (1.f - ClimbAlpha) * (1.f - TuckAlpha) * (1.f - 0.4f * RunAlpha * MoveBlend);
	AnkleR->SetRelativeRotation(FRotator(-(HipRf + KneeRf) * Level, 0.f, 0.f));
	AnkleL->SetRelativeRotation(FRotator(-(HipLf + KneeLf) * Level, 0.f, 0.f));

	// ---------- Body bounce ----------
	// The hips dip as the legs spread (so planted feet stay on the ground) and rise over each step
	// Walking: the hips dip when the legs spread. Running: the body is lowest mid-stance and floats up
	// while the legs are spread, so there is a brief moment with both feet off the ground.
	const float LegLen = 46.5f;   // hip to ankle (the feet stay flat, so the ankle is what rises and falls)
	const float WalkBounce = -LegLen * (1.f - FMath::Cos(FMath::DegreesToRadians(HipAmp * S)));
	const float RunBounce  = -0.75f * LegLen * (1.f - FMath::Cos(FMath::DegreesToRadians(HipAmp * S))) + 6.f * S * S;
	const float Bounce = FMath::Lerp(WalkBounce, RunBounce, RunAlpha);
	// Bent knees (relaxed stance, crouching, guarding, landing): the body sinks exactly as far as the legs fold, so the
	// flat feet stay on the ground (thigh 23 cm, shin 23.5 cm)
	const float PoseH = 35.f * Squash + 55.f * Duck + Stance * Relax;
	const float PoseK = -70.f * Squash - 95.f * Duck - 2.f * Stance * Relax;
	const float LegDrop = 23.f * (1.f - FMath::Cos(FMath::DegreesToRadians(PoseH))) + 23.5f * (1.f - FMath::Cos(FMath::DegreesToRadians(PoseH + PoseK)));
	float BodyZ = FMath::Lerp(Bounce, 0.f, Air) - LegDrop;
	BodyZ = FMath::Lerp(BodyZ, -42.f, SlideAlpha);
	BodyZ = FMath::Lerp(BodyZ, 0.f, SitAlpha);
	BodyZ = FMath::Lerp(BodyZ, 98.f, MountAlpha);                  // up onto the saddle
	BodyZ = FMath::Lerp(BodyZ, -45.f, TuckAlpha);
	SmoothBodyZ = FMath::FInterpTo(SmoothBodyZ, BodyZ, Dt, 22.f); // soften any remaining steps
	BodyRoot->SetRelativeLocation(FVector(0.f, 0.f, SmoothBodyZ));

	// ---------- Chest: lean into the run, twist against the hips, lean into turns ----------
	const float NewYaw = GetActorRotation().Yaw;
	const float TurnRate = FMath::Clamp(FMath::FindDeltaAngleDegrees(PrevYaw, NewYaw) / Dt, -720.f, 720.f);
	PrevYaw = NewYaw;
	YawRate = FMath::FInterpTo(YawRate, TurnRate, Dt, 8.f);
	const float TurnRoll = FMath::Clamp(YawRate * 0.025f, -12.f, 12.f) * Stream;

	const float AirLean = FMath::Lerp(4.f, -10.f, F01); // slight arch back while rising, forward while falling
	// at ease the chest leans a little forward (not standing against a wall) and lifts with each breath
	float UpperPitch = FMath::Lerp(-Lean - RestLean * Ground, AirLean, Air) - 12.f * Squash - 14.f * Duck + 0.9f * BreathS * IdleAmt;
	UpperPitch = FMath::Lerp(UpperPitch, 28.f, SlideAlpha);  // lean back into the slide
	UpperPitch = FMath::Lerp(UpperPitch, 0.f, SitAlpha);
	UpperPitch = FMath::Lerp(UpperPitch, -6.f * RunAlpha, MountAlpha);   // a little forward in a gallop
	UpperPitch = FMath::Lerp(UpperPitch, -45.f, TuckAlpha);
	const float Sway = 0.9f * FMath::Sin(IdleTime * 0.62f) * IdleAmt;   // a slow shift of weight from side to side while standing
	UpperPivot->SetRelativeRotation(FRotator(UpperPitch, Twist * S * Ground, TurnRoll + Sway));

	// Chest bob: the upper body sinks as each foot takes weight and springs back up between steps.
	// (Only the chest moves, so the feet never sink into the ground.) In the air it stretches on the way up.
	const float BobAmp = FMath::Lerp(4.f, 16.f, RunAlpha) * MoveBlend;
	const float ChestTarget = (Ground * (-BobAmp * (1.f - S * S)) + Air * FMath::Lerp(5.f, -4.f, F01)) * (1.f - FMath::Max(SlideAlpha, SitAlpha)) + 0.6f * BreathS * IdleAmt;   // the chest rises as he breathes in
	SmoothChestZ = FMath::FInterpTo(SmoothChestZ, ChestTarget, Dt, 30.f);
	UpperPivot->SetRelativeLocation(UpperBase + FVector(0.f, 0.f, SmoothChestZ));

	// Pelvis sways with the steps: a little roll as weight shifts, and hips turning against the shoulders
	float FlipPitch = 0.f;
	if (bRolling)
	{
		const float K = FMath::Clamp(RollT / RollDur, 0.f, 1.f);
		FlipPitch = -360.f * (K * K * (3.f - 2.f * K)); // one forward somersault
	}
	BodyRoot->SetRelativeRotation(FRotator(FlipPitch, -0.5f * Twist * S * Ground, 2.5f * MoveBlend * C * Ground));

	// The head counter-steers so the knight keeps looking ahead, with a small nod on each step
	const float Nod = 2.5f * FMath::Sin(2.f * GaitPhase) * MoveBlend * Ground;
	HeadPivot->SetRelativeRotation(FRotator(-0.6f * UpperPitch + Nod - 7.f * Ground, -0.7f * Twist * S * Ground, -0.5f * TurnRoll - 0.7f * Sway));

	// ---------- Cape and plume ----------
	const float FlapAmp = 3.f + 9.f * Stream;
	const float CapeUpperG = FMath::Lerp(-4.f, -28.f, Stream) + FlapAmp * FMath::Sin(FlutterTime * 3.1f);
	const float CapeLowerG = FMath::Lerp(-6.f, -38.f, Stream) + FlapAmp * 1.6f * FMath::Sin(FlutterTime * 5.3f + 1.2f);
	const float CapeUpperA = FMath::Lerp(-5.f, -40.f, F01);  // streams down while rising, flies up while falling
	const float CapeLowerA = FMath::Lerp(-8.f, -65.f, F01);
	CapeUpper->SetRelativeRotation(FRotator(
		FMath::Lerp(CapeUpperG, CapeUpperA, Air), 0.f,
		(2.f + 5.f * Stream) * FMath::Sin(FlutterTime * 2.3f) - 0.5f * TurnRoll));
	CapeLower->SetRelativeRotation(FRotator(
		FMath::Lerp(CapeLowerG, CapeLowerA, Air), 0.f,
		(3.f + 9.f * Stream) * FMath::Sin(FlutterTime * 3.7f + 0.8f) - 0.8f * TurnRoll));

	const float PlumeG = FMath::Lerp(0.f, 20.f, Stream) + (3.f + 5.f * Stream) * FMath::Sin(FlutterTime * 4.1f + 0.5f);
	const float PlumeA = FMath::Lerp(20.f, -15.f, F01);
	PlumePivot->SetRelativeRotation(FRotator(FMath::Lerp(PlumeG, PlumeA, Air), 0.f, 4.f * Stream * FMath::Sin(FlutterTime * 3.3f)));
	TickCape(Dt);

	// ---------- Arms ----------
	const float ArmSway = 1.5f * FMath::Sin(IdleTime * 1.7f) * (1.f - MoveBlend);
	const float ArmAirPitch = FMath::Lerp(100.f, 25.f, F01);   // arms thrown up on the way up, then out
	const float ArmAirSpread = FMath::Lerp(20.f, 50.f, F01);

	// Left arm (shield): swings opposite the left leg; raised in front of the chest when blocking
	float PitchL = FMath::Lerp(0.6f * ArmAmp * S + ArmSway, ArmAirPitch, Air) + 25.f * Squash + 4.f * Ground;
	float RollL = Air * ArmAirSpread * 0.7f + 1.2f * BreathS * IdleAmt;
	float ElbowLdeg = FMath::Lerp(ElbowBase + ElbowSwing * FMath::Max(S, 0.f), 15.f, Air) + 15.f * Ground;   // the shield arm rests with the elbow a little bent forward
	PitchL = FMath::Lerp(PitchL, 38.f, BlockAlpha);
	ElbowLdeg = FMath::Lerp(ElbowLdeg, 55.f, BlockAlpha);
	PitchL = FMath::Lerp(PitchL, -40.f, SlideAlpha);   RollL = FMath::Lerp(RollL, 30.f, SlideAlpha);
	PitchL = FMath::Lerp(PitchL, 32.f, SitAlpha);      ElbowLdeg = FMath::Lerp(ElbowLdeg, 70.f, SitAlpha);
	PitchL = FMath::Lerp(PitchL, 38.f, MountAlpha);    ElbowLdeg = FMath::Lerp(ElbowLdeg, 55.f, MountAlpha);    // holding the reins
	PitchL = FMath::Lerp(PitchL, 85.f, TuckAlpha);     ElbowLdeg = FMath::Lerp(ElbowLdeg, 100.f, TuckAlpha);
	PitchL = FMath::Lerp(PitchL, 140.f + 35.f * CS, ClimbAlpha);  ElbowLdeg = FMath::Lerp(ElbowLdeg, 25.f, ClimbAlpha);
	ArmPivotL->SetRelativeLocation(LeftBase + BlockAlpha * FVector(22.f, 30.f, 10.f)); // shield comes across in front of the body
	ArmPivotL->SetRelativeRotation(FRotator(PitchL, 0.f, RollL));
	ElbowL->SetRelativeRotation(FRotator(ElbowLdeg, 0.f, 0.f));
	HoldShield();

	// Right arm (sword): swings opposite the right leg, unless it is busy mining or chopping
	float TargetPitch = FMath::Lerp(-0.22f * ArmAmp * S - ArmSway, ArmAirPitch, Air) + 25.f * Squash + 4.f * Ground;   // arms hang a touch forward (relaxed shoulders)
	float TargetYaw = 0.f;
	float RollR = -Air * ArmAirSpread - 1.2f * BreathS * IdleAmt;   // the arms ease out a little with each breath
	float ElbowRdeg = FMath::Lerp(HeldElbow + 0.25f * ElbowBase, 40.f, Air);
	float ArmSpeed = 25.f;

	TargetPitch = FMath::Lerp(TargetPitch, -35.f, SlideAlpha);   RollR = FMath::Lerp(RollR, -30.f, SlideAlpha);
	TargetPitch = FMath::Lerp(TargetPitch, 32.f, SitAlpha);      ElbowRdeg = FMath::Lerp(ElbowRdeg, 70.f, SitAlpha);
	TargetPitch = FMath::Lerp(TargetPitch, 38.f, MountAlpha);    ElbowRdeg = FMath::Lerp(ElbowRdeg, 55.f, MountAlpha);
	TargetPitch = FMath::Lerp(TargetPitch, 85.f, TuckAlpha);     ElbowRdeg = FMath::Lerp(ElbowRdeg, 100.f, TuckAlpha);
	TargetPitch = FMath::Lerp(TargetPitch, 140.f - 35.f * CS, ClimbAlpha);  ElbowRdeg = FMath::Lerp(ElbowRdeg, 25.f, ClimbAlpha);

	if (bCharging && !bAttacking && !bSwinging)
	{
		// Spear drawn back, the stronger the pull the further back
		const float K = FMath::Clamp(ChargeT / 1.0f, 0.f, 1.f);
		TargetPitch = FMath::Lerp(70.f, 45.f, K);
		TargetYaw = FMath::Lerp(0.f, 18.f, K);
		ElbowRdeg = FMath::Lerp(60.f, 110.f, K);
		RollR = 0.f;
		ArmSpeed = 20.f;
	}

	if (bAttacking && !bSwinging)
	{
		const float T = AttackT / AttackDur;
		if (AtkStyle == EAtkStyle::Thrust)
		{
			// Pull back, then drive the arm straight out
			if (T < 0.35f)
			{
				const float A = T / 0.35f;
				TargetPitch = FMath::Lerp(55.f, 45.f, A);  TargetYaw = FMath::Lerp(10.f, 18.f, A);  ElbowRdeg = FMath::Lerp(90.f, 115.f, A);
			}
			else
			{
				const float A = FMath::Clamp((T - 0.35f) / 0.25f, 0.f, 1.f);
				TargetPitch = FMath::Lerp(45.f, 88.f, A);  TargetYaw = FMath::Lerp(18.f, 0.f, A);  ElbowRdeg = FMath::Lerp(115.f, 4.f, A);
			}
		}
		else if (AtkStyle == EAtkStyle::Cast)
		{
			// Raise the arm and point
			const float A = FMath::Clamp(T / 0.3f, 0.f, 1.f);
			const float E = A * A * (3.f - 2.f * A);
			TargetPitch = FMath::Lerp(20.f, 92.f, E);  TargetYaw = 0.f;  ElbowRdeg = FMath::Lerp(50.f, 8.f, E);
		}
		else if (AttackDir > 0)
		{
						// First swing, right to left: the arm comes up to shoulder height and swings out to the right,
			// then the whole arm sweeps flat across the body
			if (T < 0.35f)
			{
				const float A = T / 0.35f;
				const float E = A * A * (3.f - 2.f * A);
				TargetPitch = FMath::Lerp(10.f, 78.f, E);
				TargetYaw = FMath::Lerp(0.f, 80.f, E);
				ElbowRdeg = FMath::Lerp(ElbowRdeg, 65.f, E);
			}
			else if (T < 0.55f)
			{
				const float A = (T - 0.35f) / 0.2f;
				TargetPitch = FMath::Lerp(78.f, 84.f, A);
				TargetYaw = FMath::Lerp(80.f, -70.f, A * A);
				ElbowRdeg = FMath::Lerp(65.f, 5.f, A);
			}
			else { TargetPitch = 84.f; TargetYaw = -70.f; ElbowRdeg = 5.f; }
		}
		else
		{
						// Follow-up, left to right: starts where the first one ended, draws back across the body, whips flat back out
			if (T < 0.35f)
			{
				const float A = T / 0.35f;
				const float E = A * A * (3.f - 2.f * A);
				TargetPitch = FMath::Lerp(84.f, 80.f, E);
				TargetYaw = FMath::Lerp(-70.f, -88.f, E);
				ElbowRdeg = FMath::Lerp(5.f, 70.f, E);
			}
			else if (T < 0.55f)
			{
				const float A = (T - 0.35f) / 0.2f;
				TargetPitch = FMath::Lerp(80.f, 84.f, A);
				TargetYaw = FMath::Lerp(-88.f, 75.f, A * A);
				ElbowRdeg = FMath::Lerp(70.f, 5.f, A);
			}
			else { TargetPitch = 84.f; TargetYaw = 75.f; ElbowRdeg = 5.f; }
		}
		RollR = 0.f;
		ArmSpeed = 38.f;
	}

	if (bFishing)
	{
		// Pole held out over the water, with a slow bob
		TargetPitch = 66.f + 3.f * FMath::Sin(IdleTime * 2.f);
		TargetYaw = 0.f;
		RollR = 0.f;
		ElbowRdeg = 22.f;
		ArmSpeed = 14.f;
	}

	if (bSwinging)
	{
		SwingTime += Dt;
		const float T = FMath::Fmod(SwingTime, SwingPeriod) / SwingPeriod;

		if (T < 0.55f)
		{
			// Wind up: raise the arm smoothly
			const float A = T / 0.55f;
			TargetPitch = FMath::Lerp(StrikeAngle, WindupAngle, A * A * (3.f - 2.f * A));
		}
		else if (T < 0.7f)
		{
			// Strike: fast swing down
			const float A = (T - 0.55f) / 0.15f;
			TargetPitch = FMath::Lerp(WindupAngle, StrikeAngle, A * A);
		}
		else
		{
			// Brief pause on impact
			TargetPitch = StrikeAngle;
		}
		if (bSideways)
		{
			// The swing value drives a horizontal sweep, with the arm held out in front
			TargetYaw = TargetPitch;
			TargetPitch = 80.f;
		}
		RollR = 0.f;
		ElbowRdeg = 0.f;
		ArmSpeed = 30.f;
	}

	ArmPitch = FMath::FInterpTo(ArmPitch, TargetPitch, Dt, ArmSpeed);
	ArmYaw = FMath::FInterpTo(ArmYaw, TargetYaw, Dt, ArmSpeed);
	ArmPivot->SetRelativeRotation(FRotator(ArmPitch, ArmYaw, RollR));
	ElbowRCur = FMath::FInterpTo(ElbowRCur, ElbowRdeg, Dt, 18.f);
	ElbowR->SetRelativeRotation(FRotator(ElbowRCur, 0.f, 0.f));
	HoldWeaponAtEase(Dt, bAttacking || bCharging || bSwinging || bFishing || Air > 0.5f || FMath::Max(FMath::Max(SitAlpha, MountAlpha), FMath::Max(ClimbAlpha, FMath::Max(TuckAlpha, SlideAlpha))) > 0.5f);

	// ---------- Dust puffs: footfalls when running, landings, slides ----------
	const FVector FeetBase = GetActorLocation() + FVector(0.f, 0.f, -90.f);
	if (!bFalling && !bSpecial && MoveBlend > 0.6f && RunAlpha > 0.3f && ((C > 0.f) != (PrevC > 0.f)))
	{
		const float Side = (S > 0.f) ? 13.f : -13.f;
		SpawnDust(FeetBase + GetActorForwardVector() * 12.f + GetActorRightVector() * Side, 26.f + 14.f * RunAlpha);
	}
	PrevC = C;
	if (!bFalling && bWasFalling == false && LandTimer > LandDuration - Dt * 1.5f && LandImpact > 0.45f)
	{
		SpawnDust(FeetBase + GetActorRightVector() * 25.f, 50.f * LandImpact + 20.f);
		SpawnDust(FeetBase - GetActorRightVector() * 25.f, 50.f * LandImpact + 20.f);
	}
	if (bSliding)
	{
		SlideDustTimer += Dt;
		if (SlideDustTimer > 0.06f)
		{
			SlideDustTimer = 0.f;
			SpawnDust(FeetBase - SlideDir * 30.f + GetActorRightVector() * FMath::FRandRange(-20.f, 20.f), FMath::FRandRange(32.f, 52.f));
		}
	}
	for (int32 i = 0; i < Dust.Num(); ++i)
	{
		FDust& D = Dust[i];
		D.Age += Dt;
		FTransform T(FQuat::Identity, FVector(0.f, 0.f, -9000.f), FVector(0.001f));
		if (D.Age < D.Life)
		{
			const float K = D.Age / D.Life;
			const float Sz = D.Size * (0.4f + 0.9f * FMath::Sqrt(K)) * (1.f - K * K) / 100.f;
			T = FTransform(FQuat::Identity, D.Pos + FVector(0.f, 0.f, 30.f * K), FVector(FMath::Max(Sz, 0.001f)));
		}
		DustISM->UpdateInstanceTransform(i, T, true, i == Dust.Num() - 1, true);
	}
}

void AKnightCharacter::HurtHorse(float Amount)
{
	HorseHealth = FMath::Clamp(HorseHealth - Amount, 0.f, HorseMaxHealth);
	LastHorseHurtTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
}

void AKnightCharacter::DoMove(float Right, float Forward)
{
	const double Now = GetWorld()->GetTimeSeconds();
	if (!bMounted)
	{
		// Shield up: a combat stance. Move relative to the camera (which is where you face), so A and D sidestep and S backs up.
		if (bBlockHeld && !bRolling && !bSliding && !bClimbing && !bSitting && GetController())
		{
			const FRotator Yaw(0.f, GetController()->GetControlRotation().Yaw, 0.f);
			AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::X), Forward);
			AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y), Right);
			StanceFwd = Forward;
			StanceRight = Right;
			return;
		}
		Super::DoMove(Right, Forward);
		return;
	}
	// Like a car in a game: the horse goes forward and turns on a tight circle (a turning radius of about 1.75 m). The faster
	// it goes, the quicker it turns, and standing still it can still swing round. S backs it up slowly (it never sprints backwards).
	const float Dt = GetWorld()->GetDeltaSeconds();
	const float Speed = GetVelocity().Size2D();
	const float TurnRate = FMath::Clamp(FMath::RadiansToDegrees(Speed / 175.f), 70.f, 230.f);   // degrees per second
	if (Forward > 0.1f && GetController())
	{
		// Moving forward, the horse follows the camera like the knight does on foot: turn the mouse and it swings round that way
		// (A and D bend the direction further left or right of where the camera points), at its tight turning rate.
		const FRotator Cam(0.f, GetController()->GetControlRotation().Yaw, 0.f);
		const FVector Want = FRotationMatrix(Cam).GetUnitAxis(EAxis::X) * Forward + FRotationMatrix(Cam).GetUnitAxis(EAxis::Y) * Right;
		const float Cur = GetActorRotation().Yaw;
		const float Delta = FMath::FindDeltaAngleDegrees(Cur, Want.Rotation().Yaw);
		SetActorRotation(FRotator(0.f, Cur + FMath::Clamp(Delta, -TurnRate * Dt, TurnRate * Dt), 0.f));
	}
	else if (!FMath::IsNearlyZero(Right))
		SetActorRotation(FRotator(0.f, GetActorRotation().Yaw + Right * TurnRate * Dt, 0.f));
	if (Forward < -0.1f)
	{
		LastBackTime = Now;
		AddMovementInput(-GetActorForwardVector(), 1.f);
	}
	else AddMovementInput(GetActorForwardVector(), FMath::Max(Forward, 0.f));
}

void AKnightCharacter::OnInventoryKey()
{
	// A screen is open (shop, smithy, bank, a conversation, the skills board): Tab closes it, as it always has
	if (IsUIOpen())
	{
		if (!bSettingsOpen) ToggleInventory();
		return;
	}
	// Otherwise Tab frees the mouse cursor to use the side panel, or puts it away again
	bPanelCursor = !bPanelCursor;
	RefreshUIMode();
}

void AKnightCharacter::SetPanelAlways(bool bOn)
{
	bPanelAlways = bOn;
	if (GConfig) GConfig->SetBool(TEXT("Jeebsland"), TEXT("PanelAlways"), bPanelAlways, GGameUserSettingsIni);
}

// A movement key (A, D, W or S) tapped twice within a quarter second, always (shield or not): a roll that way, relative to the
// camera. Any other movement key pressed in between breaks the pair, so "A D A D" (strafing back and forth) never rolls.
void AKnightCharacter::TapKey(int32 Key)
{
	const double Now = GetWorld()->GetTimeSeconds();
	const bool bDouble = (Key == LastTapKey) && (Now - LastTapTime) < 0.28;
	LastTapKey = Key;
	LastTapTime = Now;
	if (!bDouble || bMounted || IsUIOpen() || !GetController()) return;
	const FRotator Yaw(0.f, GetController()->GetControlRotation().Yaw, 0.f);
	const FVector Fwd = FRotationMatrix(Yaw).GetUnitAxis(EAxis::X), Right = FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y);
	PendingRollDir = Key == 0 ? -Right : (Key == 1 ? Right : (Key == 2 ? Fwd : -Fwd));
	LastTapKey = -1;   // a third tap does not start another
	StartRoll();
	PendingRollDir = FVector::ZeroVector;
}

// ---------------- Dropping things on the ground ----------------
void AKnightCharacter::DropBag(const TArray<FInvSlot>& Items, int32 Ingots)
{
	const FVector Where = GetActorLocation() + GetActorForwardVector() * 130.f;
	const FVector Ground(Where.X, Where.Y, JeebTerrain::HeightAt(Where.X, Where.Y));
	if (ALootDrop* Drop = GetWorld()->SpawnActor<ALootDrop>(Ground, FRotator::ZeroRotator)) Drop->Setup(Items.Num() <= 1 ? ELootLook::Item : ELootLook::Pack, Items, Ingots);   // one thing: show the thing itself
}

void AKnightCharacter::DropFromPack(int32 SlotIdx, int32 Count)
{
	if (!Skills || !Skills->Slots.IsValidIndex(SlotIdx)) return;
	FInvSlot& S = Skills->Slots[SlotIdx];
	if (S.Id == 0 || S.Count <= 0) return;
	const int32 N = FMath::Clamp(Count, 1, S.Count);
	TArray<FInvSlot> Items;
	FInvSlot D; D.Id = S.Id; D.Count = N;
	Items.Add(D);
	S.Count -= N;
	if (S.Count <= 0) { S.Id = 0; S.Count = 0; }
	DropBag(Items, 0);
	RefreshEquipment();
}

void AKnightCharacter::DropIngots(int32 Count)
{
	if (!Skills) return;
	const int32 N = FMath::Clamp(Count, 0, Skills->GetIngots());
	if (N <= 0 || !Skills->SpendIngots(N)) return;
	DropBag(TArray<FInvSlot>(), N);
}

void AKnightCharacter::DropFromHotbar(int32 H)
{
	if (!Skills || H < 0 || H >= 5 || Skills->Hotbar[H] == 0) return;
	TArray<FInvSlot> Items;
	FInvSlot D; D.Id = Skills->Hotbar[H]; D.Count = 1;
	Items.Add(D);
	Skills->Hotbar[H] = 0;
	DropBag(Items, 0);
	RefreshEquipment();
}

// ---------------- Dying ----------------
// Whatever killed you (a blow, the poison gas, anything), everything you own goes into a big sack where you fell: your pack,
// your weapon bar, your armour and the rest of what you wore, your potions and your ingots. You wake up in Blackrock, just
// inside the east gate, in the starting kit, at full health. Skills and levels stay with you.
void AKnightCharacter::Die()
{
	if (!Skills) return;
	if (bMounted) Dismount();
	if (bClimbing) ExitClimb(false, false);
	if (bSitting) StandUp();
	const FVector Fell = GetActorLocation();
	TArray<FInvSlot> Items;
	TArray<uint8> Where;
	int32 Ingots = 0;
	Skills->TakeEverything(Items, Where, Ingots);
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (ALootDrop* Sack = GetWorld()->SpawnActor<ALootDrop>(FVector(Fell.X, Fell.Y, JeebTerrain::HeightAt(Fell.X, Fell.Y) + 2.f), FRotator::ZeroRotator, SP))
		Sack->SetupSack(Items, Where, Ingots);

	Skills->ResetToStartingKit();
	RefreshEquipment();
	CloseLoot();
	bAttacking = false; bCharging = false; bBlocking = false; bBlockHeld = false;
	SpeedUntil = -1.f; VigourUntil = -1.f;
	GetCharacterMovement()->Velocity = FVector::ZeroVector;
	const FVector2D VC = JeebTerrain::VillageCenter();
	const FVector2D Wake(VC.X + JeebTerrain::VillageHalf() - 450.f, VC.Y);
	SetActorLocation(FVector(Wake.X, Wake.Y, JeebTerrain::HeightAt(Wake.X, Wake.Y) + 100.f));
	SetActorRotation(FRotator(0.f, 180.f, 0.f));
	MaxHealth = 100 + (Skills->GetLevel(ESkillType::Defense) - 1) * 3;
	Health = (float)MaxHealth;
	LastHurtTime = -100.f;
	JeebMsg::Post(5.f, FColor::Red, TEXT("You fell. Everything you had is in a sack where you fell. You wake up in Blackrock."));
}

// ---------------- Potions ----------------
float AKnightCharacter::GetPotionCooldownLeft(int32 Type) const
{
	if (Type < 0 || Type >= JeebItems::NumPotions) return 0.f;
	return FMath::Max(0.f, PotionReadyAt[Type] - GetWorld()->GetTimeSeconds());
}

void AKnightCharacter::DrinkPotion()
{
	if (!Skills || IsUIOpen()) return;
	FInvSlot& P = Skills->PotionSlot;
	if (P.Id == 0 || !JeebItems::IsPotion(P.Id) || P.Count <= 0) { JeebMsg::Post(2.f, FColor::Orange, TEXT("No potion in the potion slot.")); return; }
	const int32 T = JeebItems::PotionType(P.Id);
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now < PotionReadyAt[T])
	{
		JeebMsg::Post(2.f, FColor::Orange, FString::Printf(TEXT("%s is not ready yet (%d s)."), JeebItems::PotionName(T), FMath::CeilToInt(PotionReadyAt[T] - Now)));
		return;
	}
	switch (T)
	{
	case JeebItems::P_Speed:  SpeedUntil = Now + 8.f; break;
	case JeebItems::P_Heal:   Health = (float)MaxHealth; break;
	case JeebItems::P_Vigour: VigourUntil = Now + 60.f; break;   // the bigger bar is worked out in Tick (the extra comes full)
	}
	PotionReadyAt[T] = Now + JeebItems::PotionCooldown(T);
	if (--P.Count <= 0) P = FInvSlot();
	JeebMsg::Post(2.5f, FColor(170, 255, 170), FString::Printf(TEXT("You drink a %s."), JeebItems::PotionName(T)));
}

// ---------------- Looting a sack ----------------
void AKnightCharacter::OpenLoot(ALootDrop* Drop)
{
	if (!Drop) return;
	LootTarget = Drop;
	SetPanelCursor(true);   // the cursor comes out, like Tab, so you can take things
}

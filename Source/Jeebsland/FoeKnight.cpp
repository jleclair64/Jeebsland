#include "FoeKnight.h"
#include "KnightDesign.h"
#include "WeaponDesign.h"
#include "KnightCharacter.h"
#include "HealthComponent.h"
#include "Combat.h"
#include "Loot.h"
#include "Terrain.h"
#include "Messages.h"
#include "ProceduralMeshComponent.h"
#include "KnightModel.h"
#include "Components/CapsuleComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "EngineUtils.h"

AFoeKnight::AFoeKnight()
{
	PrimaryActorTick.bCanEverTick = true;
	Rand.Initialize(4242);
	bReplicates = true;
	SetReplicateMovement(true);
	SetNetUpdateFrequency(30.f);

	// The root is a capsule the size of the player's, so you bump into him; his feet are 96 below its centre
	Capsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Capsule"));
	Capsule->InitCapsuleSize(34.f, 96.f);
	Capsule->SetCollisionProfileName(TEXT("Pawn"));
	Capsule->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	SetRootComponent(Capsule);

	// The same skeleton as the player's knight (positions relative to the capsule centre)
	TMap<USceneComponent*, FVector> Placed;
	auto MakePivot = [&](const TCHAR* Name, USceneComponent* Parent, FVector CapsulePos) -> USceneComponent*
	{
		USceneComponent* P = CreateDefaultSubobject<USceneComponent>(Name);
		P->SetupAttachment(Parent ? Parent : RootComponent.Get());
		P->SetRelativeLocation(CapsulePos - (Parent ? Placed.FindChecked(Parent) : FVector::ZeroVector));
		Placed.Add(P, CapsulePos);
		return P;
	};
	using namespace KnightDesign;
	BodyRoot   = MakePivot(TEXT("BodyRoot"),   nullptr,    FVector(0, 0, 0));
	HipL       = MakePivot(TEXT("HipL"),       BodyRoot,   PivotPos(P_HipL));
	HipR       = MakePivot(TEXT("HipR"),       BodyRoot,   PivotPos(P_HipR));
	KneeL      = MakePivot(TEXT("KneeL"),      HipL,       PivotPos(P_KneeL));
	KneeR      = MakePivot(TEXT("KneeR"),      HipR,       PivotPos(P_KneeR));
	AnkleL     = MakePivot(TEXT("AnkleL"),     KneeL,      PivotPos(P_AnkleL));
	AnkleR     = MakePivot(TEXT("AnkleR"),     KneeR,      PivotPos(P_AnkleR));
	UpperPivot = MakePivot(TEXT("UpperPivot"), BodyRoot,   FVector(0, 0, -41.f));
	HeadPivot  = MakePivot(TEXT("HeadPivot"),  UpperPivot, FVector(0, 0, 23.f));
	PlumePivot = MakePivot(TEXT("PlumePivot"), HeadPivot,  FVector(-2, 0, 71.f));
	// (the helm is modelled at its full size: no head scaling)
	CapeUpper  = MakePivot(TEXT("CapeUpper"),  UpperPivot, FVector(-26, 0, 13.f));
	CapeLower  = MakePivot(TEXT("CapeLower"),  CapeUpper,  FVector(-26, 0, -19.f));
	ArmR       = MakePivot(TEXT("ArmR"),       UpperPivot, PivotPos(P_ArmR));
	ArmL       = MakePivot(TEXT("ArmL"),       UpperPivot, PivotPos(P_ArmL));
	ElbowR     = MakePivot(TEXT("ElbowR"),     ArmR,       PivotPos(P_ElbowR));
	WristR     = MakePivot(TEXT("WristR"),     ElbowR,     PivotPos(P_WristR));
	ElbowL     = MakePivot(TEXT("ElbowL"),     ArmL,       PivotPos(P_ElbowL));

	Hp = CreateDefaultSubobject<UHealthComponent>(TEXT("Hp"));
	Hp->Max = Hp->Cur = MaxHp;
	Hp->BarHeight = 130.f;
	Hp->HitHeight = 10.f;
	Hp->HitRadius = 60.f;
}

void AFoeKnight::Setup(const FVector2D& InPenCenter, const FVector2D& InPenHalf)
{
	PenCenter = InPenCenter;
	PenHalf = InPenHalf;
	WanderTo = PenCenter;
}

UProceduralMeshComponent* AFoeKnight::MakePart(USceneComponent* Attach, int32 PivotId, const KM::FMD& InMd)
{
	KM::FMD Shrunk;   // the feet are drawn smaller (KnightDesign::FootScale)
	if (KnightDesign::IsFootPivot(PivotId)) { Shrunk = InMd; for (FVector& P : Shrunk.V) P = KnightDesign::FootVertex(PivotId, P); }
	const KM::FMD& Md = KnightDesign::IsFootPivot(PivotId) ? Shrunk : InMd;
	UProceduralMeshComponent* C = NewObject<UProceduralMeshComponent>(this);
	C->SetupAttachment(Attach);
	C->SetRelativeLocation(-KnightDesign::PivotPos(PivotId));   // shapes are modelled around the body's centre
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->RegisterComponent();
	TArray<FVector> Normals;
	KM::ComputeNormals(Md, Normals);
	TArray<int32> Tris;
	Tris.Reserve(Md.I.Num() * 2);
	for (int32 t = 0; t + 2 < Md.I.Num(); t += 3)
	{
		Tris.Add(Md.I[t]); Tris.Add(Md.I[t + 1]); Tris.Add(Md.I[t + 2]);
		Tris.Add(Md.I[t]); Tris.Add(Md.I[t + 2]); Tris.Add(Md.I[t + 1]);
	}
	C->CreateMeshSection_LinearColor(0, Md.V, Tris, Normals, TArray<FVector2D>(), TArray<FLinearColor>(), TArray<FProcMeshTangent>(), false);
	Meshes.Add(C);
	return C;
}

void AFoeKnight::Paint(UProceduralMeshComponent* C, const FLinearColor& Color, bool bMetal)
{
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"));
	UMaterialInterface* Metal = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Metal.M_Metal"));
	if (UMaterialInstanceDynamic* M = C->CreateDynamicMaterialInstance(0, (bMetal && Metal) ? Metal : Base))
	{
		M->SetVectorParameterValue(TEXT("Color"), Color);
		if (bMetal && Metal) { M->SetScalarParameterValue(TEXT("Metallic"), 1.f); M->SetScalarParameterValue(TEXT("Roughness"), 0.3f); }
	}
}

void AFoeKnight::BeginPlay()
{
	Super::BeginPlay();
	using namespace KnightDesign;
	USceneComponent* Pivots[P_Count] = {};
	Pivots[P_Body] = BodyRoot;   Pivots[P_Upper] = UpperPivot; Pivots[P_Head] = HeadPivot; Pivots[P_Plume] = PlumePivot;
	Pivots[P_CapeU] = CapeUpper; Pivots[P_CapeL] = CapeLower;  Pivots[P_ArmL] = ArmL;      Pivots[P_ArmR] = ArmR;
	Pivots[P_ElbowL] = ElbowL;   Pivots[P_ElbowR] = ElbowR;    Pivots[P_HipL] = HipL;      Pivots[P_HipR] = HipR;
	Pivots[P_KneeL] = KneeL;     Pivots[P_KneeR] = KneeR;
	Pivots[P_AnkleL] = AnkleL;   Pivots[P_AnkleR] = AnkleR;   Pivots[P_WristR] = WristR;

	// A full suit of iron (dark grey plate, red accents), shield included; no bare skin
	TArray<FDef> Defs;
	Build(Defs);
	// the armour from the Meshy model when it is there (the same as the player's), tinted for his tier
	const TArray<KnightModel::FPart>& Model = KnightModel::Parts();
	for (const KnightModel::FPart& MP : Model)
	{
		if (!Pivots[MP.Pivot]) continue;
		UProceduralMeshComponent* C = KnightModel::MakeComp(this, Pivots[MP.Pivot], MP);
		Meshes.Add(C);
		KnightModel::Paint(C, Tier);
	}
	if (Model.Num() > 0)   // the dark body inside the armour
	{
		TArray<FDef> Cores;
		KnightModel::AddCores(Cores);
		for (const FDef& D : Cores)
			if (Pivots[D.Pivot]) Paint(MakePart(Pivots[D.Pivot], D.Pivot, D.Mesh), D.Fixed, false);
	}
	for (const FDef& D : Defs)
	{
		if (D.Mesh.V.Num() == 0 || !Pivots[D.Pivot] || D.bBare || D.Style != 0 || D.Slot >= SlotBrush) continue;   // (he carries the kite shield, and no brush)
		if (Model.Num() > 0 && KnightModel::IsModelSlot(D.Slot)) continue;
		if (Tier < D.MinTier || Tier > D.MaxTier) continue;
		UProceduralMeshComponent* C = MakePart(Pivots[D.Pivot], D.Pivot, D.Mesh);
		if (D.Slot == SlotShield) ShieldMeshes.Add(C);   // (arrows stick in these)
		FLinearColor Col = JeebItems::Palette(Tier, static_cast<int32>(D.Role));
		Col.R *= D.Shade; Col.G *= D.Shade; Col.B *= D.Shade;
		if (D.bFixedColor) Col = D.Fixed;
		const bool bMetal = !D.bFixedColor && (D.Role == EArmorRole::Plate || D.Role == EArmorRole::Trim || D.Role == EArmorRole::BootBody);
		Paint(C, Col, bMetal);
	}
	// His sword, in the right hand, held the way the player holds one
	{
		TArray<WeaponDesign::FWPart> Parts;
		WeaponDesign::Build(JeebItems::K_Sword, Parts, KM::Xf(GripPos(false), KM::DownTo(BladeDir(1))));
		for (const WeaponDesign::FWPart& WP : Parts)
		{
			if (WP.Mesh.V.Num() == 0) continue;
			UProceduralMeshComponent* C = MakePart(ElbowR, P_ElbowR, WP.Mesh);
			const bool bMetal = !WP.bFixed && (WP.Role == 0 || WP.Role == 2);
			Paint(C, WP.bFixed ? WP.Fixed : JeebItems::Palette(Tier, WP.Role), bMetal);
		}
	}

	Hp->OnDamaged = [this](AActor* From) { OnHurt(From); };
	Hp->OnHitSeen = [this]() { HitFlash = 0.35f; };
	// His hurt boxes follow his joints (HitBoxes.h)
	Hp->GatherHurt = [this](TArray<JeebHit::FCapsule>& Out)
	{
		JeebHit::FKnightJoints J;
		J.Upper = UpperPivot; J.Head = HeadPivot; J.ArmL = ArmL; J.ArmR = ArmR; J.ElbowL = ElbowL; J.ElbowR = ElbowR; J.WristR = WristR;
		J.HipL = HipL; J.HipR = HipR; J.KneeL = KneeL; J.KneeR = KneeR; J.AnkleL = AnkleL; J.AnkleR = AnkleR;
		JeebHit::KnightHurt(J, Out);
	};
	// Blows that don't come through hit boxes (arrows, other weapons' swings): his raised shield, facing the blow (its 90-degree
	// wedge), takes the whole of it
	Hp->ModifyDamage = [this](int32 Amount, AActor* From) -> int32
	{
		if (BlockAlpha < 0.6f || bGuardBroken || KnockT >= 0.f || !From) return Amount;
		const FVector To = (From->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
		if (FVector::DotProduct(GetActorForwardVector().GetSafeNormal2D(), To) < FMath::Cos(FMath::DegreesToRadians(JeebHit::FShieldWedge().HalfAngle))) return Amount;
		return TakeOnShield(Amount, From) ? 0 : Amount;
	};
	// His raised shield as hit boxes see it: the 90-degree wedge in front of him (HitBoxes.h). A sword's hit box that meets it
	// before his body is stopped there, and the shield takes all of it
	Hp->GatherShield = [this](JeebHit::FShieldWedge& Out)
	{
		if (BlockAlpha < 0.6f || DeathT > 0.f || bGuardBroken || KnockT >= 0.f) return false;
		Out = JeebHit::KnightShield(GetActorLocation(), GetActorForwardVector(), JeebHit::HelmTop(HeadPivot));   // (up to the top of his helm)
		return true;
	};
	GuardMax = GuardPool = JeebItems::GuardPool(JeebItems::ShieldId(0, Tier));   // (an iron kite shield)
	Hp->ShieldBlock = [this](int32 Amount, AActor* From) -> int32
	{
		TakeOnShield(Amount, From);
		return 0;
	};
	Hp->KnockDown = [this](AActor* From) { KnockDown(From); };
	Hp->ShieldParts = [this](TArray<UPrimitiveComponent*>& Out) { for (UProceduralMeshComponent* C : ShieldMeshes) Out.Add(C); };
	Hp->React = [this](AActor* From, const JeebHit::FReact& R) { OnReact(From, R); };
	Hp->IsSwinging = [this]() { return KnockT < 0.f && DeathT <= 0.f && (Act == EAct::Windup || Act == EAct::Strike); };   // (a kick into his swing loses)
	if (bStill) { Post = GetActorLocation(); PostYaw = GetActorRotation().Yaw; }
	LastLoc = GetActorLocation();
	if (HasAuthority() && !bStill) Rise();
}

void AFoeKnight::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AFoeKnight, RepAct);
	DOREPLIFETIME(AFoeKnight, bRepBlock);
	DOREPLIFETIME(AFoeKnight, bRepDown);
	DOREPLIFETIME(AFoeKnight, RepKnock);
}

bool AFoeKnight::TakeOnShield(int32 Amount, AActor* From)
{
	if (bGuardBroken || Amount <= 0) return false;
	const float Now = GetWorld()->GetTimeSeconds();
	GuardPool -= Amount;   // (all of it: none reaches him)
	GuardHitTime = Now;
	if (bStill) Hp->LastHitTime = Now;   // (the still knight's guard and health come back 3 s after the last blow)
	JeebCombat::AddNumber(GetWorld(), GetActorLocation() + FVector(0.f, 0.f, 150.f), Amount, JeebCombat::N_Blocked);
	HitFlash = 0.2f;
	if (GuardPool < 1.f)
	{
		GuardPool = 0.f;
		bGuardBroken = true;
		BlockT = -1.f;   // the shield drops
		bRepBlock = false;
		if (USkillComponent* Sk = From ? From->FindComponentByClass<USkillComponent>() : nullptr)
			Sk->Say(2.5f, FColor::Orange, TEXT("His guard is broken! His shield is down until it recovers."));
	}
	return true;
}

// (server) A kick: down on his back. How he gets up is picked at random but never the same way twice running, so a player
// has to read it (still knights too: practice for the sweep)
void AFoeKnight::KnockDown(AActor* From)
{
	if (!HasAuthority() || KnockT >= 0.f || DeathT > 0.f) return;
	if (BlockT > 0.f && !bGuardBroken && GuardMax > 0.f)   // a kick on his raised shield crushes half its guard
	{
		GuardPool -= 0.5f * GuardMax; GuardHitTime = GetWorld()->GetTimeSeconds();
		if (bStill) Hp->LastHitTime = GuardHitTime;
		if (GuardPool < 1.f) { GuardPool = 0.f; bGuardBroken = true; }
	}
	KnockOpt = uint8((KnockOpt + 1 + Rand.RandRange(0, 1)) % 3);
	++KnockCount;
	RepKnock = uint8(((KnockCount & 63) << 2) | KnockOpt);
	KnockT = 0.f;
	Act = EAct::Idle; ActT = 0.f;
	BlockT = -1.f; bRepBlock = false;
	RepAct = static_cast<uint8>(Act);
	AttackCool = FMath::Max(AttackCool, 1.f);
	// knocked back a little, away from the kicker
	const FVector Away = (GetActorLocation() - (From ? From->GetActorLocation() : GetActorLocation() + GetActorForwardVector())).GetSafeNormal2D();
	if (bStill) SetActorLocation(GetActorLocation() + Away * 50.f, true);
	else MoveTowards(FVector2D(GetActorLocation()) + FVector2D(Away.X, Away.Y) * 50.f, 1.0e5f, 1.f);
}

// (server) Hit by a low sweep: up into the air and a little back, bent by his DI (a random direction held, as a player might),
// landing on his back. Already lying down: he is scooped up off the ground and lies there again when he lands.
void AFoeKnight::Launch(AActor* From)
{
	if (!HasAuthority() || DeathT > 0.f) return;
	const bool bWasDown = KnockT >= 0.f;
	if (!bAir) GroundZ = GetActorLocation().Z;
	KnockT = -1.f;
	KnockDown(From);
	if (bWasDown) KnockT = KnockFall;   // (stays flat)
	const FVector Away = (GetActorLocation() - (From ? From->GetActorLocation() : GetActorLocation() - GetActorForwardVector())).GetSafeNormal2D();
	FVector DI = FVector::ZeroVector;
	const int32 Pick = Rand.RandRange(0, 4);   // none, toward his left, his right, away, back in
	const FVector Side = FVector::CrossProduct(FVector::UpVector, Away);
	if (Pick == 1) DI = -Side; else if (Pick == 2) DI = Side; else if (Pick == 3) DI = Away; else if (Pick == 4) DI = -Away;
	AirVel = JeebHit::LaunchVelocity(Away, DI);
	bAir = true;
}

// (server) A clean hit: hitstun cuts his swing short (your attack beat his), with a push back; or the sweep's launch
void AFoeKnight::OnReact(AActor* From, const JeebHit::FReact& R)
{
	if (!HasAuthority() || DeathT > 0.f) return;
	if (R.bLaunch) { Launch(From); return; }
	if (KnockT >= 0.f || R.Stun <= 0.f) return;   // (lying down or flying: he stays down)
	const FVector Away = (GetActorLocation() - (From ? From->GetActorLocation() : GetActorLocation())).GetSafeNormal2D();
	const FVector By = Away * R.Push * 0.18f;   // (about how far the push carries a player)
	if (bStill) SetActorLocation(GetActorLocation() + By, true);
	else MoveTowards(FVector2D(GetActorLocation() + By), 1.0e5f, 1.f);
	if (bStill) return;   // (a training post: he never acts anyway)
	Act = EAct::Stun; ActT = 0.f; StunDur = R.Stun;
	BlockT = -1.f;
	RepAct = static_cast<uint8>(Act);
}

void AFoeKnight::OnRep_Knock()
{
	KnockOpt = RepKnock & 3;
	KnockT = (KnockT >= 0.f) ? KnockFall : 0.f;   // (already down, e.g. swept off the ground: he stays flat)
	GroundZ = GetActorLocation().Z;
	Act = EAct::Idle;
}

void AFoeKnight::TickKnock(float Dt)
{
	if (KnockT < 0.f) return;
	const float Before = KnockT;
	KnockT += Dt;
	if (HasAuthority() ? bAir : GetActorLocation().Z > GroundZ + 8.f) KnockT = FMath::Min(KnockT, KnockFall);   // (launched: lying back in the air; the rest waits until he lands)
	const float RollEnd = KnockLie + (KnockOpt ? KnockRoll : 0.f), End = RollEnd + KnockRise;
	// how far down (1 lying)
	const float Down = (KnockT < KnockFall) ? FMath::SmoothStep(0.f, 1.f, KnockT / KnockFall) : (KnockT < RollEnd ? 1.f : 1.f - FMath::SmoothStep(0.f, 1.f, (KnockT - RollEnd) / KnockRise));
	// rolling over sideways along the ground (his length lies along his facing), to his left or right
	const float Side = (KnockOpt == 1) ? -1.f : 1.f;
	float RollK = 0.f;
	if (KnockOpt && KnockT > KnockLie) RollK = FMath::SmoothStep(0.f, 1.f, FMath::Min(1.f, (KnockT - KnockLie) / KnockRoll));
	if (KnockOpt && HasAuthority() && Before < RollEnd && Before >= KnockLie)
	{
		const float K0 = FMath::SmoothStep(0.f, 1.f, FMath::Clamp((Before - KnockLie) / KnockRoll, 0.f, 1.f));
		const float K1 = FMath::SmoothStep(0.f, 1.f, FMath::Clamp((KnockT - KnockLie) / KnockRoll, 0.f, 1.f));
		const FVector By = GetActorRightVector().GetSafeNormal2D() * Side * KnockRollDist * (K1 - K0);
		if (bStill) SetActorLocation(GetActorLocation() + By, true);
		else MoveTowards(FVector2D(GetActorLocation() + By), 1.0e5f, 1.f);
	}
	if (KnockT >= End)
	{
		KnockT = -1.f;
		BodyRoot->SetRelativeRotation(FRotator::ZeroRotator);
		BodyRoot->SetRelativeLocation(FVector::ZeroVector);
		return;
	}
	// the pose: flat on his back (the top tipped back), legs out straight, arms flung wide
	const FQuat Lie = FQuat(FVector::ForwardVector, -Side * 2.f * PI * RollK) * FQuat(FRotator(80.f * Down, 0.f, 0.f));
	BodyRoot->SetRelativeRotation(Lie);
	BodyRoot->SetRelativeLocation(FVector(0.f, 0.f, -78.f * Down));
	auto Toward = [Down](USceneComponent* C, const FRotator& R) { C->SetRelativeRotation(FMath::Lerp(C->GetRelativeRotation(), R, Down)); };
	Toward(HipR, FRotator(8.f, 0.f, -6.f));   Toward(HipL, FRotator(14.f, 0.f, 6.f));
	Toward(KneeR, FRotator(-12.f, 0.f, 0.f)); Toward(KneeL, FRotator(-24.f, 0.f, 0.f));
	Toward(AnkleR, FRotator(20.f, 0.f, 0.f)); Toward(AnkleL, FRotator(20.f, 0.f, 0.f));
	Toward(ArmR, FRotator(15.f, 0.f, -55.f)); Toward(ArmL, FRotator(15.f, 0.f, 55.f));
	Toward(ElbowR, FRotator(20.f, 0.f, 0.f)); Toward(ElbowL, FRotator(20.f, 0.f, 0.f));
	Toward(HeadPivot, FRotator(-28.f, 0.f, 0.f));
}

void AFoeKnight::OnRep_Act()
{
	Act = static_cast<EAct>(RepAct);
	ActT = 0.f;
}

void AFoeKnight::OnRep_Down()
{
	if (bRepDown)
	{
		if (DeathT <= 0.f) DeathT = 0.001f;   // topple
		KnockT = -1.f;
		Act = EAct::Idle;
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		return;
	}
	// back on his feet
	DeathT = 0.f;
	HitFlash = 0.f;
	BodyRoot->SetRelativeRotation(FRotator::ZeroRotator);
	BodyRoot->SetRelativeLocation(FVector::ZeroVector);
	Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
}

AKnightCharacter* AFoeKnight::FindTarget(float& OutDist) const
{
	AKnightCharacter* Best = nullptr;
	OutDist = 1.0e9f;
	for (TActorIterator<AKnightCharacter> It(GetWorld()); It; ++It)
	{
		if (It->GetHealth() <= 0.f) continue;
		const float D = FVector::Dist2D(It->GetActorLocation(), GetActorLocation());
		if (D < OutDist) { OutDist = D; Best = *It; }
	}
	return Best;
}

FVector2D AFoeKnight::ClampToPen(const FVector2D& P) const
{
	const FVector2D In = PenHalf - FVector2D(80.f, 80.f);   // stay clear of the fence
	return FVector2D(FMath::Clamp(P.X, PenCenter.X - In.X, PenCenter.X + In.X), FMath::Clamp(P.Y, PenCenter.Y - In.Y, PenCenter.Y + In.Y));
}

// Walk toward a point (inside the pen), feet on the ground; returns true once there
bool AFoeKnight::MoveTowards(const FVector2D& To, float Speed, float Dt)
{
	const FVector Me = GetActorLocation();
	const FVector2D Goal = ClampToPen(To);
	const FVector2D D = Goal - FVector2D(Me.X, Me.Y);
	const float Len = D.Size();
	if (Len < 8.f) return true;
	const FVector2D Step = D / Len * FMath::Min(Len, Speed * Dt);
	const FVector2D N(Me.X + Step.X, Me.Y + Step.Y);
	SetActorLocation(FVector(N.X, N.Y, JeebTerrain::HeightAt(N.X, N.Y) + 98.f), true);   // swept: he cannot walk through you
	return false;
}

void AFoeKnight::Rise()
{
	Hp->Reset();
	bRepDown = false;
	DeathT = 0.f;
	HitFlash = 0.f;
	Act = EAct::Idle;
	BlockT = 0.f;
	KnockT = -1.f; GuardPool = GuardMax; bGuardBroken = false; bAir = false;
	AttackCool = 2.f;
	const FVector2D P = PenCenter;
	SetActorLocation(FVector(P.X, P.Y, JeebTerrain::HeightAt(P.X, P.Y) + 98.f));
	BodyRoot->SetRelativeRotation(FRotator::ZeroRotator);
	BodyRoot->SetRelativeLocation(FVector::ZeroVector);
	SetActorHiddenInGame(false);
	Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
}

void AFoeKnight::OnHurt(AActor* From)
{
	HitFlash = 0.35f;
	if (bStill)   // a sparring post: never falls, never steps back (his health fills again in Tick)
	{
		if (Hp->Cur <= 0) Hp->Cur = 1;
		return;
	}
	if (Hp->Cur <= 0)
	{
		if (DeathT <= 0.f)
		{
			DeathT = 0.001f;
			if (bAir) { bAir = false; const FVector P = GetActorLocation(); SetActorLocation(FVector(P.X, P.Y, JeebTerrain::HeightAt(P.X, P.Y) + 98.f)); }   // (beaten in the air: down to the ground)
			KnockT = -1.f;
			bRepDown = true;
			RespawnT = 20.f;
			Act = EAct::Idle;
			BlockT = 0.f;
			Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			if (ALootDrop* Drop = GetWorld()->SpawnActor<ALootDrop>(GetActorLocation() - FVector(0.f, 0.f, 90.f), FRotator::ZeroRotator))
				Drop->Setup(ELootLook::Item, TArray<FInvSlot>(), 12 + Rand.RandRange(0, 8));   // a small purse of ingots
			if (USkillComponent* Sk = From ? From->FindComponentByClass<USkillComponent>() : nullptr)
				Sk->Say(4.f, FColor(255, 210, 120), TEXT("The pen knight yields! He will be ready for another bout soon."));
		}
		return;
	}
	// Knocked back a step, and sometimes he gets his shield up for the next blows (never in the middle of his own swing)
	const FVector Away = (GetActorLocation() - (From ? From->GetActorLocation() : GetActorLocation())).GetSafeNormal2D();
	MoveTowards(FVector2D(GetActorLocation()) + FVector2D(Away.X, Away.Y) * 35.f, 1.0e5f, 1.f);
	if (Act == EAct::Idle && BlockT <= 0.f && !bGuardBroken && KnockT < 0.f && Rand.FRand() < 0.4f) BlockT = Rand.FRandRange(0.9f, 1.6f);
}

void AFoeKnight::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float Dt = FMath::Min(DeltaSeconds, 0.05f);
	const float Now = GetWorld()->GetTimeSeconds();

	// ---- Beaten: topple, lie still, vanish, then stand up again in the middle of the pen ----
	if (DeathT > 0.f)
	{
		if (DeathT < 1.f)
		{
			DeathT = FMath::Min(1.f, DeathT + Dt * 1.6f);
			const float K = DeathT * DeathT;
			BodyRoot->SetRelativeRotation(FRotator(-80.f * K, 0.f, 0.f));
			BodyRoot->SetRelativeLocation(FVector(-30.f * K, 0.f, -60.f * K));
		}
		if (HasAuthority())
		{
			RespawnT -= Dt;
			if (RespawnT < 14.f) SetActorHiddenInGame(true);
			if (RespawnT <= 0.f) Rise();
		}
		LastLoc = GetActorLocation();
		return;
	}

	// Launched by a sweep: he flies (the server moves him; everyone else sees it replicated) until he lands, on his back
	if (HasAuthority() && bAir)
	{
		AirVel.Z -= 980.f * Dt;
		FVector P = GetActorLocation() + AirVel * Dt;
		if (!bStill) { const FVector2D C = ClampToPen(FVector2D(P.X, P.Y)); P.X = C.X; P.Y = C.Y; }
		const float Floor = bStill ? GroundZ : JeebTerrain::HeightAt(P.X, P.Y) + 98.f;
		if (P.Z <= Floor && AirVel.Z < 0.f) { P.Z = Floor; bAir = false; AirVel = FVector::ZeroVector; }
		SetActorLocation(P, true);
	}

	// (on players' machines he is only drawn: the server decides what he does)
	if (!HasAuthority())
	{
		if (Act != EAct::Idle) ActT += Dt;
		BlockT = bRepBlock ? 1.f : -1.f;
	}
	else if (bStill)
	{
		if (Hp->Cur < Hp->Max && Now - Hp->LastHitTime > 3.f) Hp->Reset();   // (training mode: full again after a pause)
		if ((GuardPool < GuardMax || bGuardBroken) && Now - Hp->LastHitTime > 3.f) { GuardPool = GuardMax; bGuardBroken = false; }   // (and his guard)
		BlockT = (bStillGuard && !bGuardBroken && KnockT < 0.f) ? 1.f : -1.f;
		bRepBlock = BlockT > 0.f;
		if (KnockT < 0.f)   // back on his feet away from his spot: he walks back to it and turns to face the way he did
		{
			const FVector To = Post - GetActorLocation();
			if (To.Size2D() > 4.f) SetActorLocation(GetActorLocation() + FVector(To.X, To.Y, 0.f).GetClampedToMaxSize(160.f * Dt), true);
			SetActorRotation(FRotator(0.f, FMath::FixedTurn(GetActorRotation().Yaw, PostYaw, 200.f * Dt), 0.f));
		}
	}
	else if (KnockT >= 0.f)   // knocked down: nothing but getting up
	{
		BlockT = -1.f;
		bRepBlock = false;
		RepAct = static_cast<uint8>(EAct::Idle);
	}
	else
	{
	if ((bGuardBroken || GuardPool < GuardMax) && Now - GuardHitTime > GuardBack) { GuardPool = GuardMax; bGuardBroken = false; }   // (his guard comes back after a pause)
	float Dist = 1.0e9f;
	AKnightCharacter* Knight = FindTarget(Dist);
	const FVector Me = GetActorLocation();
	FVector2D ToP = FVector2D::ZeroVector;
	if (Knight)
	{
		ToP = FVector2D(Knight->GetActorLocation() - Me);
		Dist = ToP.Size();
	}
	const bool bEngaged = Knight && Dist < 1100.f && Knight->GetHealth() > 0.f;

	// ---- Facing: toward you when engaged, otherwise the way he walks ----
	float Yaw = GetActorRotation().Yaw;
	if (bEngaged)
	{
		const float Want = FMath::RadiansToDegrees(FMath::Atan2(ToP.Y, ToP.X));
		const float TurnRate = (Act == EAct::Strike) ? 60.f : 260.f;   // committed once the blade comes down
		Yaw = FMath::FixedTurn(Yaw, Want, TurnRate * Dt);
	}

	// ---- What he does ----
	AttackCool -= Dt;
	BlockT -= Dt;
	if (Act != EAct::Idle) ActT += Dt;
	switch (Act)
	{
	case EAct::Idle:
		if (bEngaged)
		{
			const FVector2D Dir = ToP / FMath::Max(Dist, 1.f);
			if (Dist > 190.f) MoveTowards(FVector2D(Me) + Dir * 200.f, BlockT > 0.f ? 160.f : 300.f, Dt);        // close in
			else if (Dist < 120.f) MoveTowards(FVector2D(Me) - Dir * 100.f, 150.f, Dt);                         // step back
			else                                                                                                   // circle you
			{
				StrafeT -= Dt;
				if (StrafeT <= 0.f) { StrafeDir = Rand.FRand() < 0.5f ? -1.f : 1.f; StrafeT = Rand.FRandRange(1.5f, 3.f); }
				MoveTowards(FVector2D(Me) + FVector2D(-Dir.Y, Dir.X) * StrafeDir * 100.f, 90.f, Dt);
			}
			if (Dist < StrikeReach - 10.f && AttackCool <= 0.f && BlockT <= 0.f) { Act = EAct::Windup; ActT = 0.f; bStruck = false; }
			else if (BlockT <= 0.f && AttackCool > 0.6f && Dist < 260.f && Rand.FRand() < 0.15f * Dt) BlockT = Rand.FRandRange(0.8f, 1.3f);   // now and then he guards
		}
		else
		{
			// Pace the pen while nobody is around
			WanderWait -= Dt;
			if (WanderWait <= 0.f && MoveTowards(WanderTo, 120.f, Dt))
			{
				WanderTo = ClampToPen(PenCenter + FVector2D(Rand.FRandRange(-1.f, 1.f) * PenHalf.X, Rand.FRandRange(-1.f, 1.f) * PenHalf.Y));
				WanderWait = Rand.FRandRange(2.f, 5.f);
			}
			else if (WanderWait <= 0.f)
			{
				const FVector2D D = WanderTo - FVector2D(Me);
				if (!D.IsNearlyZero()) Yaw = FMath::FixedTurn(Yaw, FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X)), 180.f * Dt);
			}
		}
		break;
	case EAct::Windup:   // the telegraph: the sword rises high and holds for a moment
		BlockT = 0.f;
		if (ActT >= 0.65f) { Act = EAct::Strike; ActT = 0.f; }
		break;
	case EAct::Strike:   // a quick downward cut with a short lunge
		if (Knight && Dist > 120.f) MoveTowards(FVector2D(Me) + ToP / FMath::Max(Dist, 1.f) * 100.f, 260.f, Dt);
		if (!bStruck && ActT >= 0.14f)
		{
			bStruck = true;
			if (Knight)
			{
				const FVector2D Fwd(FMath::Cos(FMath::DegreesToRadians(Yaw)), FMath::Sin(FMath::DegreesToRadians(Yaw)));
				const float D2 = FVector2D(Knight->GetActorLocation() - GetActorLocation()).Size();
				if (D2 < StrikeReach && FVector2D::DotProduct(Fwd, ToP / FMath::Max(Dist, 1.f)) > 0.45f)
				{
					Knight->Hurt(Rand.RandRange(HitMin, HitMax), this);   // (during a kick: knocked down, Hurt)
					JeebHit::FReact R; R.Stun = StrikeStun; R.Push = StrikePush;
					Knight->TakeReact(this, R);
				}
			}
		}
		if (ActT >= 0.2f) { Act = EAct::Recover; ActT = 0.f; }
		break;
	case EAct::Stun:     // hit: he can do nothing for a moment (his swing, if any, is lost)
		BlockT = -1.f;
		if (ActT >= StunDur) { Act = EAct::Idle; ActT = 0.f; AttackCool = FMath::Max(AttackCool, 0.4f); }
		break;
	case EAct::Recover:  // a moment open after the swing: your chance to hit back
		if (ActT >= 0.55f) { Act = EAct::Idle; ActT = 0.f; AttackCool = Rand.FRandRange(1.6f, 2.6f); }
		break;
	}
	SetActorRotation(FRotator(0.f, Yaw, 0.f));
	RepAct = static_cast<uint8>(Act);
	if (bGuardBroken) BlockT = FMath::Min(BlockT, -0.01f);   // (a broken guard: the shield stays down)
	bRepBlock = BlockT > 0.f;
	}

	// ---- Animation ----
	const FVector Moved = GetActorLocation() - LastLoc;
	LastLoc = GetActorLocation();
	const float Speed = FVector2D(Moved.X, Moved.Y).Size() / FMath::Max(Dt, 1e-4f);
	MoveBlend = FMath::FInterpTo(MoveBlend, FMath::Clamp(Speed / 150.f, 0.f, 1.f), Dt, 10.f);
	GaitPhase = FMath::Fmod(GaitPhase + 2.f * PI * 1.4f * Dt, 2.f * PI);
	BlockAlpha = FMath::FInterpTo(BlockAlpha, BlockT > 0.f ? 1.f : 0.f, Dt, 12.f);
	HitFlash = FMath::Max(0.f, HitFlash - Dt);
	const float S = FMath::Sin(GaitPhase), C = FMath::Cos(GaitPhase);

	// legs step the way he moves (forward, back, or sideways while circling)
	const FVector LV = GetActorRotation().UnrotateVector(FVector(Moved.X, Moved.Y, 0.f));
	const float Fw = LV.Size() > 0.1f ? LV.X / LV.Size() : 1.f, Sd = LV.Size() > 0.1f ? LV.Y / LV.Size() : 0.f;
	const float HipAmp = 30.f * MoveBlend, KneeAmp = 40.f * MoveBlend;
	const float Crouch = 0.35f * BlockAlpha + (Act == EAct::Strike ? 0.3f : 0.f);
	// a relaxed stance (knees a little bent), feet kept flat at the ankles
	const float Stance = 12.f;
	const float HipRp = HipAmp * S * Fw + 30.f * Crouch + Stance, HipLp = -HipAmp * S * Fw + 30.f * Crouch + Stance;
	const float KneeRp = -KneeAmp * FMath::Pow(0.5f * (1.f + C), 2.f) - 55.f * Crouch - 2.f * Stance;
	const float KneeLp = -KneeAmp * FMath::Pow(0.5f * (1.f - C), 2.f) - 55.f * Crouch - 2.f * Stance;
	HipR->SetRelativeRotation(FRotator(HipRp, 0.f, 22.f * MoveBlend * Sd * (0.5f + 0.5f * S)));
	HipL->SetRelativeRotation(FRotator(HipLp, 0.f, 22.f * MoveBlend * Sd * (0.5f - 0.5f * S)));
	KneeR->SetRelativeRotation(FRotator(KneeRp, 0.f, 0.f));
	KneeL->SetRelativeRotation(FRotator(KneeLp, 0.f, 0.f));
	AnkleR->SetRelativeRotation(FRotator(-(HipRp + KneeRp), 0.f, 0.f));
	AnkleL->SetRelativeRotation(FRotator(-(HipLp + KneeLp), 0.f, 0.f));
	BodyRoot->SetRelativeLocation(FVector(0.f, 0.f, -3.f * MoveBlend * FMath::Abs(S) - 10.f * Crouch - 1.f));

	// sword arm: hangs ready; on a windup it rises high overhead (the tell), then cuts down hard and follows through
	float ArmPitch = 20.f, ArmYaw = 0.f, Elbow = -25.f;
	if (Act == EAct::Windup) { const float K = FMath::SmoothStep(0.f, 0.35f, ActT); ArmPitch = FMath::Lerp(20.f, 165.f, K); Elbow = FMath::Lerp(-25.f, -60.f, K); ArmYaw = -15.f * K; }
	else if (Act == EAct::Strike) { const float K = FMath::Clamp(ActT / 0.16f, 0.f, 1.f); ArmPitch = FMath::Lerp(165.f, 5.f, K * K); Elbow = FMath::Lerp(-60.f, -5.f, K); ArmYaw = FMath::Lerp(-15.f, 20.f, K); }
	else if (Act == EAct::Recover) { const float K = FMath::Clamp(ActT / 0.55f, 0.f, 1.f); ArmPitch = FMath::Lerp(5.f, 20.f, K); Elbow = FMath::Lerp(-5.f, -25.f, K); ArmYaw = FMath::Lerp(20.f, 0.f, K); }
	ArmR->SetRelativeRotation(FRotator(ArmPitch, ArmYaw, 0.f));
	ElbowR->SetRelativeRotation(FRotator(-Elbow, 0.f, 0.f));
	// shield arm: across the body when guarding
	ArmL->SetRelativeRotation(FRotator(FMath::Lerp(10.f, 60.f, BlockAlpha), FMath::Lerp(0.f, 32.f, BlockAlpha), 0.f));
	ElbowL->SetRelativeRotation(FRotator(FMath::Lerp(25.f, 55.f, BlockAlpha), 0.f, 0.f));
	// chest: leans into the cut, rocks back when hit
	const float Flinch = HitFlash / 0.35f;
	UpperPivot->SetRelativeRotation(FRotator((Act == EAct::Strike ? -10.f : (Act == EAct::Windup ? 6.f : 0.f)) + 12.f * Flinch * FMath::Sin(Flinch * 9.f), 0.f, 0.f));
	HeadPivot->SetRelativeRotation(FRotator(0.f, 0.f, 2.f * FMath::Sin(Now * 1.3f)));
	TickKnock(Dt);   // (knocked down: over all of that)
}

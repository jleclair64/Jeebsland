#include "Projectile.h"
#include "KnightCharacter.h"
#include "HealthComponent.h"
#include "HitBoxes.h"
#include "Terrain.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "Net/UnrealNetwork.h"
#include "Inventory.h"

AProjectile::AProjectile()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(false);   // (each machine flies its own copy from the launch)
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	ConstructorHelpers::FObjectFinder<UStaticMesh> Cyl(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(TEXT("/Engine/BasicShapes/Cone.Cone"));
	ConstructorHelpers::FObjectFinder<UStaticMesh> Ball(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	BallMesh = Ball.Object;
	// The arrow is built back from its tip (the actor's own position): a 70 cm shaft, the head, white fletching at the back
	auto Part = [&](const TCHAR* Name, UStaticMesh* Mesh)
	{
		UStaticMeshComponent* C = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		C->SetupAttachment(Root);
		C->SetStaticMesh(Mesh);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		return C;
	};
	Body = Part(TEXT("Body"), Cyl.Object);
	Body->SetRelativeLocationAndRotation(FVector(-37.f, 0.f, 0.f), FRotator(-90.f, 0.f, 0.f));   // (the cylinder stands along Z: laid along X)
	Body->SetRelativeScale3D(FVector(0.012f, 0.012f, 0.68f));
	Head = Part(TEXT("Head"), Cone.Object);
	Head->SetRelativeLocationAndRotation(FVector(-3.f, 0.f, 0.f), FRotator(-90.f, 0.f, 0.f));    // (the cone's point leads)
	Head->SetRelativeScale3D(FVector(0.028f, 0.028f, 0.065f));
	Fletch = Part(TEXT("Fletch"), Cube.Object);
	Fletch->SetRelativeLocation(FVector(-65.f, 0.f, 0.f));
	Fletch->SetRelativeScale3D(FVector(0.1f, 0.004f, 0.035f));
}

void AProjectile::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AProjectile, Shooter);
	DOREPLIFETIME(AProjectile, L);
	DOREPLIFETIME(AProjectile, Stuck);
}

void AProjectile::Launch(AKnightCharacter* InShooter, const FVector& Dir, float InSpeed, float MaxRange, int32 InDamage, bool bInCrit, ESkillType InSkill, bool bMagic, int32 Tier, int32 InArrowId)
{
	Shooter = InShooter;
	Damage = InDamage;
	bCrit = bInCrit;
	Skill = InSkill;
	L.Start = GetActorLocation();
	L.Dir = Dir.GetSafeNormal();
	L.Speed = InSpeed;
	L.Range = MaxRange;
	L.bBolt = bMagic;
	L.Tier = static_cast<uint8>(FMath::Clamp(Tier, 0, 255));
	L.bPurple = bMagic && InSkill == ESkillType::Magic && Tier >= 100;
	L.ArrowId = bMagic ? 0 : InArrowId;
	L.Seq = 0;
	if (HasAuthority()) SetLifeSpan(8.f);   // (a miss into the sky; landing makes it StuckSeconds)
	ApplyLaunch();
}

void AProjectile::OnRep_Launch()
{
	if (Stuck.bStuck) return;
	SetActorLocation(L.Start);
	ApplyLaunch();
}

void AProjectile::ApplyLaunch()
{
	if (L.Seq == SeenSeq || Stuck.bStuck) return;
	const bool bFirst = SeenSeq == 255;
	SeenSeq = L.Seq;
	bFlying = true;
	SetActorHiddenInGame(false);
	Velocity = FVector(L.Dir) * L.Speed;
	SetActorRotation(Velocity.Rotation());
	if (L.Seq > 0) bSpent = true;   // (relaunched: it glanced off a shield)
	if (!bFirst || IsNetMode(NM_DedicatedServer)) return;
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"));
	auto Paint = [&](UStaticMeshComponent* C, const FLinearColor& Col)
	{
		if (UMaterialInstanceDynamic* M = C->CreateDynamicMaterialInstance(0, Base)) M->SetVectorParameterValue(TEXT("Color"), Col);
	};
	if (L.bBolt)
	{
		Head->SetStaticMesh(BallMesh);
		Head->SetRelativeRotation(FRotator::ZeroRotator);
		Head->SetRelativeScale3D(FVector(0.28f));
		Head->SetRelativeLocation(FVector::ZeroVector);
		Body->SetVisibility(false);
		Fletch->SetVisibility(false);
		Paint(Head, L.bPurple ? FLinearColor(2.6f, 0.5f, 3.f) : FLinearColor(0.6f, 1.5f, 3.2f));
		return;
	}
	static const FLinearColor Tips[JeebItems::NumTiers] = {
		FLinearColor(0.5f, 0.52f, 0.54f), FLinearColor(0.35f, 0.35f, 0.4f), FLinearColor(0.7f, 0.4f, 0.2f), FLinearColor(0.6f, 0.6f, 0.7f),
		FLinearColor(0.85f, 0.87f, 0.92f), FLinearColor(1.0f, 0.75f, 0.25f), FLinearColor(0.05f, 0.05f, 0.06f), FLinearColor(0.1f, 1.0f, 0.4f), FLinearColor(0.4f, 1.2f, 1.6f) };   // tin ... diamond
	const bool bFire = JeebItems::IsFireArrow(L.ArrowId);
	Paint(Body, FLinearColor(0.5f, 0.34f, 0.17f));
	Paint(Head, bFire ? FLinearColor(4.f, 1.4f, 0.25f) : Tips[FMath::Clamp<int32>(L.Tier, 0, JeebItems::NumTiers - 1)]);   // (fire: a glowing head)
	Paint(Fletch, bFire ? FLinearColor(0.85f, 0.18f, 0.08f) : FLinearColor(0.92f, 0.92f, 0.88f));
	if (bFire)
	{
		Glow = NewObject<UPointLightComponent>(this);
		Glow->SetupAttachment(Root);
		Glow->SetRelativeLocation(FVector(-4.f, 0.f, 0.f));
		Glow->SetLightColor(FLinearColor(1.f, 0.45f, 0.12f));
		Glow->SetIntensity(900.f);
		Glow->SetAttenuationRadius(380.f);
		Glow->SetCastShadows(false);
		Glow->RegisterComponent();
	}
}

void AProjectile::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bFlying || IsHidden()) return;
	if (!L.bBolt) Velocity.Z -= ArrowSag * DeltaSeconds;
	const FVector From = GetActorLocation();   // (the tip)
	const FVector To = From + Velocity * DeltaSeconds;
	const bool bAuth = HasAuthority();
	// (a player's copy just stops and waits: the server says where it ended up, or relaunches it off a shield)
	auto Wait = [this]() { bFlying = false; SetActorHiddenInGame(true); };
	const FVector Dir = Velocity.GetSafeNormal();

	// Anything with hit boxes along the way (HitBoxes.h): a raised shield's wedge first, then the body. Whatever the tip meets first.
	if (!bSpent)
	{
		JeebHit::FCapsule Seg;
		Seg.A = From; Seg.B = To; Seg.R = L.bBolt ? 8.f : 2.5f;
		float Best = 2.f;
		UHealthComponent* BestH = nullptr;
		AKnightCharacter* BestK = nullptr;
		bool bOnShield = false;
		JeebHit::FShieldWedge BestWedge;
		TArray<JeebHit::FCapsule> Hurt;
		auto Consider = [&](UHealthComponent* H, AKnightCharacter* K, AActor* Who)
		{
			if (FVector::DistSquared(Who->GetActorLocation(), From) > FMath::Square(FVector::Dist(From, To) + 400.f)) return;
			Hurt.Reset();
			JeebHit::FShieldWedge W;
			bool bW;
			if (K) { K->GatherHurt(Hurt); bW = K->GetShieldWedge(W); }
			else { JeebHit::HurtOf(H, Hurt); bW = JeebHit::ShieldOf(H, W); }
			const float Tw = bW ? JeebHit::FirstTouchWedge(Seg, W) : 2.f;
			const float Tb = JeebHit::FirstTouch(Seg, Hurt);
			const float T = FMath::Min(Tw, Tb);
			if (T > 1.f || T >= Best) return;
			Best = T; BestH = H; BestK = K; bOnShield = Tw <= Tb; BestWedge = W;
		};
		for (UHealthComponent* H : UHealthComponent::All())
			if (H && H->IsAlive() && H->GetOwner() && H->GetOwner() != Shooter) Consider(H, nullptr, H->GetOwner());
		if (JeebHit::InMoveLab(GetWorld()))   // other players' knights: only in MoveLab until PvP exists
			for (TActorIterator<AKnightCharacter> It(GetWorld()); It; ++It)
				if (*It != Shooter && It->GetHealth() > 0.f) Consider(nullptr, *It, *It);
		if (Best <= 1.f)
		{
			AActor* Target = BestK ? static_cast<AActor*>(BestK) : BestH->GetOwner();
			const FVector At = From + (To - From) * Best;
			if (bOnShield)
			{
				// The shield itself (its parts' boxes, a little past the wedge's band): the arrow sticks in it. Only the wedge's edge:
				// it glances off to the side. The guard takes the blow either way (no rebound for the archer).
				TArray<UPrimitiveComponent*> Parts;
				if (BestK) BestK->GetShieldParts(Parts); else if (BestH->ShieldParts) BestH->ShieldParts(Parts);
				const FVector S0 = At - Dir * 25.f, S1 = At + Dir * 45.f;
				int32 HitPart = -1; float HitT = 2.f; FVector HitAt = At;
				for (int32 i = 0; i < Parts.Num(); ++i)
				{
					UPrimitiveComponent* C = Parts[i];
					if (!C || !C->IsVisible()) continue;
					const FTransform Xf = C->GetComponentTransform();
					const FBox Local = C->CalcBounds(FTransform::Identity).GetBox();
					FVector HitL, Normal; float Time = 0.f;
					if (FMath::LineExtentBoxIntersection(Local, Xf.InverseTransformPosition(S0), Xf.InverseTransformPosition(S1), FVector(1.5f), HitL, Normal, Time) && Time < HitT)
					{ HitT = Time; HitPart = i; HitAt = Xf.TransformPosition(HitL); }
				}
				if (!bAuth) { if (HitPart >= 0) { SetActorLocation(HitAt); Wait(); return; } bSpent = true; Wait(); return; }
				if (BestK) BestK->Hurt(Damage, Shooter); else if (BestH->ShieldBlock) BestH->ShieldBlock(Damage, Shooter); else if (Shooter) Shooter->DealDamage(BestH, Damage, bCrit, Skill, true);
				if (HitPart >= 0) StickAt(HitAt, Target, Parts[HitPart], FName(TEXT("Shield")), static_cast<uint8>(HitPart), false);
				else Deflect(At, (At - BestWedge.Apex).GetSafeNormal2D());
				return;
			}
			if (!bAuth) { SetActorLocation(At); Wait(); return; }
			if (BestK) BestK->Hurt(Damage, Shooter);
			else if (Shooter) Shooter->DealDamage(BestH, Damage, bCrit, Skill, true);
			if (JeebItems::IsFireArrow(L.ArrowId)) Burn(Target);
			// stuck in him at the joint nearest the tip (his armour: spent)
			USceneComponent* Joint = nullptr;
			float JointD = 1.0e9f;
			TArray<USceneComponent*> Comps;
			Target->GetComponents<USceneComponent>(Comps);
			for (USceneComponent* C : Comps)
			{
				if (!C || C->GetClass() != USceneComponent::StaticClass()) continue;   // (the joints: plain pivots, named the same on every machine)
				const float D = FVector::DistSquared(C->GetComponentLocation(), At);
				if (D < JointD) { JointD = D; Joint = C; }
			}
			if (!Joint) Joint = Target->GetRootComponent();
			if (L.bBolt) { Destroy(); return; }
			StickAt(At, Target, Joint, Joint ? Joint->GetFName() : NAME_None, 0, false);
			return;
		}
	}
	// Solid ground, walls, tree trunks
	FHitResult Blocked;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(Projectile), false, Shooter);
	Q.AddIgnoredActor(this);
	bool bHit = GetWorld()->LineTraceSingleByChannel(Blocked, From, To, ECC_Visibility, Q);
	FVector Land = bHit ? Blocked.ImpactPoint : To;
	const float Ground = JeebTerrain::HeightAt(To.X, To.Y);
	if (!bHit && To.Z < Ground) { bHit = true; Land = FVector(To.X, To.Y, Ground); Blocked = FHitResult(); }   // (the ground itself, where it has no collision)
	if (bHit)
	{
		if (L.bBolt) { if (bAuth) Destroy(); else Wait(); return; }
		if (!bAuth) { SetActorLocation(Land); Wait(); return; }
		StickAt(Land, nullptr, nullptr, NAME_None, 0, !bSpent);   // (the ground, trees and walls stay put: it stays where it is)
		return;
	}
	SetActorLocation(To);
	SetActorRotation(Velocity.Rotation());
	Travelled += (To - From).Size();
	if (L.bBolt && Travelled > L.Range) { if (bAuth) Destroy(); else Wait(); }   // (bolts fade out at their range; arrows fly on until they land)
}

// (server) Comes to rest with its tip at Tip, a little way in: on its own in the world, or held by a part of something
void AProjectile::StickAt(const FVector& Tip, AActor* On, USceneComponent* Comp, FName CompName, uint8 Index, bool bPickable)
{
	const FVector Dir = Velocity.IsNearlyZero() ? GetActorForwardVector() : Velocity.GetSafeNormal();
	const FTransform W(Dir.Rotation(), Tip + Dir * 7.f);   // (sunk 7 cm in)
	Stuck.bStuck = true;
	Stuck.On = Comp ? On : nullptr;
	Stuck.Comp = CompName;
	Stuck.Index = Index;
	Stuck.bPickable = bPickable && !bSpent && !L.bBolt;
	if (Comp)
	{
		const FTransform Rel = W.GetRelativeTransform(Comp->GetComponentTransform());
		Stuck.Loc = Rel.GetLocation(); Stuck.Rot = Rel.Rotator();
	}
	else { Stuck.Loc = W.GetLocation(); Stuck.Rot = W.Rotator(); }
	SetLifeSpan(StuckSeconds);
	ApplyStuck();
}

void AProjectile::OnRep_Stuck()
{
	if (Stuck.bStuck) ApplyStuck();
}

void AProjectile::ApplyStuck()
{
	bFlying = false;
	SetActorHiddenInGame(false);
	USceneComponent* Part = Stuck.On ? FindPart(Stuck.On, Stuck.Comp, Stuck.Index) : nullptr;
	if (Part)
	{
		AttachToComponent(Part, FAttachmentTransformRules::KeepRelativeTransform);
		SetActorRelativeLocation(Stuck.Loc);
		SetActorRelativeRotation(Stuck.Rot);
	}
	else SetActorLocationAndRotation(Stuck.Loc, Stuck.Rot);
	// a fire arrow burns out a few seconds after it lands
	if (Glow)
	{
		FTimerHandle H;
		TWeakObjectPtr<AProjectile> Me(this);
		GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([Me]()
		{
			if (!Me.IsValid()) return;
			if (Me->Glow) Me->Glow->SetVisibility(false);
			if (UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(Me->Head->GetMaterial(0))) M->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.12f, 0.1f, 0.09f));   // (charred)
		}), 5.f, false);
	}
}

USceneComponent* AProjectile::FindPart(AActor* On, FName CompName, uint8 Index) const
{
	if (!On) return nullptr;
	if (CompName == FName(TEXT("Shield")))
	{
		TArray<UPrimitiveComponent*> Parts;
		if (AKnightCharacter* K = Cast<AKnightCharacter>(On)) K->GetShieldParts(Parts);
		else if (UHealthComponent* H = On->FindComponentByClass<UHealthComponent>()) { if (H->ShieldParts) H->ShieldParts(Parts); }
		if (Parts.IsValidIndex(Index) && Parts[Index]) return Parts[Index];
		return On->GetRootComponent();
	}
	TArray<USceneComponent*> Comps;
	On->GetComponents<USceneComponent>(Comps);
	for (USceneComponent* C : Comps) if (C && C->GetFName() == CompName) return C;
	return On->GetRootComponent();
}

// (server) Touched only the edge of a raised shield's range: it glances off to that side, slower and a little up, and comes down
// in the grass (spent: it struck a shield)
void AProjectile::Deflect(const FVector& At, const FVector& Away)
{
	const FVector D = Velocity.GetSafeNormal();
	FVector Side = FVector::CrossProduct(FVector::UpVector, D).GetSafeNormal2D();
	if (FVector::DotProduct(Side, Away) < 0.f) Side = -Side;
	bSpent = true;
	L.Start = At;
	L.Dir = (Side * 0.8f + D * 0.3f + FVector(0.f, 0.f, 0.35f)).GetSafeNormal();
	L.Speed = FMath::Max(Velocity.Size() * 0.3f, 600.f);
	L.Seq = static_cast<uint8>(FMath::Max<int32>(1, (L.Seq + 1) & 255));
	SetActorLocation(At);
	ApplyLaunch();
}

// (server) A fire arrow's target keeps burning: a few points every so often (fire ignores armour, as hazards do)
void AProjectile::Burn(AActor* Target)
{
	if (!Target || !GetWorld()) return;
	TWeakObjectPtr<AActor> T(Target);
	TWeakObjectPtr<AKnightCharacter> S(Shooter);
	for (int32 i = 1; i <= BurnTicks; ++i)
	{
		FTimerHandle H;
		GetWorld()->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([T, S]()
		{
			AActor* A = T.Get();
			if (!A) return;
			if (AKnightCharacter* K = Cast<AKnightCharacter>(A)) { if (K->GetHealth() > 0.f) K->Hurt(BurnDamage, S.Get(), true); }
			else if (UHealthComponent* Hc = A->FindComponentByClass<UHealthComponent>())
			{
				if (!Hc->IsAlive()) return;
				if (S.IsValid()) S->DealDamage(Hc, BurnDamage, false, ESkillType::Archery, true);
				else Hc->ApplyDamage(BurnDamage, nullptr, false, true);
			}
		}), BurnEvery * i, false);
	}
}

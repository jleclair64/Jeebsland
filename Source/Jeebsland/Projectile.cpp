#include "Projectile.h"
#include "SpellFx.h"
#include "JeebSpells.h"
#include "JeebProps.h"
#include "KnightCharacter.h"
#include "HealthComponent.h"
#include "HitBoxes.h"
#include "PvpZones.h"
#include "JeebTuning.h"
#include "Combat.h"
#include "Terrain.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "DayNight.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "Net/UnrealNetwork.h"
#include "Inventory.h"
#include "Deer.h"
#include "Eagle.h"
#include "GameFramework/Character.h"
#include "ProceduralMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/IConsoleManager.h"

// The magic bolt's look, live in Play (read as each bolt is cast). 2026-11-18 second review: its light lit a pool about 3 m wide at dusk
// (700 out to 320 cm); now a tight glow round the bolt and the hand that cast it.
// (2026-11-22: a bolt's colour and look come from its spell's row in Content/Data/Spells.csv, L.Spell: the blue Arcane Bolt, the red Fire Bolt)
static TAutoConsoleVariable<float> CVarBoltLight(TEXT("jeeb.Magic.BoltLight"), 150.f, TEXT("Brightness of the light a magic bolt carries (was 700; 0 = no light)."), ECVF_Default);
static TAutoConsoleVariable<float> CVarBoltLightRadius(TEXT("jeeb.Magic.BoltLightRadius"), 90.f, TEXT("How far (cm) a magic bolt's light reaches (was 320)."), ECVF_Default);
static TAutoConsoleVariable<float> CVarBoltGlow(TEXT("jeeb.Magic.BoltGlow"), 0.9f, TEXT("Brightness on screen of the magic bolt's orb (M_GlowOrb Strength, exposure-locked since 2026-11-19: the same by day and at dusk; was 4 on the scene's scale, a white blob)."), ECVF_Default);

// (test pictures: say where each arrow ended up)
static bool ArrowLog() { static const bool b = FParse::Param(FCommandLine::Get(), TEXT("JeebShot")) || FParse::Param(FCommandLine::Get(), TEXT("JeebArrowTest")); return b; }

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

void AProjectile::Launch(AKnightCharacter* InShooter, const FVector& Dir, float InSpeed, float MaxRange, int32 InDamage, bool bInCrit, ESkillType InSkill, bool bMagic, int32 Tier, int32 InArrowId, float InSag, float InStraight)
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
	L.Sag = bMagic ? 0.f : InSag;
	L.Straight = InStraight;
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
	const bool bRock = JeebItems::IsRock(L.ArrowId);   // (the troll's thrown rock, 2026-11-22: KnightCharacterRock.cpp)
	if (!L.bBolt) SetActorScale3D(FVector(bRock ? 1.15f : FlyScale));
	if (!bFirst || IsNetMode(NM_DedicatedServer)) return;
	if (bRock)
	{
		// the rock's model (its middle at the actor's place); a grey ball if it is missing
		UStaticMesh* RockModel = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/TrollKit/SM_TrollRock.SM_TrollRock"), nullptr, LOAD_NoWarn | LOAD_Quiet);
		Body->SetStaticMesh(RockModel ? RockModel : BallMesh);
		Body->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
		Body->SetRelativeScale3D(RockModel ? FVector(1.f) : FVector(0.16f));
		Body->SetCastShadow(true);
		if (!RockModel)
			if (UMaterialInstanceDynamic* M = Body->CreateDynamicMaterialInstance(0, LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color")))) M->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.42f, 0.42f, 0.44f));
		Head->SetVisibility(false);
		Fletch->SetVisibility(false);
		return;
	}
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"));
	auto Paint = [&](UStaticMeshComponent* C, const FLinearColor& Col)
	{
		if (UMaterialInstanceDynamic* M = C->CreateDynamicMaterialInstance(0, Base)) M->SetVectorParameterValue(TEXT("Color"), Col);
	};
	if (L.bBolt)
	{
		Head->SetStaticMesh(BallMesh);
		Head->SetRelativeRotation(FRotator::ZeroRotator);
		Head->SetRelativeScale3D(FVector(0.11f));   // (2026-11-22: the hot core; the halo and streak round it)
		Head->SetRelativeLocation(FVector::ZeroVector);
		// (2026-11-22, owner: "more like the projectiles in the Hogwarts game ... one simple blue spell for now": both the staff's and the wand's)
		// a white-hot core, a soft blue halo round it (Fletch) and a streak of light trailing behind (Body, a ball stretched along the flight,
		// growing with the distance flown so it never pokes back through the caster: TickBoltLook), a flash as it leaves and a burst where it
		// lands (SpellFx.cpp). /Game/Props/M_GlowOrb, unlit and additive; the old flat paint if it is missing.
		// (2026-11-22, owner: a second spell "a different color like yellow or red": the Fire Bolt, a bigger ball with a yellow-hot core and
		// an orange halo; its size and colour from Spells.csv)
		const JeebSpells::FSpell& Sp = JeebSpells::Get(L.Spell);
		const bool bFireSpell = Sp.Look == JeebSpells::Look_Fire;
		const FLinearColor Gem = Sp.Color;
		const float Sz = Sp.Size;
		Head->SetRelativeScale3D(FVector(0.11f * Sz));
		UE_LOG(LogTemp, Log, TEXT("JEEBSPELL bolt seen: %s (row %d) colour %s, cast by %s"), *Sp.Name, int32(L.Spell), *Gem.ToString(), Shooter ? *Shooter->GetName() : TEXT("?"));
		UMaterialInterface* OrbMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Props/M_GlowOrb.M_GlowOrb"));
		if (OrbMat)
		{
			const float G = FMath::Max(0.f, CVarBoltGlow.GetValueOnGameThread());
			auto Glowing = [&](UStaticMeshComponent* C, const FLinearColor& Col, float Strength)
			{
				C->SetStaticMesh(BallMesh);
				C->SetVisibility(true);
				C->SetCastShadow(false);
				if (UMaterialInstanceDynamic* M = C->CreateDynamicMaterialInstance(0, OrbMat))
				{
					M->SetVectorParameterValue(TEXT("Color"), Col);
					M->SetScalarParameterValue(TEXT("Strength"), Strength);
				}
			};
			Glowing(Head, bFireSpell ? FLinearColor(1.f, 0.85f, 0.35f) : FMath::Lerp(Gem, FLinearColor::White, 0.6f), G * 1.6f);
			Glowing(Fletch, Gem, G * (bFireSpell ? 0.7f : 0.55f));
			Fletch->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
			Fletch->SetRelativeScale3D(FVector(0.24f * Sz));
			Glowing(Body, Gem, G * 0.7f);
			Body->SetRelativeRotation(FRotator::ZeroRotator);
			Body->SetRelativeScale3D(FVector(0.001f));
			ASpellFx::Play(GetWorld(), ESpellFx::Muzzle, GetActorLocation(), FVector(L.Dir), Gem);
		}
		else { Body->SetVisibility(false); Fletch->SetVisibility(false); Paint(Head, FLinearColor(0.6f, 1.5f, 3.2f)); }
		const float LightI = FMath::Max(0.f, CVarBoltLight.GetValueOnGameThread());
		if (LightI > 0.f)
		{
			Glow = NewObject<UPointLightComponent>(this);
			Glow->SetupAttachment(Root);
			Glow->SetLightColor(Gem);
			Glow->SetIntensity(LightI);
			Glow->SetAttenuationRadius(FMath::Clamp(CVarBoltLightRadius.GetValueOnGameThread(), 10.f, 1000.f));
			Glow->SetCastShadows(false);
			Glow->RegisterComponent();
		}
		return;
	}
	const bool bFire = JeebItems::IsFireArrow(L.ArrowId);
	if (UStaticMesh* ArrowModel = JeebProps::ArrowMesh(L.ArrowId))
	{
		// (2026-11-12) the arrows' model: its tip is the actor's origin, as the shapes' was; its head and feathers in the arrow's colours
		Body->SetStaticMesh(ArrowModel);
		Body->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
		Body->SetRelativeScale3D(FVector(1.f));
		Body->SetMaterial(0, nullptr);
		Head->SetVisibility(false);
		Fletch->SetVisibility(false);
		JeebProps::TintArrow(Body, L.ArrowId);
	}
	else
	{
		Paint(Body, FLinearColor(0.5f, 0.34f, 0.17f));
		Paint(Head, JeebItems::ArrowTipColor(L.ArrowId));   // (fire: a glowing head)
		Paint(Fletch, JeebItems::ArrowFeatherColor(L.ArrowId));
	}
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
	const bool bRock = JeebItems::IsRock(L.ArrowId);   // (a thrown rock: no streak, it tumbles, it bounces off what it hits)
	if (!IsNetMode(NM_DedicatedServer)) TickTrail();   // (2026-11-22, owner: the white movement line on spells and the troll's rock too)
	if (!bFlying || IsHidden()) return;
	if (Travelled >= L.Straight) Velocity.Z -= L.Sag * DeltaSeconds;   // (a full draw: none at all for its first L.Straight)
	const FVector From = GetActorLocation();   // (the tip)
	const FVector To = From + Velocity * DeltaSeconds;
	const bool bAuth = HasAuthority();
	// (a player's copy just stops and waits: the server says where it ended up, or relaunches it off a shield)
	auto Wait = [this]() { bFlying = false; SetActorHiddenInGame(true); };
	const FVector Dir = Velocity.GetSafeNormal();
	auto BoltBurst = [&](const FVector& P)
	{
		if (!L.bBolt || IsNetMode(NM_DedicatedServer)) return;
		const JeebSpells::FSpell& Sp = JeebSpells::Get(L.Spell);
		ASpellFx::Play(GetWorld(), Sp.Look == JeebSpells::Look_Fire ? ESpellFx::FireBurst : ESpellFx::Burst, P, Dir, Sp.Color);
	};   // (2026-11-22: a burst where a spell lands)

	// Anything with hit boxes along the way (HitBoxes.h): a raised shield's wedge first, then the body. Whatever the tip meets first.
	if (!bSpent)
	{
		JeebHit::FCapsule Seg;
		Seg.A = From; Seg.B = To; Seg.R = L.bBolt ? 8.f : bRock ? 7.f : 2.5f;
		float Best = 2.f;
		UHealthComponent* BestH = nullptr;
		AKnightCharacter* BestK = nullptr;
		bool bOnShield = false;
		uint8 BestZone = JeebHit::Z_None;   // (the body part it meets: armour, and headshots)
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
			uint8 Zone = JeebHit::Z_None;
			const float Tb = JeebHit::FirstTouch(Seg, Hurt, &Zone);
			const float T = FMath::Min(Tw, Tb);
			if (T > 1.f || T >= Best) return;
			Best = T; BestH = H; BestK = K; bOnShield = Tw <= Tb; BestWedge = W; BestZone = Zone;
		};
		for (UHealthComponent* H : UHealthComponent::All())
			if (H && H->IsAlive() && H->GetOwner() && H->GetOwner() != Shooter) Consider(H, nullptr, H->GetOwner());
		// other players' knights: only where the shooter and the target both stand in a PvP zone (PvpZones.h; MoveLab always). The server
		// decides; a player's copy of the arrow flies for show and stops at nobody it may not hurt.
		for (TActorIterator<AKnightCharacter> It(GetWorld()); It; ++It)
			if (*It != Shooter && It->GetHealth() > 0.f && JeebPvp::MayHurt(Shooter, *It)) Consider(nullptr, *It, *It);
		if (Best <= 1.f)
		{
			AActor* Target = BestK ? static_cast<AActor*>(BestK) : BestH->GetOwner();
			if (ArrowLog()) UE_LOG(LogTemp, Log, TEXT("JEEBARROW hit %s shield %d"), *Target->GetName(), bOnShield ? 1 : 0);
			const FVector At = From + (To - From) * Best;
			if (bOnShield && bRock)   // (a rock off a raised shield: no damage, the guard untouched; it bounces away)
			{
				if (!bAuth) { SetActorLocation(At); Wait(); return; }
				Deflect(At, (At - BestWedge.Apex).GetSafeNormal2D());
				return;
			}
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
				BoltBurst(At);
				if (!bAuth) { if (HitPart >= 0) { SetActorLocation(HitAt); Wait(); return; } bSpent = true; Wait(); return; }
				// Arrows and bolts do no shield damage (owner 2026-11-12): a raised shield simply stops them, its guard untouched
				if (!BestK && !BestH->ShieldBlock && Shooter) Shooter->DealDamage(BestH, Damage, bCrit, Skill, true);
				if (HitPart >= 0) StickAt(HitAt, Target, Parts[HitPart], FName(TEXT("Shield")), static_cast<uint8>(HitPart), false);
				else Deflect(At, (At - BestWedge.Apex).GetSafeNormal2D());
				return;
			}
			if (!bAuth) { BoltBurst(At); SetActorLocation(At); Wait(); return; }
			// A headshot (owner, 2026-11-02): arrows and bolts to the head do 1.5x, before the helmet takes its share
			const bool bHead = BestZone == JeebHit::Z_Head;
			int32 Dmg = bHead ? FMath::RoundToInt(Damage * JeebCombat::HeadshotMul()) : Damage;
			// (2026-11-23, owner) arrows (not bolts or rocks) do double to a target on a galloping horse, a broom or a troll's rush
			if (BestK && !L.bBolt && !bRock && BestK->IsFastMounted())
			{
				Dmg = FMath::Max(1, FMath::RoundToInt(Dmg * FMath::Max(1.f, JeebTune::V().ArrowMountedMul)));
				if (ArrowLog()) UE_LOG(LogTemp, Log, TEXT("JEEBARROW fast-mounted target: x%.1f -> %d"), JeebTune::V().ArrowMountedMul, Dmg);
			}
			// (2026-11-26) each arrow's job (Arrows.csv), all on the server: broadheads hurt animals more, bodkins go through armour, heavy arrows shove, fire arrows burn
			const bool bArrow = !L.bBolt && !bRock && JeebItems::IsArrow(L.ArrowId);
			const JeebItems::FArrowRow& AR = JeebItems::ArrowRowFor(L.ArrowId);
			const bool bAnimal = Target->IsA<ADeer>() || Target->IsA<AEagle>();
			if (bArrow && bAnimal && !FMath::IsNearlyEqual(AR.AnimalMul, 1.f)) Dmg = FMath::Max(1, FMath::RoundToInt(Dmg * AR.AnimalMul));
			const float Pierce = bArrow ? AR.Pierce : 0.f;
			if (ArrowLog()) UE_LOG(LogTemp, Log, TEXT("JEEBARROW %s hits %s: dmg %d animal %d pierce %.2f knock %.0f burn %.0f"), *JeebItems::ItemName(L.ArrowId), *Target->GetName(), Dmg, bAnimal ? 1 : 0, Pierce, bArrow ? AR.Knock : 0.f, bArrow ? AR.Burn : 0.f);
			if (BestK) BestK->Hurt(Dmg, Shooter, false, BestZone, bHead, Pierce);
			else if (Shooter) { BestH->PierceNext = Pierce; Shooter->DealDamage(BestH, Dmg, bCrit, Skill, true, BestZone, bHead); BestH->PierceNext = 0.f; }
			if (bArrow && AR.Knock > 0.f)
			{
				const FVector Away = Dir.GetSafeNormal2D();
				if (BestK) { if (BestK->GetHealth() > 0.f && JeebPvp::MayHurt(Shooter, BestK)) { JeebHit::FReact R; R.Stun = 0.35f; R.Push = AR.Knock; BestK->TakeReact(Shooter ? static_cast<AActor*>(Shooter) : this, R); } }
				else if (ACharacter* C = Cast<ACharacter>(Target)) C->LaunchCharacter(Away * AR.Knock + FVector(0.f, 0.f, 120.f), true, false);
			}
			if (bArrow && AR.Burn > 0.f) Burn(Target, AR.Burn);
			if (bRock) { Deflect(At, -Dir.GetSafeNormal2D()); return; }   // (a rock never sticks in anyone: it drops off him, spent)
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
			if (L.bBolt) { BoltBurst(At); Destroy(); return; }
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
		if (L.bBolt) { BoltBurst(Land); if (bAuth) Destroy(); else Wait(); return; }
		if (!bAuth) { SetActorLocation(Land); Wait(); return; }
		StickAt(Land, nullptr, nullptr, NAME_None, 0, !bSpent);   // (the ground, trees and walls stay put: it stays where it is)
		return;
	}
	SetActorLocation(To);
	Travelled += (To - From).Size();
	if (bRock) SetActorRotation((Velocity.Rotation().Quaternion() * FQuat(FVector::YAxisVector, -Travelled * 0.011f)).Rotator());   // (it tumbles end over end as it flies)
	else SetActorRotation(Velocity.Rotation());
	if (L.bBolt && !IsNetMode(NM_DedicatedServer) && Body && Body->IsVisible())   // (2026-11-22: the streak behind the spell, up to 90 cm, and the halo's shimmer)
	{
		const float Sz = JeebSpells::Get(L.Spell).Size;
		const float Len = FMath::Min(Travelled, 90.f * Sz);
		Body->SetRelativeLocation(FVector(-Len * 0.5f, 0.f, 0.f));
		Body->SetRelativeScale3D(FVector(FMath::Max(Len, 1.f) / 100.f, 0.075f * Sz, 0.075f * Sz));
		if (Fletch) Fletch->SetRelativeScale3D(FVector(0.24f * Sz * (1.f + 0.12f * FMath::Sin(Travelled * 0.05f))));
		// (2026-11-22, owner: "sort of like a bottle rocket with a sparkly/smokey trail") sparks in the spell's colour and soft puffs of smoke
		// left hanging in the air behind it, each gone within about 0.8 s (SpellFx Spark / Smoke; about 20 alive per bolt at the staff's speed); none in its first metre (the hand)
		if (Travelled > 100.f)
		{
			const FLinearColor Col = JeebSpells::Get(L.Spell).Color;
			const FVector Back = -Velocity.GetSafeNormal();
			if (Travelled - SparkAt >= 200.f) { SparkAt = Travelled; ASpellFx::Play(GetWorld(), ESpellFx::Spark, GetActorLocation() + Back * 20.f, Back, Col); }
			if (Travelled - SmokeAt >= 350.f) { SmokeAt = Travelled; ASpellFx::Play(GetWorld(), ESpellFx::Smoke, GetActorLocation() + Back * 40.f, Back, Col); }
		}
	}
	if (L.bBolt && Travelled > L.Range) { if (bAuth) Destroy(); else Wait(); }   // (bolts fade out at their range; arrows fly on until they land)
}

// (server) Comes to rest with its tip at Tip, a little way in: on its own in the world, or held by a part of something
void AProjectile::StickAt(const FVector& Tip, AActor* On, USceneComponent* Comp, FName CompName, uint8 Index, bool bPickable)
{
	const FVector Dir = Velocity.IsNearlyZero() ? GetActorForwardVector() : Velocity.GetSafeNormal();
	FTransform W(Dir.Rotation(), Tip + Dir * 7.f);   // (sunk 7 cm in)
	if (JeebItems::IsRock(L.ArrowId))   // (a rock lies on what it landed on, a random way round, a little sunk)
		W = FTransform(FRotator(FMath::FRandRange(-25.f, 25.f), FMath::FRandRange(0.f, 360.f), FMath::FRandRange(-25.f, 25.f)), Tip + FVector(0.f, 0.f, 3.f));
	if (ArrowLog()) UE_LOG(LogTemp, Log, TEXT("JEEBARROW stuck at %s on %s / %s (dmg %d)"), *Tip.ToCompactString(), On ? *On->GetName() : TEXT("-"), *CompName.ToString(), Damage);
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
	SetActorScale3D(FVector(1.f));   // (flying it was drawn bigger)
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
			if (!Me->Head->IsVisible()) JeebProps::Tint(Me->Body, FLinearColor(0.12f, 0.1f, 0.09f), 0.f, 0.8f);   // (the model's head)
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
	L.Straight = 0.f;
	L.Seq = static_cast<uint8>(FMath::Max<int32>(1, (L.Seq + 1) & 255));
	SetActorLocation(At);
	ApplyLaunch();
}

// (server) A fire arrow's target keeps burning: a few points every so often (fire ignores armour, as hazards do)
void AProjectile::Burn(AActor* Target, float Total)
{
	if (!Target || !GetWorld() || Total <= 0.f) return;
	const int32 Per = FMath::Max(1, FMath::RoundToInt(Total / BurnTicks));
	TWeakObjectPtr<AActor> T(Target);
	TWeakObjectPtr<AKnightCharacter> S(Shooter);
	for (int32 i = 1; i <= BurnTicks; ++i)
	{
		FTimerHandle H;
		GetWorld()->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([T, S, Per]()
		{
			AActor* A = T.Get();
			if (!A) return;
			if (AKnightCharacter* K = Cast<AKnightCharacter>(A)) { if (K->GetHealth() > 0.f && JeebPvp::MayHurt(S.Get(), K)) K->Hurt(Per, S.Get(), true); }
			else if (UHealthComponent* Hc = A->FindComponentByClass<UHealthComponent>())
			{
				if (!Hc->IsAlive()) return;
				if (S.IsValid()) S->DealDamage(Hc, Per, false, ESkillType::Archery, true);
				else Hc->ApplyDamage(Per, nullptr, false, true);
			}
		}), BurnEvery * i, false);
	}
}

// (players' machines) A thin white streak behind a flying arrow, like the sword's: the tail's last moments, turned to face the
// camera, thinning and fading out behind it. It fades away once the arrow lands.
// It starts a few feet out in front of the knight and comes in softly over a foot and a half (owner, 2026-11-06). A fast arrow goes 90 cm in a frame, so the
// soft part gets points of its own along the way; nothing is drawn before it. (The tail leaves from about 60 cm behind his chest.)
void AProjectile::TickTrail()
{
	const float Now = GetWorld()->GetTimeSeconds();
	if (bFlying && !IsHidden())
	{
		static constexpr float TrailSkip = 220.f, TrailRamp = 45.f;
		const FVector Tail = GetActorLocation() - GetActorForwardVector() * (L.bBolt ? 0.f : JeebItems::IsRock(L.ArrowId) ? 8.f : 68.f * GetActorScale3D().X);
		if (TrailFlown < 0.f) { TrailFlown = 0.f; TrailLast = Tail; TrailLastT = Now; }
		const float Step = FVector::Dist(Tail, TrailLast), From = TrailFlown, To = From + Step;
		auto AddAt = [&](float D)
		{
			const float A = Step > 0.01f ? (D - From) / Step : 1.f;
			TrailPts.Add({ FMath::Lerp(TrailLast, Tail, A), FMath::Lerp(TrailLastT, Now, A), FMath::SmoothStep(TrailSkip, TrailSkip + TrailRamp, D) });
		};
		for (int32 i = 0; i <= 5; ++i)
		{
			const float D = TrailSkip + TrailRamp * i / 5.f;
			if (D > From && D <= To) AddAt(D);
		}
		if (To > TrailSkip + TrailRamp) AddAt(To);
		TrailFlown = To;
		TrailLast = Tail;
		TrailLastT = Now;
	}
	TrailPts.RemoveAll([&](const FTrailPt& P) { return Now - P.T > TrailLife; });
	if (!Trail)
	{
		if (TrailPts.Num() < 2) return;
		Trail = NewObject<UProceduralMeshComponent>(this, TEXT("Trail"));
		Trail->SetupAttachment(Root);
		Trail->SetUsingAbsoluteLocation(true); Trail->SetUsingAbsoluteRotation(true); Trail->SetUsingAbsoluteScale(true);
		Trail->SetWorldTransform(FTransform::Identity);   // (its points are world positions)
		Trail->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Trail->SetCastShadow(false);
		Trail->RegisterComponent();
		if (UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_SwordTrail.M_SwordTrail")))
			if (UMaterialInstanceDynamic* D = Trail->CreateDynamicMaterialInstance(0, M))   // (fuller and brighter than the sword's wisp: easy to follow)
			{
				D->SetScalarParameterValue(TEXT("Opacity"), 1.f);
				D->SetScalarParameterValue(TEXT("Bright"), 1.3f);
			}
	}
	if (TrailPts.Num() < 2) { if (Trail->GetNumSections() > 0) Trail->ClearAllMeshSections(); return; }
	if (UMaterialInstanceDynamic* D = Cast<UMaterialInstanceDynamic>(Trail->GetMaterial(0)))   // (dimmer and warmer in the low sun)
		D->SetVectorParameterValue(TEXT("Tint"), FLinearColor(0.92f, 0.95f, 1.0f) * ADayNight::TrailLight);
	FVector Cam = GetActorLocation() + FVector(0.f, 0.f, 300.f);
	if (const APlayerController* PC = GetWorld()->GetFirstPlayerController())   // (only for the look: whoever is watching here)
		if (PC->PlayerCameraManager) Cam = PC->PlayerCameraManager->GetCameraLocation();
	TArray<FVector> V; TArray<FVector2D> UV; TArray<FLinearColor> Col; TArray<int32> Tri;
	const int32 N = TrailPts.Num();
	for (int32 i = 0; i < N; ++i)
	{
		const FVector P = TrailPts[i].P;
		const FVector Along = (TrailPts[FMath::Min(i + 1, N - 1)].P - TrailPts[FMath::Max(i - 1, 0)].P).GetSafeNormal();
		FVector Side = FVector::CrossProduct(Along, (Cam - P).GetSafeNormal()).GetSafeNormal();
		if (Side.IsNearlyZero()) Side = FVector::UpVector;
		const float Age = FMath::Clamp((Now - TrailPts[i].T) / TrailLife, 0.f, 1.f);
		const float Half = TrailWidth * (1.f - 0.7f * Age) * FMath::Lerp(0.35f, 1.f, TrailPts[i].W);
		const float Alpha = FMath::Pow(1.f - Age, 1.2f) * TrailPts[i].W;
		V.Add(P - Side * Half); UV.Add(FVector2D(Age, 0.f)); Col.Add(FLinearColor(1.f, 1.f, 1.f, Alpha));
		V.Add(P + Side * Half); UV.Add(FVector2D(Age, 1.f)); Col.Add(FLinearColor(1.f, 1.f, 1.f, Alpha));
	}
	for (int32 k = 0; k + 3 < V.Num(); k += 2) Tri.Append({ k, k + 1, k + 3, k, k + 3, k + 2 });
	Trail->CreateMeshSection_LinearColor(0, V, Tri, TArray<FVector>(), UV, Col, TArray<FProcMeshTangent>(), false);
}

float AProjectile::FirstBodyAlong(UWorld* World, const FVector& A, const FVector& B, const AActor* Ignore)
{
	if (!World) return 2.f;
	JeebHit::FCapsule Seg;
	Seg.A = A; Seg.B = B; Seg.R = 3.f;
	float Best = 2.f;
	TArray<JeebHit::FCapsule> Hurt;
	auto Consider = [&](UHealthComponent* H, AKnightCharacter* K)
	{
		Hurt.Reset();
		JeebHit::FShieldWedge W;
		bool bW;
		if (K) { K->GatherHurt(Hurt); bW = K->GetShieldWedge(W); }
		else { JeebHit::HurtOf(H, Hurt); bW = JeebHit::ShieldOf(H, W); }
		Best = FMath::Min3(Best, bW ? JeebHit::FirstTouchWedge(Seg, W) : 2.f, JeebHit::FirstTouch(Seg, Hurt));
	};
	for (UHealthComponent* H : UHealthComponent::All())
		if (H && H->IsAlive() && H->GetOwner() && H->GetOwner() != Ignore) Consider(H, nullptr);
	for (TActorIterator<AKnightCharacter> It(World); It; ++It)
		if (*It != Ignore && It->GetHealth() > 0.f) Consider(nullptr, *It);
	return Best;
}

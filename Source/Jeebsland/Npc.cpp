#include "Npc.h"
#include "Lore.h"
#include "SkillComponent.h"
#include "Terrain.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "NpcBody.h"
#include "NpcLife.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "EngineUtils.h"
#include "TimerManager.h"

ANpc::ANpc()
{
	PrimaryActorTick.bCanEverTick = true;

	Capsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Capsule"));
	Capsule->InitCapsuleSize(36.f, 92.f);
	Capsule->SetCollisionProfileName(TEXT("BlockAll"));
	Capsule->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	// (2026-11-18) players pass through the townsfolk, as in RuneScape: each machine shows a person a moment behind or ahead of the
	// server while they stop for whoever talks to them (NpcLife.h), so a person that blocked knights would make the server and the
	// player disagree about where the knight can go. No overlap events either: they move every frame.
	Capsule->SetCollisionObjectType(ECC_WorldDynamic);   // (a moving thing: the ground traces, which look at WorldStatic only, skip it)
	Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	Capsule->SetGenerateOverlapEvents(false);
	SetRootComponent(Capsule);

	BodyRoot = CreateDefaultSubobject<USceneComponent>(TEXT("BodyRoot"));
	BodyRoot->SetupAttachment(Capsule);
	BodyRoot->SetRelativeLocation(FVector(0.f, 0.f, -92.f)); // feet
}

FString ANpc::GetNpcName() const
{
	if (!NameOverride.IsEmpty()) return NameOverride;   // (Steele Creek's people, SetFolk)
	switch (Role)
	{
	case ENpcRole::Shopkeeper: return TEXT("Brindle, the Trader");
	case ENpcRole::Blacksmith: return TEXT("Hobb Ironhand");
	case ENpcRole::Villager:   return JeebLore::SpeakerName(3 + VillagerIdx);
	case ENpcRole::Banker:     return JeebLore::SpeakerName(JeebLore::BankerSpeaker);
	case ENpcRole::QuestGiver: return Look.Name;
	default:                   return TEXT("Sir Aldous, Gate Knight");
	}
}

int32 ANpc::SpeakerIdx() const
{
	switch (Role)
	{
	case ENpcRole::Shopkeeper: return 0;
	case ENpcRole::Blacksmith: return 1;
	case ENpcRole::Villager:   return 3 + VillagerIdx;
	case ENpcRole::Banker:     return JeebLore::BankerSpeaker;
	case ENpcRole::QuestGiver: return -1;   // (no lore: they talk about their quests, QuestComponent.cpp)
	case ENpcRole::Townsfolk:  return -1;   // (no lore: a greeting of their own, Townsfolk.csv)
	default:                   return 2;
	}
}

void ANpc::Setup(ENpcRole InRole, int32 InVillagerIdx)
{
	Role = InRole;
	VillagerIdx = FMath::Clamp(InVillagerIdx, 0, JeebLore::NumVillagers - 1);
	HomeYaw = GetActorRotation().Yaw;
	PlacedAt = FVector2D(GetActorLocation()); PlacedYaw = HomeYaw;

	// Villagers' and quest givers' sizes (Tobin is a boy)
	const bool bQuestP = (Role == ENpcRole::QuestGiver);
	if (Role == ENpcRole::Villager || bQuestP)
	{
		const float Sc = bQuestP ? Look.Scale : (VillagerIdx == 1) ? 0.78f : (VillagerIdx == 5 ? 0.94f : 1.f);
		if (Sc != 1.f)
		{
			SetActorScale3D(FVector(Sc));
			HalfH = 92.f * Sc;
			AddActorWorldOffset(FVector(0.f, 0.f, -92.f * (1.f - Sc)));
		}
	}
	LastLoc = GetActorLocation();
	BuildBody();
}

// The person's own garment colour: the old body's tunic (or robe), dyed onto a model's main garment too
FLinearColor ANpc::OwnColour() const
{
	static const FLinearColor VTunic[JeebLore::NumVillagers] = {
		{0.35f,0.20f,0.45f,1.f}, {0.25f,0.45f,0.30f,1.f}, {0.40f,0.10f,0.12f,1.f}, {0.40f,0.32f,0.20f,1.f}, {0.80f,0.72f,0.55f,1.f},
		{0.18f,0.18f,0.20f,1.f}, {0.15f,0.35f,0.50f,1.f}, {0.45f,0.30f,0.15f,1.f}, {0.55f,0.18f,0.35f,1.f}, {0.50f,0.40f,0.28f,1.f} };
	if (Role == ENpcRole::QuestGiver) return Look.Tunic;
	if (Folk == 2) return Role == ENpcRole::Banker ? FLinearColor(0.10f, 0.12f, 0.36f) : FLinearColor(0.28f, 0.14f, 0.40f);
	switch (Role)
	{
	case ENpcRole::Villager:   return VTunic[VillagerIdx];
	case ENpcRole::Banker:     return FLinearColor(0.08f, 0.14f, 0.12f);
	case ENpcRole::Blacksmith: return FLinearColor(0.30f, 0.30f, 0.33f);
	case ENpcRole::GateKnight: return FLinearColor(0.60f, 0.62f, 0.68f);
	default:                   return FLinearColor(0.50f, 0.22f, 0.12f);
	}
}

void ANpc::BuildBody()
{
	BuiltSetting = JeebPeople::Setting();
	if (JeebPeople::Enabled() && BuildModel()) return;
	BuildShapes();
}

// Takes the body apart (jeeb.Npc.Models changed): every component under BodyRoot
void ANpc::ClearBody()
{
	TArray<USceneComponent*> Kids;
	BodyRoot->GetChildrenComponents(true, Kids);
	for (USceneComponent* K : Kids) if (K) K->DestroyComponent();
	HeadPivot = ArmRPivot = ArmLPivot = LegLPivot = LegRPivot = nullptr;
	Body = nullptr;
	ModelDef = nullptr;
	BodyRoot->SetRelativeScale3D(FVector(1.f));
	BodyRoot->SetRelativeLocation(FVector(0.f, 0.f, -92.f));
}

// A skinned Meshy body (NpcBody.h): which one and its colour from PeopleWho.csv; false keeps the old shapes
bool ANpc::BuildModel()
{
	static const TCHAR* RoleKeys[] = { TEXT("shopkeeper"), TEXT("blacksmith"), TEXT("gateknight"), TEXT("villager"), TEXT("banker"), TEXT("questgiver"), TEXT("townsfolk") };
	FString RoleKey = RoleKeys[FMath::Clamp(int32(Role), 0, 6)];
	if (Role == ENpcRole::Villager) RoleKey += FString::FromInt(VillagerIdx);
	const FString FolkKey = Folk == 1 ? TEXT("troll") : Folk == 2 ? TEXT("wizard") : TEXT("");
	FString Short;
	GetNpcName().Split(TEXT(","), &Short, nullptr);   // ("Sir Dunmore, the Trader": also "Sir Dunmore")
	TArray<FString> Keys = { GetNpcName(), Short, PersonKey.IsNone() ? FString() : PersonKey.ToString() };
	if (!FolkKey.IsEmpty()) { Keys.Add(FolkKey + TEXT(":") + RoleKey); Keys.Add(FolkKey); }
	// (2026-11-18, Townsfolk.csv: a person with a look of their own there wears it unless PeopleWho.csv names them, or their folk)
	JeebPeople::FWho Who = HintModel.IsEmpty() ? JeebPeople::FWho() : JeebPeople::Find(Keys);
	if (!HintModel.IsEmpty() && !Who.bFound)
	{
		Who.Model = HintModel; Who.bFound = true; Who.Scale = HintScale;
		if (HintTint.R >= 0.f) Who.Tint = HintTint;
	}
	Keys.Add(RoleKey);
	if (!Who.bFound) Who = JeebPeople::Find(Keys);
	if (!TestModel.IsEmpty()) { Who.Model = TestModel; Who.bFound = true; }   // (test pictures: -PeopleLineup)
	if (!Who.bFound)
	{
		// (no row: by role; a quest giver with light hair is one of the town's elders)
		const bool bLightHair = Look.Hair.GetLuminance() > 0.55f;
		Who.Model = Role == ENpcRole::Blacksmith ? TEXT("Smith") : Role == ENpcRole::Banker ? TEXT("Banker") : Role == ENpcRole::GateKnight ? TEXT("Guard")
			: (Role == ENpcRole::QuestGiver && bLightHair) ? TEXT("Elder") : TEXT("Townsman");
	}
	if (Who.Model.IsEmpty() || Who.Model.Equals(TEXT("shapes"), ESearchCase::IgnoreCase)) return false;
	const JeebPeople::FModel* M = JeebPeople::Model(Who.Model);
	if (!M) return false;

	USkeletalMeshComponent* S = NewObject<USkeletalMeshComponent>(this);
	S->SetupAttachment(BodyRoot);
	S->SetRelativeRotation(FRotator(0.f, M->Yaw, 0.f));
	if (Who.Scale > 0.f && Who.Scale != 1.f) S->SetRelativeScale3D(FVector(Who.Scale));   // (PeopleWho.csv Scale: the trolls are bigger)
	S->SetSkeletalMesh(M->Mesh);
	S->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	S->SetGenerateOverlapEvents(false);
	S->SetCanEverAffectNavigation(false);
	// cheap when far or unseen: no pose updates off screen, fewer updates at a distance, gone past 90 m
	S->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	S->bEnableUpdateRateOptimizations = true;
	S->LDMaxDrawDistance = 9000.f;
	S->CachedMaxDrawDistance = 9000.f;
	S->SetAnimInstanceClass(UNpcAnimInstance::StaticClass());
	S->RegisterComponent();
	if (UMaterialInstanceDynamic* D = S->CreateDynamicMaterialInstance(0))
	{
		D->SetVectorParameterValue(TEXT("Tint"), Who.Tint.R >= 0.f ? Who.Tint : OwnColour());
		D->SetScalarParameterValue(TEXT("GarmentLum"), M->GarmentLum);
		// (PeopleWho.csv Skin: the skin dyed too, where the model's material has a skin mask: the trolls' green-grey)
		if (Who.Skin.A > 0.f) D->SetVectorParameterValue(TEXT("SkinTint"), FLinearColor(Who.Skin.R, Who.Skin.G, Who.Skin.B, 1.f));
	}
	if (UNpcAnimInstance* A = Cast<UNpcAnimInstance>(S->GetAnimInstance()))
	{
		// (each person starts their idle somewhere else, the same on every machine)
		const float StartAt = float(GetTypeHash(GetNpcName()) % 1000u) * 0.0137f;
		A->SetClips(M->Clips[0], M->Clips[1], M->Clips[2], StartAt);
	}
	Body = S;
	ModelDef = M;
	bShadowOn = true;
	return true;
}

// The old body built from simple shapes (also every dedicated server's, and Steele Creek's trolls')
void ANpc::BuildShapes()
{
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UStaticMesh* Cyl = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* Cone = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"));
	UMaterialInterface* Metal = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Metal.M_Metal"));

	auto Pivot = [&](USceneComponent* Parent, const FVector& Loc) -> USceneComponent*
	{
		USceneComponent* P = NewObject<USceneComponent>(this);
		P->SetupAttachment(Parent);
		P->SetRelativeLocation(Loc);
		P->RegisterComponent();
		return P;
	};
	auto Part = [&](USceneComponent* Parent, UStaticMesh* Mesh, const FVector& Loc, const FRotator& Rot, const FVector& Scale, const FLinearColor& Color, bool bMetal = false)
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetStaticMesh(Mesh);
		C->SetupAttachment(Parent);
		C->SetRelativeLocation(Loc);
		C->SetRelativeRotation(Rot);
		C->SetRelativeScale3D(Scale);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->RegisterComponent();
		if (UMaterialInstanceDynamic* M = C->CreateDynamicMaterialInstance(0, (bMetal && Metal) ? Metal : Base))
		{
			M->SetVectorParameterValue(TEXT("Color"), Color);
			if (bMetal && Metal) { M->SetScalarParameterValue(TEXT("Metallic"), 0.9f); M->SetScalarParameterValue(TEXT("Roughness"), 0.3f); }
		}
	};

	const bool bSmith = (Role == ENpcRole::Blacksmith);
	const bool bKnight = (Role == ENpcRole::GateKnight);
	const bool bBanker = (Role == ENpcRole::Banker);
	const bool bTroll = Folk == 1, bWizard = Folk == 2;   // (Steele Creek's trolls and wizards, SetFolk: 2026-11-18)
	const FLinearColor Skin = bTroll ? FLinearColor(0.38f, 0.47f, 0.36f) : bSmith ? FLinearColor(0.72f, 0.48f, 0.34f) : FLinearColor(0.82f, 0.60f, 0.45f);
	FLinearColor Tunic = bBanker ? FLinearColor(0.08f, 0.14f, 0.12f) : bSmith ? FLinearColor(0.30f, 0.30f, 0.33f) : (bKnight ? FLinearColor(0.60f, 0.62f, 0.68f) : FLinearColor(0.50f, 0.22f, 0.12f));
	const FLinearColor Pants = bKnight ? FLinearColor(0.40f, 0.42f, 0.48f) : FLinearColor(0.20f, 0.16f, 0.12f);
	const bool bQuest = (Role == ENpcRole::QuestGiver);   // (a quest giver: a villager's body in their own colours, QuestsPeople.csv)
	const bool bVill = (Role == ENpcRole::Villager) || bQuest;
	static const FLinearColor VTunic[JeebLore::NumVillagers] = {
		{0.35f,0.20f,0.45f,1.f}, {0.25f,0.45f,0.30f,1.f}, {0.40f,0.10f,0.12f,1.f}, {0.40f,0.32f,0.20f,1.f}, {0.80f,0.72f,0.55f,1.f},
		{0.18f,0.18f,0.20f,1.f}, {0.15f,0.35f,0.50f,1.f}, {0.45f,0.30f,0.15f,1.f}, {0.55f,0.18f,0.35f,1.f}, {0.50f,0.40f,0.28f,1.f} };
	static const FLinearColor VHair[JeebLore::NumVillagers] = {
		{0.75f,0.75f,0.78f,1.f}, {0.55f,0.35f,0.12f,1.f}, {0.80f,0.80f,0.82f,1.f}, {0.12f,0.08f,0.05f,1.f}, {0.85f,0.65f,0.25f,1.f},
		{0.70f,0.70f,0.72f,1.f}, {0.30f,0.15f,0.08f,1.f}, {0.20f,0.20f,0.22f,1.f}, {0.50f,0.20f,0.08f,1.f}, {0.40f,0.28f,0.15f,1.f} };
	const float Bulk = (bSmith ? 1.18f : 1.f) * (bTroll ? 1.22f : 1.f);
	if (bWizard) Tunic = Role == ENpcRole::Banker ? FLinearColor(0.10f, 0.12f, 0.36f) : FLinearColor(0.28f, 0.14f, 0.40f);   // a robe
	if (bVill)
	{
		if (bQuest) Tunic = Look.Tunic; else if (!bWizard) Tunic = VTunic[VillagerIdx];
		// (their size is set in Setup)
	}

	// Legs
	LegLPivot = Pivot(BodyRoot, FVector(0.f, -13.f, 84.f));
	LegRPivot = Pivot(BodyRoot, FVector(0.f, 13.f, 84.f));
	Part(LegLPivot, Cyl, FVector(0.f, 0.f, -42.f), FRotator::ZeroRotator, FVector(0.2f * Bulk, 0.2f * Bulk, 0.84f), Pants, bKnight);
	Part(LegRPivot, Cyl, FVector(0.f, 0.f, -42.f), FRotator::ZeroRotator, FVector(0.2f * Bulk, 0.2f * Bulk, 0.84f), Pants, bKnight);
	Part(LegLPivot, Cube, FVector(8.f, 0.f, -80.f), FRotator::ZeroRotator, FVector(0.34f, 0.2f, 0.1f), FLinearColor(0.12f, 0.08f, 0.05f));
	Part(LegRPivot, Cube, FVector(8.f, 0.f, -80.f), FRotator::ZeroRotator, FVector(0.34f, 0.2f, 0.1f), FLinearColor(0.12f, 0.08f, 0.05f));

	// Torso
	Part(BodyRoot, Cyl, FVector(0.f, 0.f, 112.f), FRotator::ZeroRotator, FVector(0.5f * Bulk, 0.38f * Bulk, 0.62f), Tunic, bKnight);
	Part(BodyRoot, Sphere, FVector(0.f, 0.f, 142.f), FRotator::ZeroRotator, FVector(0.54f * Bulk, 0.42f * Bulk, 0.2f), Tunic, bKnight);
	Part(BodyRoot, Cyl, FVector(0.f, 0.f, 84.f), FRotator::ZeroRotator, FVector(0.52f * Bulk, 0.4f * Bulk, 0.07f), FLinearColor(0.22f, 0.13f, 0.06f));   // belt

	// Arms (pivot at the shoulder)
	ArmLPivot = Pivot(BodyRoot, FVector(0.f, -30.f * Bulk, 140.f));
	ArmRPivot = Pivot(BodyRoot, FVector(0.f, 30.f * Bulk, 140.f));
	for (int32 sd = -1; sd <= 1; sd += 2)
	{
		USceneComponent* A = sd < 0 ? ArmLPivot : ArmRPivot;
		Part(A, Cyl, FVector(0.f, 0.f, -26.f), FRotator::ZeroRotator, FVector(0.15f * Bulk, 0.15f * Bulk, 0.55f), Tunic, bKnight);
		Part(A, Sphere, FVector(0.f, 0.f, -56.f), FRotator::ZeroRotator, FVector(0.14f, 0.14f, 0.14f), bKnight ? FLinearColor(0.5f, 0.5f, 0.55f) : Skin, bKnight);
	}

	// Head
	HeadPivot = Pivot(BodyRoot, FVector(0.f, 0.f, 152.f));
	Part(HeadPivot, Cyl, FVector(0.f, 0.f, -2.f), FRotator::ZeroRotator, FVector(0.14f, 0.14f, 0.1f), Skin);                          // neck
	Part(HeadPivot, Sphere, FVector(0.f, 0.f, 14.f), FRotator::ZeroRotator, FVector(0.30f, 0.27f, 0.32f), Skin);                      // head
	Part(HeadPivot, Sphere, FVector(13.f, 0.f, 12.f), FRotator::ZeroRotator, FVector(0.07f, 0.06f, 0.08f), Skin);                     // nose
	Part(HeadPivot, Sphere, FVector(12.f, -5.5f, 19.f), FRotator::ZeroRotator, FVector(0.045f, 0.05f, 0.06f), FLinearColor(0.02f, 0.02f, 0.02f));
	Part(HeadPivot, Sphere, FVector(12.f, 5.5f, 19.f), FRotator::ZeroRotator, FVector(0.045f, 0.05f, 0.06f), FLinearColor(0.02f, 0.02f, 0.02f));

	switch (Role)
	{
	case ENpcRole::Shopkeeper:
	{
		const FLinearColor Green(0.12f, 0.36f, 0.18f);
		Part(HeadPivot, Cyl, FVector(0.f, 0.f, 28.f), FRotator::ZeroRotator, FVector(0.44f, 0.44f, 0.03f), Green);     // hat brim
		Part(HeadPivot, Cyl, FVector(0.f, 0.f, 38.f), FRotator::ZeroRotator, FVector(0.26f, 0.26f, 0.2f), Green);      // hat crown
		Part(HeadPivot, Cyl, FVector(0.f, 0.f, 31.f), FRotator::ZeroRotator, FVector(0.27f, 0.27f, 0.03f), FLinearColor(0.9f, 0.7f, 0.15f)); // band
		Part(HeadPivot, Sphere, FVector(11.f, 0.f, 6.f), FRotator::ZeroRotator, FVector(0.14f, 0.2f, 0.06f), FLinearColor(0.78f, 0.76f, 0.7f)); // moustache
		Part(BodyRoot, Cube, FVector(22.f, 0.f, 105.f), FRotator::ZeroRotator, FVector(0.05f, 0.4f, 0.55f), FLinearColor(0.88f, 0.84f, 0.72f));  // apron
		break;
	}
	case ENpcRole::Blacksmith:
	{
		Part(HeadPivot, Sphere, FVector(8.f, 0.f, 2.f), FRotator::ZeroRotator, FVector(0.24f, 0.26f, 0.22f), FLinearColor(0.18f, 0.10f, 0.05f)); // beard
		Part(HeadPivot, Cyl, FVector(0.f, 0.f, 27.f), FRotator::ZeroRotator, FVector(0.3f, 0.3f, 0.03f), FLinearColor(0.12f, 0.10f, 0.10f));       // headband
		Part(BodyRoot, Cube, FVector(21.f, 0.f, 100.f), FRotator::ZeroRotator, FVector(0.06f, 0.42f, 0.7f), FLinearColor(0.16f, 0.10f, 0.06f));   // leather apron
		// Hammer in the right hand
		Part(ArmRPivot, Cyl, FVector(8.f, 0.f, -66.f), FRotator(90.f, 0.f, 0.f), FVector(0.05f, 0.05f, 0.4f), FLinearColor(0.35f, 0.2f, 0.08f));
		Part(ArmRPivot, Cube, FVector(30.f, 0.f, -66.f), FRotator::ZeroRotator, FVector(0.14f, 0.14f, 0.2f), FLinearColor(0.3f, 0.3f, 0.33f), true);
		break;
	}
	case ENpcRole::Banker:
	{
		Part(HeadPivot, Sphere, FVector(-3.f, 0.f, 20.f), FRotator::ZeroRotator, FVector(0.33f, 0.30f, 0.29f), FLinearColor(0.7f, 0.7f, 0.72f));   // grey hair
		if (bWizard) break;   // (a wizard banker wears his own hat, below)
		Part(HeadPivot, Cyl, FVector(0.f, 0.f, 28.f), FRotator::ZeroRotator, FVector(0.42f, 0.42f, 0.03f), FLinearColor(0.05f, 0.05f, 0.06f));      // top hat brim
		Part(HeadPivot, Cyl, FVector(0.f, 0.f, 44.f), FRotator::ZeroRotator, FVector(0.26f, 0.26f, 0.32f), FLinearColor(0.05f, 0.05f, 0.06f));      // top hat
		Part(HeadPivot, Cyl, FVector(0.f, 0.f, 33.f), FRotator::ZeroRotator, FVector(0.27f, 0.27f, 0.04f), FLinearColor(0.9f, 0.7f, 0.15f));        // gold band
		Part(HeadPivot, Cube, FVector(14.f, 0.f, 19.f), FRotator::ZeroRotator, FVector(0.03f, 0.22f, 0.05f), FLinearColor(0.9f, 0.7f, 0.15f));      // spectacles
		Part(BodyRoot, Cube, FVector(21.f, 0.f, 112.f), FRotator::ZeroRotator, FVector(0.04f, 0.1f, 0.5f), FLinearColor(0.9f, 0.7f, 0.15f));       // gold waistcoat stripe
		break;
	}
	case ENpcRole::Villager:
	case ENpcRole::QuestGiver:
	{
		const FLinearColor Hair = bQuest ? Look.Hair : VHair[VillagerIdx];
		Part(HeadPivot, Sphere, FVector(-3.f, 0.f, 20.f), FRotator::ZeroRotator, FVector(0.33f, 0.30f, 0.29f), Hair);   // hair
		if (bQuest || (!bWizard && !bTroll)) switch (bQuest ? Look.Hat : VillagerIdx)
		{
		case 0:   // Mabry: a crooked herbalist's hat
			Part(HeadPivot, Cyl, FVector(0.f, 0.f, 28.f), FRotator::ZeroRotator, FVector(0.46f, 0.46f, 0.03f), FLinearColor(0.25f, 0.12f, 0.35f));
			Part(HeadPivot, Cone, FVector(0.f, 0.f, 46.f), FRotator(0.f, 0.f, 12.f), FVector(0.26f, 0.26f, 0.4f), FLinearColor(0.25f, 0.12f, 0.35f));
			break;
		case 3: case 6:   // flat caps
			Part(HeadPivot, Cyl, FVector(0.f, 0.f, 28.f), FRotator::ZeroRotator, FVector(0.34f, 0.34f, 0.05f), FLinearColor(0.22f, 0.18f, 0.12f));
			break;
		case 7:   // Brother Ansel's cowl
			Part(HeadPivot, Sphere, FVector(-4.f, 0.f, 16.f), FRotator::ZeroRotator, FVector(0.40f, 0.38f, 0.40f), FLinearColor(0.35f, 0.28f, 0.2f));
			break;
		case 4:   // Wren: a white baker's cap
			Part(HeadPivot, Cyl, FVector(0.f, 0.f, 30.f), FRotator::ZeroRotator, FVector(0.3f, 0.3f, 0.16f), FLinearColor(0.95f, 0.95f, 0.92f));
			break;
		case 8:   // Peregrine: a feathered bard's hat
			Part(HeadPivot, Cyl, FVector(0.f, 0.f, 28.f), FRotator::ZeroRotator, FVector(0.4f, 0.4f, 0.03f), FLinearColor(0.2f, 0.4f, 0.25f));
			Part(HeadPivot, Cone, FVector(-8.f, 0.f, 44.f), FRotator(0.f, 0.f, -20.f), FVector(0.06f, 0.06f, 0.4f), FLinearColor(0.9f, 0.2f, 0.2f));
			break;
		default: break;
		}
		if (bQuest ? Look.bApron : (VillagerIdx == 5 || VillagerIdx == 9 || VillagerIdx == 2))   // aprons and shawls
			Part(BodyRoot, Cube, FVector(21.f, 0.f, 105.f), FRotator::ZeroRotator, FVector(0.05f, 0.4f, 0.5f), FLinearColor(0.82f, 0.78f, 0.68f));
		break;
	}
	default: // gate knight
	{
		Part(HeadPivot, Sphere, FVector(0.f, 0.f, 18.f), FRotator::ZeroRotator, FVector(0.36f, 0.33f, 0.38f), FLinearColor(0.62f, 0.64f, 0.70f), true);   // helm
		Part(HeadPivot, Cube, FVector(16.f, 0.f, 14.f), FRotator::ZeroRotator, FVector(0.04f, 0.28f, 0.08f), FLinearColor(0.02f, 0.02f, 0.03f));       // visor slit
		Part(HeadPivot, Cone, FVector(-4.f, 0.f, 44.f), FRotator::ZeroRotator, FVector(0.12f, 0.12f, 0.3f), FLinearColor(0.8f, 0.07f, 0.07f));         // plume
		Part(BodyRoot, Cube, FVector(24.f, 0.f, 105.f), FRotator::ZeroRotator, FVector(0.03f, 0.1f, 0.5f), FLinearColor(0.8f, 0.07f, 0.07f));          // tabard cross
		Part(BodyRoot, Cube, FVector(24.f, 0.f, 118.f), FRotator::ZeroRotator, FVector(0.03f, 0.34f, 0.1f), FLinearColor(0.8f, 0.07f, 0.07f));
		// Shield on the left arm and spear in the right hand
		Part(ArmLPivot, Cube, FVector(0.f, -14.f, -40.f), FRotator::ZeroRotator, FVector(0.08f, 0.5f, 0.7f), FLinearColor(0.9f, 0.9f, 0.86f));
		Part(ArmLPivot, Cube, FVector(5.f, -14.f, -40.f), FRotator::ZeroRotator, FVector(0.04f, 0.1f, 0.6f), FLinearColor(0.8f, 0.07f, 0.07f));
		Part(ArmRPivot, Cyl, FVector(0.f, 8.f, 20.f), FRotator::ZeroRotator, FVector(0.04f, 0.04f, 2.1f), FLinearColor(0.35f, 0.2f, 0.08f));
		Part(ArmRPivot, Cone, FVector(0.f, 8.f, 132.f), FRotator::ZeroRotator, FVector(0.08f, 0.08f, 0.3f), FLinearColor(0.8f, 0.82f, 0.88f), true);
		break;
	}
	}

	// Steele Creek's folk (2026-11-18): a troll stands taller and broader, with tusks and a heavy brow; a wizard has a tall pointed
	// hat, a long white beard and a robe to the ground
	if (bTroll)
	{
		BodyRoot->SetRelativeScale3D(FVector(1.16f));
		Part(HeadPivot, Cone, FVector(12.f, -6.f, 2.f), FRotator(0.f, 0.f, 0.f), FVector(0.035f, 0.035f, 0.09f), FLinearColor(0.92f, 0.88f, 0.76f));   // tusks
		Part(HeadPivot, Cone, FVector(12.f, 6.f, 2.f), FRotator(0.f, 0.f, 0.f), FVector(0.035f, 0.035f, 0.09f), FLinearColor(0.92f, 0.88f, 0.76f));
		Part(HeadPivot, Cube, FVector(11.f, 0.f, 23.f), FRotator(0.f, 0.f, 0.f), FVector(0.06f, 0.26f, 0.05f), Skin * 0.8f);                           // brow
		Part(HeadPivot, Sphere, FVector(0.f, -15.f, 16.f), FRotator::ZeroRotator, FVector(0.05f, 0.1f, 0.12f), Skin);                                // ears
		Part(HeadPivot, Sphere, FVector(0.f, 15.f, 16.f), FRotator::ZeroRotator, FVector(0.05f, 0.1f, 0.12f), Skin);
	}
	if (bWizard)
	{
		const FLinearColor Hat = Tunic * 0.85f, Star(0.95f, 0.80f, 0.30f);
		Part(HeadPivot, Cyl, FVector(0.f, 0.f, 27.f), FRotator::ZeroRotator, FVector(0.50f, 0.50f, 0.03f), Hat);                       // brim
		Part(HeadPivot, Cone, FVector(-2.f, 0.f, 52.f), FRotator(0.f, 0.f, -8.f), FVector(0.30f, 0.30f, 0.52f), Hat);                  // tall pointed crown
		Part(HeadPivot, Cyl, FVector(0.f, 0.f, 30.f), FRotator::ZeroRotator, FVector(0.31f, 0.31f, 0.03f), Star);                       // band
		Part(HeadPivot, Cone, FVector(10.f, 0.f, -4.f), FRotator(180.f, 0.f, 0.f), FVector(0.22f, 0.24f, 0.34f), FLinearColor(0.93f, 0.93f, 0.90f));   // beard
		Part(BodyRoot, Cone, FVector(0.f, 0.f, 50.f), FRotator::ZeroRotator, FVector(0.62f, 0.52f, 1.0f), Tunic);                     // robe to the ground
	}
}

TArray<FString> ANpc::MakeConversation(int32* OutLore)
{
	TArray<FString> Lines;
	const int32 V = Visits++;
	const bool bFirst = (V == 0);
	switch (Role)
	{
	case ENpcRole::Shopkeeper:
		Lines.Add(bFirst ? TEXT("Well met, traveller! Welcome to the Trading Post.") : TEXT("Back again! Welcome, welcome."));
		break;
	case ENpcRole::Blacksmith:
		Lines.Add(bFirst ? TEXT("Hmph. A customer. Mind the sparks.") : TEXT("Back at my forge, are you?"));
		break;
	case ENpcRole::GateKnight:
		Lines.Add(bFirst ? TEXT("Halt! Ah, a traveller. Forgive me, old habits.") : TEXT("Ah, you again. Well met."));
		break;
	case ENpcRole::Banker:
		Lines.Add(bFirst ? TEXT("Welcome to the vault, traveller. Everything you leave here is perfectly safe. Mostly.") : TEXT("Ah, back to the vault. Mind the stairs."));
		break;
	case ENpcRole::Townsfolk:
	{
		// (Townsfolk.csv: one of their own lines each visit, then a goodbye; no lore)
		TArray<FString> Mine;
		FirstHello.ParseIntoArray(Mine, TEXT("|"), true);
		Lines.Add(Mine.Num() > 0 ? Mine[V % Mine.Num()].TrimStartAndEnd() : FString(TEXT("Good day to you.")));
		static const TCHAR* Bye[] = { TEXT("Well, I'd best be getting on."), TEXT("Mind how you go."), TEXT("Good day, traveller.") };
		Lines.Add(Bye[V % UE_ARRAY_COUNT(Bye)]);
		return Lines;
	}
	default:
	{
		static const TCHAR* Hello[] = { TEXT("Oh! Hello there."), TEXT("Well, well. Come to listen?"), TEXT("Sit a moment, if you like."), TEXT("You look like someone who asks questions."), TEXT("Mind how you go, traveller.") };
		Lines.Add(bFirst ? TEXT("Oh! A new face. Hello there.") : Hello[V % UE_ARRAY_COUNT(Hello)]);
		break;
	}
	}

	if (!FirstHello.IsEmpty() && bFirst && Lines.Num() > 0) Lines[0] = FirstHello;   // (Steele Creek's people greet in their own words, SetFolk)

	// This visit's fragment of the lore; it goes into the Lore tab
	const TArray<int32> Mine = JeebLore::BySpeaker(SpeakerIdx());
	if (Mine.Num() > 0)
	{
		const int32 Idx = Mine[V % Mine.Num()];
		const JeebLore::FEntry& E = JeebLore::Entries()[Idx];
		TArray<FString> Parts;
		FString(E.Text).ParseIntoArray(Parts, TEXT("|"), true);
		for (const FString& P : Parts) Lines.Add(P);
		if (OutLore) *OutLore = Idx;   // the listener asks the server to note it down (EInvOp::Lore)
	}

	switch (Role)
	{
	case ENpcRole::Shopkeeper: Lines.Add(TEXT("Now then: sell me what you have gathered, or take a look at my wares.")); break;
	case ENpcRole::Blacksmith: Lines.Add(TEXT("Bring me the materials and I'll make you something worth carrying.")); break;
	case ENpcRole::GateKnight: Lines.Add(TEXT("Stay on the road and keep your shield high. Safe travels.")); break;
	case ENpcRole::Banker:     Lines.Add(TEXT("Now then: what shall we put away, and what shall we bring back up?")); break;
	default:
	{
		static const TCHAR* Bye[] = { TEXT("That is all I will say. For now."), TEXT("Mind you do not repeat that too loudly."), TEXT("Come back. I may remember more."), TEXT("Safe roads, traveller.") };
		Lines.Add(Bye[V % UE_ARRAY_COUNT(Bye)]);
		break;
	}
	}
	return Lines;
}

void ANpc::SetRound(const TSharedPtr<JeebLife::FRound>& InRound)
{
	Round = InRound;
	Lag = 0.f;
	if (!Round.IsValid() || Round->Segs.Num() == 0) return;
	// (where the clock has them now, at once: nobody walks across town from where they were spawned)
	const JeebLife::FPose P = JeebLife::Eval(*Round, JeebLife::Now(GetWorld()), false);
	SetActorLocation(FVector(P.At.X, P.At.Y, JeebLife::DrawnGround(P.At.X, P.At.Y) + HalfH));
	SetActorRotation(FRotator(0.f, P.Yaw, 0.f));
	HomeYaw = P.Yaw;
	bFootSet = false;
	TraceAt = FVector2D(1.0e9f, 1.0e9f);
	LastLoc = GetActorLocation();
}

float ANpc::RoamSlack() const { return Round.IsValid() ? Round->Roam : 0.f; }
FVector2D ANpc::RoamAnchor() const { return Round.IsValid() ? Round->Anchor : FVector2D(GetActorLocation()); }

void ANpc::RebuildBody()
{
	ClearBody();
	BuildBody();
}

// Far from every player a person costs next to nothing (2026-11-18): every frame within 45 m of your knight, ten times a second to
// 90 m (where the body stops being drawn), then once a second; on a dedicated server four times a second with a player within 60 m
// (the server only needs them for its reach checks), else every 2 s
void ANpc::UpdateTickRate(float NearestPlayerCm)
{
	const bool bDedi = GetNetMode() == NM_DedicatedServer;
	const float Want = bDedi ? (NearestPlayerCm < 6000.f ? 0.25f : 2.f) : (NearestPlayerCm < 4500.f ? 0.f : NearestPlayerCm < 9000.f ? 0.1f : 1.f);
	if (Want != TickPlan) { TickPlan = Want; SetActorTickInterval(Want); }
}

// (2026-11-18, owner: "The NPCs feet are under the path") the ground a person stands on: the ground AS DRAWN there (Steele Creek and
// the open world are drawn by the world's 8 m mesh, whose flat triangles sit up to a few dozen cm off the exact height they were
// placed by), and near your knight a short trace down onto whatever solid floor is there (steps, porches, the bridge's deck), only
// when they have moved 35 cm or every 0.3 s while walking. The road ribbons have no collision and lie 2-5 cm over the drawn ground.
float ANpc::GroundUnder(const FVector2D& P, bool bTrace, float DeltaSeconds)
{
	const float G = JeebLife::DrawnGround(P.X, P.Y);
	if (!bTrace) { TraceOff = 0.f; TraceAt = FVector2D(1.0e9f, 1.0e9f); return G; }
	TraceAge += DeltaSeconds;
	if (FVector2D::DistSquared(P, TraceAt) > FMath::Square(35.f) || (bPoseWalk && TraceAge > 0.3f))
	{
		TraceAge = 0.f;
		TraceAt = P;
		TraceOff = 0.f;
		FHitResult Hit;
		FCollisionQueryParams Q(SCENE_QUERY_STAT(NpcGround), false, this);
		if (GetWorld()->LineTraceSingleByObjectType(Hit, FVector(P.X, P.Y, G + 90.f), FVector(P.X, P.Y, G - 60.f), FCollisionObjectQueryParams(ECC_WorldStatic), Q))
		{
			const float Off = float(Hit.ImpactPoint.Z) - G;
			if (Off > -50.f && Off < 35.f) TraceOff = Off;   // (a floor or a step, never a counter or a prop's top: those are 60 cm and more)
		}
	}
	return G + TraceOff;
}

// While you shop (owner 2026-11-20: "they move their arms too much ... the same open hand animation keeps playing over and over"): a keeper
// greets you with the talk gesture once as the menu opens, then stands in the calm idle, with one more gesture every 13 to 19 s (each keeper
// its own spacing, from the name). Anyone else who talks (stories, quests) keeps the talk for the whole conversation, as before. Looks only.
bool ANpc::KeeperGestures() const
{
	if (!bTalking) return false;
	if (!OffersTrade()) return true;
	float Len = 2.5f;
	if (ModelDef && ModelDef->Clips[2]) Len = ModelDef->Clips[2]->GetPlayLength();
	const float G = FMath::Clamp(Len, 1.5f, 3.5f) - 0.15f;   // once through the clip (the cross-fade adds 0.3 s each way)
	if (TalkT < G) return true;
	const uint32 Seed = GetTypeHash(GetNpcName());
	const float Every = 13.f + float(Seed % 7u);
	const float Since = FMath::Fmod(TalkT - G, Every);
	return Since > Every - G;
}

void ANpc::Alarm(const FVector& LookAt, float Seconds, float BubbleSeconds, const FString& Line, bool bShout)
{
	const double Now = GetWorld()->GetTimeSeconds();
	AlarmLook = LookAt;
	AlarmUntil = Now + Seconds;
	BubbleUntil = Line.IsEmpty() ? -1.0 : Now + BubbleSeconds;
	AlarmLine = Line;
	bAlarmShout = bShout;
	SetActorTickInterval(0.f);   // (a far one may tick slowly: turn smoothly now)
}

bool ANpc::AlarmShown(FString& OutLine, bool& bOutShout, float& OutBubble, float& OutMark) const
{
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now >= AlarmUntil) return false;
	OutLine = Now < BubbleUntil ? AlarmLine : FString();
	bOutShout = bAlarmShout;
	OutBubble = FMath::Clamp(float(BubbleUntil - Now) / 0.4f, 0.f, 1.f);
	OutMark = FMath::Clamp(float(AlarmUntil - Now) / 0.6f, 0.f, 1.f);
	return true;
}

void ANpc::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	AnimT += DeltaSeconds;
	TalkT = bTalking ? TalkT + DeltaSeconds : 0.f;
	if (BuiltSetting != JeebPeople::Setting() && !IsRunningDedicatedServer()) { ClearBody(); BuildBody(); }   // (jeeb.Npc.Models changed)

	const bool bDedi = GetNetMode() == NM_DedicatedServer;
	// Your own knight (looks only: which way they face and when they wait for you; never used for gameplay), and on a dedicated
	// server the nearest of all players (only to decide how often to tick)
	APawn* Player = bDedi ? nullptr : UGameplayStatics::GetPlayerPawn(this, 0);
	const FVector Me = GetActorLocation();
	float DistToPlayer = 1e9f;
	FVector ToPlayer = FVector::ForwardVector;
	if (Player)
	{
		ToPlayer = Player->GetActorLocation() - Me;
		DistToPlayer = ToPlayer.Size2D();
	}
	float Nearest = DistToPlayer;
	if (bDedi)
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			if (const APawn* P = It->Get() ? It->Get()->GetPawn() : nullptr) Nearest = FMath::Min(Nearest, float(FVector::Dist2D(P->GetActorLocation(), Me)));
	UpdateTickRate(Nearest);

	// A beat set the old way (SetPatrol: the test lineups) becomes a round on the clock; the towns' people get theirs from ATownLife
	if (!Round.IsValid() && Patrol.Num() > 1)
		SetRound(JeebLife::MakeBeat(Patrol, 4.f, Role == ENpcRole::Villager || Role == ENpcRole::QuestGiver ? 95.f : 130.f, GetTypeHash(GetNpcName()), false));

	// Keepers, knights and quest givers turn to you from further off; villagers only when you come up to them
	const float FaceR = (OffersTrade() || Role == ENpcRole::GateKnight || Role == ENpcRole::QuestGiver) ? 650.f : 320.f;
	const bool bNear = DistToPlayer < FaceR;
	bool bWalkNow = false, bTurnNow = false, bChat = false, bChatTalk = false;
	FVector2D ChatAt = FVector2D::ZeroVector;
	FVector2D At(Me.X, Me.Y);

	if (Round.IsValid() && Round->Segs.Num() > 0)
	{
		const double Clock = JeebLife::Now(GetWorld());
		// Holding still (looks only, never on a dedicated server): while you talk to them, or when you stand right in their way.
		// They catch up afterwards, mostly by cutting their next stays short, a little by walking faster.
		bool bHold = !bDedi && (bTalking || GetWorld()->GetTimeSeconds() < AlarmUntil);   // (or alarmed: Guards.h)
		if (!bDedi && !bHold && bPoseWalk && Player && DistToPlayer < 130.f)
			bHold = FVector2D::DotProduct(FVector2D(ToPlayer.X, ToPlayer.Y).GetSafeNormal(), FVector2D(GetActorForwardVector().X, GetActorForwardVector().Y)) > 0.3f;
		if (bHold) Lag = FMath::Min(Lag + DeltaSeconds, 600.f);
		else if (Lag > 0.f) Lag = FMath::Max(0.f, Lag - DeltaSeconds * (bPoseWalk ? 0.35f : 1.5f));
		const JeebLife::FPose P = JeebLife::Eval(*Round, Clock - Lag);
		bWalkNow = P.bWalk && !bHold;
		bTurnNow = P.bTurn && !bHold;
		bChat = P.bChat;
		ChatAt = P.ChatAt;
		if (bChat)
		{
			// (the two take turns, about five seconds each, so they don't both talk at once; who goes first by their names)
			const TSharedPtr<JeebLife::FRound> Other = Round->Partner.Pin();
			const bool bFirst = !Other.IsValid() || Round->Label < Other->Label;
			bChatTalk = ((FMath::FloorToInt(float(Clock) / 5.f) & 1) == 0) == bFirst;
		}
		At = P.At;
		HomeYaw = P.Yaw;
		if (bWalkNow || bTurnNow) SetActorRotation(FMath::RInterpTo(GetActorRotation(), FRotator(0.f, P.Yaw, 0.f), DeltaSeconds, 14.f));
	}
	bPoseWalk = bWalkNow;

	// Standing on the ground as drawn (and on floors near you), eased so steps and bridge ends don't jolt
	{
		const float Ground = GroundUnder(At, !bDedi && DistToPlayer < 4500.f, DeltaSeconds);
		if (!bFootSet || FMath::Abs(FootZ - Ground) > 150.f) { FootZ = Ground; bFootSet = true; }
		else FootZ = FMath::FInterpTo(FootZ, Ground, DeltaSeconds, 12.f);
		const FVector NewLoc(At.X, At.Y, FootZ + HalfH);
		if (!NewLoc.Equals(Me, 0.1f)) SetActorLocation(NewLoc);
	}
	WalkBlend = FMath::FInterpTo(WalkBlend, bWalkNow ? 1.f : 0.f, DeltaSeconds, 6.f);

	// Facing, when not walking: you when you are close or talking to them, their partner while they chat, else their round's way
	if (!bWalkNow && !bTurnNow)
	{
		float WantYaw = HomeYaw;
		if (bChat && !ChatAt.IsZero()) WantYaw = FVector(ChatAt.X - At.X, ChatAt.Y - At.Y, 0.f).Rotation().Yaw;
		if (bTalking || (bNear && !(bChat && DistToPlayer > 220.f))) WantYaw = FVector(ToPlayer.X, ToPlayer.Y, 0.f).Rotation().Yaw;
		const bool bAlarmed = !bDedi && GetWorld()->GetTimeSeconds() < AlarmUntil;   // (the alarm: they look at the thief, quickly)
		if (bAlarmed) WantYaw = FVector(AlarmLook.X - At.X, AlarmLook.Y - At.Y, 0.f).Rotation().Yaw;
		SetActorRotation(FMath::RInterpConstantTo(GetActorRotation(), FRotator(0.f, WantYaw, 0.f), DeltaSeconds, bTalking ? 180.f : bAlarmed ? 300.f : 90.f));
	}

	// The head's turn toward you: up to 55 degrees, and back to straight ahead as you go round behind them (2026-11-18, motion review: the
	// clamp on a signed angle flipped from +55 to -55 as you crossed behind, a 110-degree whip in 0.4 s). By the size of the angle, so it
	// is the same either side and nothing wraps; NpcBody and the head below still ease it.
	auto HeadWant = [](float Rel) { return FMath::Clamp(Rel, -55.f, 55.f) * (1.f - FMath::SmoothStep(95.f, 125.f, FMath::Abs(Rel))); };

	// A skinned body (NpcBody.h): idle, walk (its feet kept to the ground's speed) or talk, the head following you a little
	if (Body)
	{
		const FVector Now = GetActorLocation();
		const float Moved = DeltaSeconds > 0.f ? FVector::Dist2D(Now, LastLoc) / DeltaSeconds : 0.f;
		LastLoc = Now;
		MoveSpeed = FMath::FInterpTo(MoveSpeed, FMath::Min(Moved, 400.f), DeltaSeconds, 8.f);
		const float Rel = FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw, FVector(ToPlayer.X, ToPlayer.Y, 0.f).Rotation().Yaw);
		const bool bShouting = GetWorld()->GetTimeSeconds() < BubbleUntil && bAlarmShout;   // (the alarm's shout: they gesture as they talk)
		const int32 State = (KeeperGestures() || bChatTalk || bShouting) ? 2 : (WalkBlend > 0.35f ? 1 : 0);   // (a keeper mostly idles while you shop: KeeperGestures)
#if !UE_BUILD_SHIPPING
		if (bTalking && OffersTrade())
		{
			static TMap<FString, int32> Last;
			int32& L = Last.FindOrAdd(GetNpcName(), -1);
			if (L != State) { L = State; UE_LOG(LogTemp, Log, TEXT("JEEBKEEPER %s %s at %.1f s into the talk"), *GetNpcName(), State == 2 ? TEXT("gesture") : TEXT("idle"), TalkT); }
		}
#endif
		const float Rate = FMath::Clamp(MoveSpeed / (ModelDef->WalkSpeed * GetActorScale3D().X), 0.4f, 2.f);
		if (UNpcAnimInstance* A = Cast<UNpcAnimInstance>(Body->GetAnimInstance()))
			A->Drive(State, Rate, (bNear && State != 1) ? HeadWant(Rel) : 0.f);
#if !UE_BUILD_SHIPPING
		static const bool bLog = FParse::Param(FCommandLine::Get(), TEXT("PeopleLog"));   // (test: JEEBNPCMOVE ten times a second)
		if (bLog && !TestModel.IsEmpty() && FMath::FloorToInt(AnimT * 10.f) != FMath::FloorToInt((AnimT - DeltaSeconds) * 10.f))
		{
			UE_LOG(LogTemp, Log, TEXT("JEEBNPCMOVE %s t %.1f yaw %.0f speed %.0f state %d turn %d lag %.1f rel %.0f headwant %.0f dist %.0f footz %.1f"),
				*TestModel, AnimT, GetActorRotation().Yaw, Moved, State, bTurnNow ? 1 : 0, Lag, Rel, (bNear && State != 1) ? HeadWant(Rel) : 0.f, DistToPlayer, FootZ);
		}
#endif
		BodyRoot->SetRelativeLocation(FVector(0.f, 0.f, -92.f - ModelDef->WalkSink * WalkBlend));
		// a shadow only within 30 m of the player (the far ones cost the most and show the least)
		const bool bShadow = DistToPlayer < 3000.f;
		if (bShadow != bShadowOn) { bShadowOn = bShadow; Body->SetCastShadow(bShadow); }
		return;
	}
	if (bDedi) return;   // (a dedicated server's shapes are never seen)

	// Head follows you a little
	if (HeadPivot)
	{
		const float Rel = FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw, FVector(ToPlayer.X, ToPlayer.Y, 0.f).Rotation().Yaw);
		const float Yaw = bNear ? HeadWant(Rel) : 0.f;
		HeadPivot->SetRelativeRotation(FMath::RInterpTo(HeadPivot->GetRelativeRotation(), FRotator(0.f, Yaw, 0.f), DeltaSeconds, 5.f));
	}

	// Arms and legs
	const float Sw = FMath::Sin(AnimT * 5.f) * 30.f * WalkBlend;
	if (LegLPivot) LegLPivot->SetRelativeRotation(FRotator(Sw, 0.f, 0.f));
	if (LegRPivot) LegRPivot->SetRelativeRotation(FRotator(-Sw, 0.f, 0.f));
	float ArmR = -Sw * 0.7f, ArmL = Sw * 0.7f;
	if (Role == ENpcRole::Blacksmith)
	{
		if (!bNear && !bTalking)
		{
			// Hammering at the anvil
			const float H = FMath::Fmod(AnimT, 1.3f) / 1.3f;
			ArmR = (H < 0.6f) ? FMath::Lerp(20.f, 110.f, FMath::Sin(H / 0.6f * PI * 0.5f)) : FMath::Lerp(110.f, 20.f, FMath::Pow((H - 0.6f) / 0.4f, 3.f));
			ArmR = -ArmR + 90.f; // 90 = forearm forward
		}
		else ArmR = 25.f;
	}
	else if (Role == ENpcRole::Shopkeeper)
	{
		ArmR = 0.f; ArmL = 0.f;
		if (KeeperGestures()) ArmR = 35.f + 12.f * FMath::Sin(AnimT * 3.f);   // a gesture as he greets you, then now and then
	}
	if (ArmRPivot) ArmRPivot->SetRelativeRotation(FRotator(ArmR, 0.f, 0.f));
	if (ArmLPivot) ArmLPivot->SetRelativeRotation(FRotator(ArmL, 0.f, 0.f));
	if (BodyRoot) BodyRoot->SetRelativeLocation(FVector(0.f, 0.f, -92.f + 1.2f * FMath::Sin(AnimT * 2.f)));
}

#if !UE_BUILD_SHIPPING
// (test pictures, 2026-11-18: -PeopleLineup=<yaw> puts one of each body in a row in front of the knight (-PeopleAtX, 450 cm), facing <yaw>
// (180 toward him); -PeopleWalk sends them back and forth across his view (see below); -PeopleTalk holds them in the talk; -PeopleModels=A,B,... picks the bodies)
namespace
{
	struct FPeopleLineup
	{
		FPeopleLineup()
		{
			FWorldDelegates::OnPostWorldInitialization.AddLambda([](UWorld* W, const UWorld::InitializationValues)
			{
				if (!W || !W->IsGameWorld()) return;
				// (-PeopleTalkAll: everyone in the talk from 3 s; -PeopleBeside=<quest key>: that person stands 2.2 m to the knight's right
				// (+Y) where he starts, facing him, so his name tag and their quest mark show)
				FString Beside;
				const bool bTalkAll = FParse::Param(FCommandLine::Get(), TEXT("PeopleTalkAll"));
				if (bTalkAll || FParse::Value(FCommandLine::Get(), TEXT("PeopleBeside="), Beside))
					W->OnWorldBeginPlay.AddLambda([W, bTalkAll, Beside]()
					{
						FTimerHandle H;
						W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([W, bTalkAll, Beside]()
						{
							const APawn* Me = UGameplayStatics::GetPlayerPawn(W, 0);
							for (TActorIterator<ANpc> It(W); It; ++It)
							{
								if (bTalkAll) It->SetTalking(true);
								if (Me && !Beside.IsEmpty() && It->GetPersonKey() == FName(*Beside))
								{
									const FVector At = Me->GetActorLocation() + FVector(0.f, 220.f, 0.f);
									It->SetActorLocation(FVector(At.X, At.Y, It->GetActorLocation().Z));
									It->SetActorRotation(FRotator(0.f, -90.f, 0.f));
								}
							}
						}), 3.f, false);
					});
				float Yaw = 0.f;
				if (!FParse::Value(FCommandLine::Get(), TEXT("PeopleLineup="), Yaw)) return;
				// (2026-11-18, motion review: the row was placed at fixed world spots, so MoveLab showed nobody and the Field's people stood far
				// off. Now it is placed from wherever the knight is, 1 s after play starts: PeopleAtX ahead of him (default 450 cm), PeopleAtY to
				// his right, on the ground under each spot; Yaw is from the knight's own facing (180 faces him). -PeopleWalk sends each one back
				// and forth across his view (-PeopleWalkLen=<cm>, 500), so every turn at the ends is a 180-degree corner, and they keep walking
				// while the knight is further than -PeopleWalkNear=<cm> (100) away.)
				W->OnWorldBeginPlay.AddLambda([W, Yaw]()
				{
					FTimerHandle H;
					W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([W, Yaw]()
					{
						FString List = TEXT("Townsman,Townswoman,Elder,Smith,Banker,Guard");
						FParse::Value(FCommandLine::Get(), TEXT("PeopleModels="), List, false);
						TArray<FString> Names;
						List.ParseIntoArray(Names, TEXT(","));
						const bool bWalk = FParse::Param(FCommandLine::Get(), TEXT("PeopleWalk")), bTalk = FParse::Param(FCommandLine::Get(), TEXT("PeopleTalk"));
						float X0 = 450.f, Y0 = 0.f, WalkLen = 500.f, WalkNear = 100.f;
						FParse::Value(FCommandLine::Get(), TEXT("PeopleAtX="), X0);
						FParse::Value(FCommandLine::Get(), TEXT("PeopleAtY="), Y0);
						FParse::Value(FCommandLine::Get(), TEXT("PeopleWalkLen="), WalkLen);
						FParse::Value(FCommandLine::Get(), TEXT("PeopleWalkNear="), WalkNear);
						const APawn* Me = UGameplayStatics::GetPlayerPawn(W, 0);
						const FVector Base = Me ? Me->GetActorLocation() : FVector(0.f, 0.f, JeebTerrain::HeightAt(0.f, 0.f) + 92.f);
						const FRotator Face(0.f, Me ? Me->GetActorRotation().Yaw : 0.f, 0.f);
						const FVector Fwd = Face.Vector(), Right = FRotationMatrix(Face).GetUnitAxis(EAxis::Y);
						for (int32 i = 0; i < Names.Num(); ++i)
						{
							const FVector Spot = Base + Fwd * X0 + Right * (Y0 + (i - (Names.Num() - 1) * 0.5f) * 170.f);
							FHitResult Hit;
							FCollisionQueryParams Q(SCENE_QUERY_STAT(PeopleLineup), false, Me);
							const float Gz = W->LineTraceSingleByChannel(Hit, FVector(Spot.X, Spot.Y, Base.Z + 300.f), FVector(Spot.X, Spot.Y, Base.Z - 1500.f), ECC_Visibility, Q) ? float(Hit.ImpactPoint.Z) : float(Base.Z) - 90.f;
							FActorSpawnParameters SP;
							SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
							ANpc* N = W->SpawnActor<ANpc>(FVector(Spot.X, Spot.Y, Gz + 92.f), FRotator(0.f, Face.Yaw + Yaw, 0.f), SP);
							if (!N) continue;
							N->TestModel = Names[i];
							FNpcLook L; L.Name = Names[i];
							N->SetupPerson(FName(*(TEXT("Lineup") + Names[i])), L);
							if (bWalk)
							{
								const FVector End = Spot - Right * WalkLen;
								N->SetPatrol({ FVector2D(Spot.X, Spot.Y), FVector2D(End.X, End.Y) });
								N->TestWalkNear = WalkNear;
							}
							if (bTalk) N->SetTalking(true);
							UE_LOG(LogTemp, Log, TEXT("JEEBPEOPLE lineup %s at %.0f,%.0f,%.0f (knight at %.0f,%.0f facing %.0f)"), *Names[i], Spot.X, Spot.Y, Gz, Base.X, Base.Y, Face.Yaw);
						}
					}), 1.f, false);
				});
			});
		}
	} GPeopleLineup;
}
#endif

// (2026-11-18 review: the name tag and quest mark floated over the new bodies) the top of the head of whatever body is shown
float ANpc::HeadTopZ() const
{
	if (Body)
		if (const USkeletalMesh* M = Body->GetSkeletalMeshAsset())
		{
			const FBoxSphereBounds B = M->GetImportedBounds();   // (the bind pose: arms out, so its top is the top of the head or hat)
			return Body->GetComponentLocation().Z + (B.Origin.Z + B.BoxExtent.Z) * Body->GetComponentScale().Z;
		}
	const float Sz = GetActorScale3D().Z * (BodyRoot ? BodyRoot->GetRelativeScale3D().Z : 1.f);
	return GetActorLocation().Z + 105.f * Sz;   // (the old shapes: where their name tag always sat, less its 15 cm)
}

#include "Npc.h"
#include "Lore.h"
#include "SkillComponent.h"
#include "Terrain.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "NpcBody.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Pawn.h"
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
	default:                   return 2;
	}
}

void ANpc::Setup(ENpcRole InRole, int32 InVillagerIdx)
{
	Role = InRole;
	VillagerIdx = FMath::Clamp(InVillagerIdx, 0, JeebLore::NumVillagers - 1);
	HomeYaw = GetActorRotation().Yaw;

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
	static const TCHAR* RoleKeys[] = { TEXT("shopkeeper"), TEXT("blacksmith"), TEXT("gateknight"), TEXT("villager"), TEXT("banker"), TEXT("questgiver") };
	FString RoleKey = RoleKeys[FMath::Clamp(int32(Role), 0, 5)];
	if (Role == ENpcRole::Villager) RoleKey += FString::FromInt(VillagerIdx);
	const FString FolkKey = Folk == 1 ? TEXT("troll") : Folk == 2 ? TEXT("wizard") : TEXT("");
	FString Short;
	GetNpcName().Split(TEXT(","), &Short, nullptr);   // ("Sir Dunmore, the Trader": also "Sir Dunmore")
	TArray<FString> Keys = { GetNpcName(), Short, PersonKey.IsNone() ? FString() : PersonKey.ToString() };
	if (!FolkKey.IsEmpty()) { Keys.Add(FolkKey + TEXT(":") + RoleKey); Keys.Add(FolkKey); }
	Keys.Add(RoleKey);
	JeebPeople::FWho Who = JeebPeople::Find(Keys);
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
		Lines.Add(bFirst ? TEXT("Well met, traveller! Welcome to the Blackrock Trading Post.") : TEXT("Back again! Welcome, welcome."));
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

void ANpc::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	AnimT += DeltaSeconds;
	if (BuiltSetting != JeebPeople::Setting() && !IsRunningDedicatedServer()) { ClearBody(); BuildBody(); }   // (jeeb.Npc.Models changed)

	APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	const FVector Me = GetActorLocation();
	float DistToPlayer = 1e9f;
	FVector ToPlayer = FVector::ForwardVector;
	if (Player)
	{
		ToPlayer = Player->GetActorLocation() - Me;
		DistToPlayer = ToPlayer.Size2D();
	}
	const bool bNear = DistToPlayer < 650.f;
	float Walk = 0.f;
	bool bTurnOnSpot = false;   // (turning onto the next leg of the beat before walking it)

	// Anyone with a beat (the gate knight, the villagers) walks it slowly and stops to talk
	if (Patrol.Num() > 1 && !bTalking && DistToPlayer > (TestWalkNear >= 0.f ? TestWalkNear : Role == ENpcRole::Villager ? 260.f : 650.f))
	{
		const FVector2D P(Me.X, Me.Y);
		const FVector2D Target = Patrol[PatrolIdx % Patrol.Num()];
		const FVector2D To = Target - P;
		const float LegYaw = FVector(To.X, To.Y, 0.f).Rotation().Yaw;
		if (PauseT > 0.f)
		{
			// (2026-11-18, motion review: at a corner they used to set off at full speed while still turning, walking sideways or backwards
			// for over a second. They turn toward the next leg during the pause, and walk only once facing within 30 degrees of it.)
			PauseT -= DeltaSeconds;
			if (!To.IsNearlyZero()) HomeYaw = LegYaw;
		}
		else
		{
			if (To.Size() < 60.f) { ++PatrolIdx; PauseT = (Role == ENpcRole::Villager) ? FMath::FRandRange(2.f, 7.f) : 4.f; StuckT = 0.f; }
			else if (FMath::Abs(FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw, LegYaw)) > 30.f)
			{
				SetActorRotation(FMath::RInterpConstantTo(GetActorRotation(), FRotator(0.f, LegYaw, 0.f), DeltaSeconds, 120.f));
				HomeYaw = LegYaw;
				bTurnOnSpot = true;
			}
			else
			{
				const FVector2D Dir = To.GetSafeNormal();
				const float Speed = (Role == ENpcRole::Villager) ? 95.f : 130.f;
				FVector NewLoc(Me.X + Dir.X * Speed * DeltaSeconds, Me.Y + Dir.Y * Speed * DeltaSeconds, 0.f);
				NewLoc.Z = JeebTerrain::HeightAt(NewLoc.X, NewLoc.Y) + HalfH;
				SetActorLocation(NewLoc, true);
				const FVector After = GetActorLocation();
				if (FVector2D::Distance(FVector2D(After.X, After.Y), P) < Speed * DeltaSeconds * 0.3f)
				{
					StuckT += DeltaSeconds;
					if (StuckT > 1.2f) { ++PatrolIdx; StuckT = 0.f; }   // blocked by something: head for the next point
				}
				else StuckT = 0.f;
				const FRotator Want(0.f, FVector(Dir.X, Dir.Y, 0.f).Rotation().Yaw, 0.f);
				SetActorRotation(FMath::RInterpConstantTo(GetActorRotation(), Want, DeltaSeconds, 120.f));
				HomeYaw = GetActorRotation().Yaw;
				Walk = 1.f;
			}
		}
	}
	WalkBlend = FMath::FInterpTo(WalkBlend, Walk, DeltaSeconds, 6.f);

	// Turn to face you when you are close, otherwise back to their own direction
	const float WantYaw = bNear ? FVector(ToPlayer.X, ToPlayer.Y, 0.f).Rotation().Yaw : HomeYaw;
	if (Walk < 0.5f && !bTurnOnSpot)
	{
		SetActorRotation(FMath::RInterpConstantTo(GetActorRotation(), FRotator(0.f, WantYaw, 0.f), DeltaSeconds, bTalking ? 180.f : 70.f));
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
		const int32 State = bTalking ? 2 : (WalkBlend > 0.35f ? 1 : 0);
		const float Rate = FMath::Clamp(MoveSpeed / (ModelDef->WalkSpeed * GetActorScale3D().X), 0.4f, 2.f);
		if (UNpcAnimInstance* A = Cast<UNpcAnimInstance>(Body->GetAnimInstance()))
			A->Drive(State, Rate, (bNear && State != 1) ? HeadWant(Rel) : 0.f);
#if !UE_BUILD_SHIPPING
		static const bool bLog = FParse::Param(FCommandLine::Get(), TEXT("PeopleLog"));   // (test: JEEBNPCMOVE ten times a second)
		if (bLog && !TestModel.IsEmpty() && FMath::FloorToInt(AnimT * 10.f) != FMath::FloorToInt((AnimT - DeltaSeconds) * 10.f))
		{
			const FVector2D Leg = Patrol.Num() > 1 ? Patrol[PatrolIdx % Patrol.Num()] - FVector2D(Now.X, Now.Y) : FVector2D::ZeroVector;
			const float LegOff = Leg.IsNearlyZero() ? 0.f : FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw, FVector(Leg.X, Leg.Y, 0.f).Rotation().Yaw);
			UE_LOG(LogTemp, Log, TEXT("JEEBNPCMOVE %s t %.1f yaw %.0f speed %.0f state %d legoff %.0f turnspot %d pause %.1f rel %.0f headwant %.0f dist %.0f"),
				*TestModel, AnimT, GetActorRotation().Yaw, Moved, State, LegOff, bTurnOnSpot ? 1 : 0, PauseT, Rel, (bNear && State != 1) ? HeadWant(Rel) : 0.f, DistToPlayer);
		}
#endif
		BodyRoot->SetRelativeLocation(FVector(0.f, 0.f, -92.f - ModelDef->WalkSink * WalkBlend));
		// a shadow only within 30 m of the player (the far ones cost the most and show the least)
		const bool bShadow = DistToPlayer < 3000.f;
		if (bShadow != bShadowOn) { bShadowOn = bShadow; Body->SetCastShadow(bShadow); }
		return;
	}

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
		if (bTalking) ArmR = 35.f + 12.f * FMath::Sin(AnimT * 3.f);   // a gesture while he talks
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

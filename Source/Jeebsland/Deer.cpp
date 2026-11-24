#include "Deer.h"
#include "JeebTuning.h"
#include "FoeData.h"
#include "FoeCamps.h"
#include "HealthComponent.h"
#include "KnightCharacter.h"
#include "SkillComponent.h"
#include "QuestComponent.h"
#include "Loot.h"
#include "Inventory.h"
#include "Terrain.h"
#include "WorldMap.h"
#include "OakHill.h"
#include "TownLab.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "AnimationRuntime.h"
#include "BonePose.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Net/UnrealNetwork.h"

static TAutoConsoleVariable<int32> CVarDeer(TEXT("jeeb.Deer"), 1,
	TEXT("1: the server places the herds (AnimalHerds.csv) when the world is built; 0: no animals. Read once, on the server."), ECVF_Default);
static TAutoConsoleVariable<float> CVarDeerMeshYaw(TEXT("jeeb.Deer.MeshYaw"), -90.f,
	TEXT("Degrees the animal's model is turned to face the way it walks (looks only)."), ECVF_Default);

namespace
{
	// One kind's body, loaded on first use and kept for the whole run (the cache outlives worlds: see NpcBody.cpp)
	struct FDeerModel { bool bTried = false; USkeletalMesh* Mesh = nullptr; UAnimSequence* Clips[JeebDeer::C_Count] = {}; };
	const FDeerModel* ModelOf(const FString& Name)
	{
		static TMap<FString, FDeerModel> All;
		FDeerModel& M = All.FindOrAdd(Name);
		if (!M.bTried)
		{
			M.bTried = true;
			const FString Base = TEXT("/Game/Animals/");
			M.Mesh = LoadObject<USkeletalMesh>(nullptr, *FString::Printf(TEXT("%sSK_%s.SK_%s"), *Base, *Name, *Name));
			static const TCHAR* Keys[JeebDeer::C_Count] = { TEXT("Idle"), TEXT("Idle2"), TEXT("Walk"), TEXT("Run"), TEXT("Graze"), TEXT("Alert"), TEXT("Death") };
			int32 Got = 0;
			for (int32 i = 0; i < JeebDeer::C_Count; ++i)
			{
				M.Clips[i] = LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("%sA_%s_%s.A_%s_%s"), *Base, *Name, Keys[i], *Name, Keys[i]));
				if (M.Clips[i]) { M.Clips[i]->AddToRoot(); ++Got; }
			}
			if (M.Mesh) M.Mesh->AddToRoot();
			UE_LOG(LogTemp, Log, TEXT("JEEBDEER model %s: mesh %s, %d of %d clips"), *Name, M.Mesh ? TEXT("ok") : TEXT("MISSING"), Got, int32(JeebDeer::C_Count));
		}
		return (M.Mesh && M.Clips[JeebDeer::C_Idle]) ? &M : nullptr;
	}

	const JeebFoes::FAnimalDef& DefOf(int32 Kind)
	{
		static const JeebFoes::FAnimalDef Plain;
		return JeebFoes::Animals().IsValidIndex(Kind) ? JeebFoes::Animals()[Kind] : Plain;
	}

	AKnightCharacter* HitterOf(AActor* From)
	{
		if (!From) return nullptr;
		if (AKnightCharacter* K = Cast<AKnightCharacter>(From)) return K;
		if (AKnightCharacter* K = Cast<AKnightCharacter>(From->GetInstigator())) return K;
		return Cast<AKnightCharacter>(From->GetOwner());
	}

	bool NearRoad(float X, float Y, float Pad)   // a quick box test before PathClearance (as FoeCamps.cpp)
	{
		for (const JeebTerrain::FPathDef& P : JeebTerrain::PathDefs())
			if (P.Bounds.ExpandBy(P.MaxHalf + Pad).IsInside(FVector2D(X, Y))) return true;
		return false;
	}
	bool InWorld(const FVector2D& P, float Margin)
	{
		return P.X > JeebWorld::MinX + Margin && P.X < JeebWorld::MaxX - Margin && P.Y > JeebWorld::MinY + Margin && P.Y < JeebWorld::MaxY - Margin;
	}
	constexpr float TownKeep = 2500.f;   // a fleeing animal never comes nearer a town than this
}

// ---------------------------------------------------------------------------------------------- the animator

void UDeerAnimInstance::SetClips(UAnimSequence* const* In, float StartAt)
{
	for (int32 i = 0; i < JeebDeer::C_Count; ++i) Clip[i] = In[i] ? In[i] : In[JeebDeer::C_Idle];
	Start = StartAt;
	bNewClips = true;
}

FAnimInstanceProxy* UDeerAnimInstance::CreateAnimInstanceProxy() { return new FDeerAnimProxy(this); }
void UDeerAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) { delete static_cast<FDeerAnimProxy*>(InProxy); }

void FDeerAnimProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
	UDeerAnimInstance* A = CastChecked<UDeerAnimInstance>(InAnimInstance);
	const int32 N = JeebDeer::C_Count;
	if (A->bNewClips)
	{
		A->bNewClips = false;
		for (int32 i = 0; i < N; ++i)
		{
			Clip[i] = A->Clip[i];
			Time[i] = Clip[i] ? FMath::Fmod(A->Start, FMath::Max(Clip[i]->GetPlayLength(), 0.01f)) : 0.f;
			Weight[i] = (i == A->Want) ? 1.f : 0.f;
		}
	}
	// Cross-fade toward the wanted clip (0.3 s; into a run 0.18 s, it bolts; into the fall 0.5 s), and run every clip that shows
	const float Fade = A->Want == JeebDeer::C_Run ? 0.18f : A->Want == JeebDeer::C_Death ? 0.5f : 0.3f;
	const float Step = DeltaSeconds / Fade;
	for (int32 i = 0; i < N; ++i)
	{
		Weight[i] = (i == A->Want) ? FMath::Min(1.f, Weight[i] + Step) : FMath::Max(0.f, Weight[i] - Step);
		if (!Clip[i]) continue;
		const float Len = FMath::Max(Clip[i]->GetPlayLength(), 0.01f);
		if (Weight[i] > 0.f || i == A->Want)
		{
			const float R = (i == JeebDeer::C_Walk || i == JeebDeer::C_Run) ? A->Rate : 1.f;
			Time[i] = (i == JeebDeer::C_Death) ? FMath::Min(Time[i] + DeltaSeconds, Len - 0.01f) : FMath::Fmod(Time[i] + DeltaSeconds * R, Len);
		}
		else if (i == JeebDeer::C_Death || i == JeebDeer::C_Graze || i == JeebDeer::C_Alert) Time[i] = 0.f;   // (these start from their beginning)
	}
}

bool FDeerAnimProxy::Evaluate(FPoseContext& Output)
{
	const int32 N = JeebDeer::C_Count;
	float Sum = 0.f;
	for (int32 i = 0; i < N; ++i) if (Clip[i]) Sum += Weight[i];
	if (Sum <= KINDA_SMALL_NUMBER) { Output.ResetToRefPose(); return true; }
	TArray<FCompactPose, TInlineAllocator<4>> Poses;
	TArray<FBlendedCurve, TInlineAllocator<4>> Curves;
	TArray<UE::Anim::FStackAttributeContainer, TInlineAllocator<4>> Attrs;
	TArray<float, TInlineAllocator<4>> W;
	Poses.Reserve(N); Curves.Reserve(N); Attrs.Reserve(N);   // (no growing while poses are held)
	for (int32 i = 0; i < N; ++i)
	{
		if (!Clip[i] || Weight[i] <= 0.f) continue;
		FCompactPose& P = Poses.AddDefaulted_GetRef();
		P.SetBoneContainer(&Output.Pose.GetBoneContainer());
		FBlendedCurve& C = Curves.AddDefaulted_GetRef();
		C.InitFrom(Output.Curve);
		UE::Anim::FStackAttributeContainer& At = Attrs.AddDefaulted_GetRef();
		FAnimationPoseData PD(P, C, At);
		Clip[i]->GetAnimationPose(PD, FAnimExtractContext(double(Time[i]), false));
		W.Add(Weight[i] / Sum);
	}
	if (Poses.Num() == 1) { Output.Pose = Poses[0]; Output.Curve = Curves[0]; }
	else
	{
		FAnimationPoseData Out(Output);
		FAnimationRuntime::BlendPosesTogether(Poses, Curves, Attrs, W, Out);
	}
	return true;
}

// ---------------------------------------------------------------------------------------------- one animal

ADeer::ADeer()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(false);   // (RepMove instead: players' machines glide between updates and stand it on their own ground)
	SetNetUpdateFrequency(20.f);
	SetNetCullDistanceSquared(FMath::Square(16000.f));   // players further than 160 m are never sent it

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	JBody = CreateDefaultSubobject<USceneComponent>(TEXT("JBody")); JBody->SetupAttachment(Root);
	JNeck = CreateDefaultSubobject<USceneComponent>(TEXT("JNeck")); JNeck->SetupAttachment(Root);
	JHead = CreateDefaultSubobject<USceneComponent>(TEXT("JHead")); JHead->SetupAttachment(Root);

	Hp = CreateDefaultSubobject<UHealthComponent>(TEXT("Hp"));
	Hp->Max = Hp->Cur = 24;
	Hp->BarHeight = 215.f;
	Hp->HitHeight = 105.f;
	Hp->HitRadius = 90.f;
	Rand.Initialize(977);
}

void ADeer::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(ADeer, RepKind, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(ADeer, RepSize, COND_InitialOnly);
	DOREPLIFETIME(ADeer, RepMove);
	DOREPLIFETIME(ADeer, RepState);
}

void ADeer::SetupDeer(int32 InKind, int32 InHerd, ADeerHerds* InHerds, const FVector2D& InHome, int32 Seed, float Size)
{
	RepKind = uint8(FMath::Clamp(InKind, 0, 255));
	RepSize = uint8(FMath::Clamp(FMath::RoundToInt((Size - 0.92f) / 0.16f * 255.f), 0, 255));
	Herd = InHerd; Herds = InHerds; Home = InHome;
	Rand.Initialize(Seed);
	const JeebFoes::FAnimalDef& D = DefOf(InKind);
	Hp->Max = Hp->Cur = D.Hp;
	RespawnSecs = D.Respawn;
}

float ADeer::Scale() const { return DefOf(RepKind).Scale * (0.92f + 0.16f * RepSize / 255.f); }

void ADeer::BeginPlay()
{
	Super::BeginPlay();
	const float S = Scale();
	Hp->BarHeight = 198.f * S; Hp->HitHeight = 82.f * S; Hp->HitRadius = 70.f * S;
	Hp->GatherHurt = [this](TArray<JeebHit::FCapsule>& Out) { GatherHurt(Out); };
	if (HasAuthority())
	{
		Hp->OnDamaged = [this](AActor* From) { OnHurt(From); };
		const FVector L = GetActorLocation();
		SetActorLocation(FVector(L.X, L.Y, JeebFoes::Ground(L.X, L.Y)));
		Goal = FVector2D(L);
		// each starts somewhere else in its day
		RepState = uint8(Rand.FRand() < 0.6f ? EDeerState::Graze : EDeerState::Idle);
		StateFor = Rand.FRandRange(3.f, 12.f);
		SendMove();
	}
	BuildBody();
	TickLook(0.f, 0.f);
}

void ADeer::BuildBody()
{
	if (IsRunningDedicatedServer() || Body) return;   // (the server needs no body: its hurt boxes are worked out from where it stands)
	const JeebFoes::FAnimalDef& D = DefOf(RepKind);
	const FDeerModel* M = ModelOf(D.Model);
	if (!M) return;
	USkeletalMeshComponent* C = NewObject<USkeletalMeshComponent>(this, TEXT("Body"));
	C->SetupAttachment(Root);
	C->SetRelativeRotation(FRotator(0.f, CVarDeerMeshYaw.GetValueOnGameThread(), 0.f));
	C->SetRelativeScale3D(FVector(Scale()));
	C->SetSkeletalMesh(M->Mesh);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetGenerateOverlapEvents(false);
	C->SetCanEverAffectNavigation(false);
	// cheap when far or unseen: no pose updates off screen, fewer at a distance, gone past 160 m (the server stops sending it there too)
	C->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	C->bEnableUpdateRateOptimizations = true;
	C->LDMaxDrawDistance = 16000.f;
	C->CachedMaxDrawDistance = 16000.f;
	C->bAffectDynamicIndirectLighting = false;
	C->SetAnimInstanceClass(UDeerAnimInstance::StaticClass());
	C->RegisterComponent();
	Anim = Cast<UDeerAnimInstance>(C->GetAnimInstance());
	if (Anim)
	{
		Anim->SetClips(M->Clips, Rand.FRandRange(0.f, 5.f) + float(GetTypeHash(GetFName()) % 100u) * 0.05f);
		Anim->Drive(IsDown() ? JeebDeer::C_Death : JeebDeer::C_Idle, 1.f);
	}
	Body = C;
}

void ADeer::OnRep_Move()
{
	MoveAt = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (!bPlaced)   // (the first word of it: stand there at once)
	{
		bPlaced = true;
		SetActorLocationAndRotation(FVector(RepMove.Loc.X, RepMove.Loc.Y, JeebFoes::Ground(RepMove.Loc.X, RepMove.Loc.Y)), FRotator(0.f, RepMove.Yaw * (360.f / 65536.f), 0.f));
	}
}

void ADeer::OnRep_State()
{
	if (StateNow() != EDeerState::Dead) { DeadFor = 0.f; }
}

void ADeer::SendMove()
{
	const FVector L = GetActorLocation();
	RepMove.Loc = L;
	RepMove.Yaw = uint16(FMath::RoundToInt(FRotator::ClampAxis(GetActorRotation().Yaw) * (65536.f / 360.f)) & 0xFFFF);
	RepMove.Speed = uint16(FMath::Clamp(FMath::RoundToInt(Speed), 0, 65535));
}

void ADeer::SetSleeping(bool bSleep)
{
	if (bSleeping == bSleep) return;
	bSleeping = bSleep;
	SetActorTickInterval(bSleep ? 1.f : 0.f);
	SetNetUpdateFrequency(bSleep ? 1.f : 20.f);
	if (!bSleep) ForceNetUpdate();
}

void ADeer::SetState(EDeerState S, const TCHAR* Why)
{
	if (StateNow() == S) return;
	static const TCHAR* const Names[] = { TEXT("graze"), TEXT("idle"), TEXT("look"), TEXT("walk"), TEXT("alert"), TEXT("flee"), TEXT("dead") };
	const bool bLoud = S == EDeerState::Alert || S == EDeerState::Flee || S == EDeerState::Dead || StateNow() == EDeerState::Flee || StateNow() == EDeerState::Dead;
	if (bLoud) UE_LOG(LogTemp, Log, TEXT("JEEBDEER %s (herd %d) %s -> %s: %s; at %.0f %.0f, %.0f m from home, health %d/%d"), *GetName(), Herd,
		Names[RepState], Names[uint8(S)], Why, GetActorLocation().X, GetActorLocation().Y, FVector2D::Distance(FVector2D(GetActorLocation()), Home) / 100.f, Hp->Cur, Hp->Max);
	RepState = uint8(S);
	StateT = 0.f;
	if (S == EDeerState::Alert) CalmT = 0.f;
	ForceNetUpdate();
}

// The hurt boxes, from where it stands (the same sums on the server and on players' machines): body, neck, head, two pairs of legs
void ADeer::GatherHurt(TArray<JeebHit::FCapsule>& Out) const
{
	if (IsDown()) return;
	const float S = Scale();
	const FTransform Xf(FRotator(0.f, GetActorRotation().Yaw, 0.f), GetActorLocation());
	auto Cap = [&](const FVector& A, const FVector& B, float R, uint8 Zone)
	{
		JeebHit::FCapsule C; C.A = Xf.TransformPosition(A * S); C.B = Xf.TransformPosition(B * S); C.R = R * S; C.Zone = Zone;
		Out.Add(C);
	};
	const float G = HeadDown;
	const FVector NeckTop = FMath::Lerp(FVector(46.f, 0.f, 122.f), FVector(72.f, 0.f, 34.f), G);
	const FVector Head = FMath::Lerp(FVector(57.f, 0.f, 128.f), FVector(84.f, 0.f, 16.f), G);
	Cap(FVector(26.f, 0.f, 84.f), FVector(-44.f, 0.f, 85.f), 24.f, JeebHit::Z_Torso);
	Cap(FVector(30.f, 0.f, 96.f), NeckTop, 12.f, JeebHit::Z_Torso);
	Cap(Head, Head, 14.f, JeebHit::Z_Head);
	Cap(FVector(24.f, 0.f, 62.f), FVector(22.f, 0.f, 8.f), 10.f, JeebHit::Z_Leg);
	Cap(FVector(-48.f, 0.f, 62.f), FVector(-50.f, 0.f, 8.f), 10.f, JeebHit::Z_Leg);
}

// Every machine: the clip, the head's height (for the hurt boxes and the joints arrows stick to), the lean on a slope, the fall
void ADeer::TickLook(float Dt, float MoveSpeed)
{
	const EDeerState St = StateNow();
	const JeebFoes::FAnimalDef& D = DefOf(RepKind);
	const float S = Scale();
	HeadDown = FMath::FInterpConstantTo(HeadDown, St == EDeerState::Graze ? 1.f : 0.f, Dt, 1.2f);
	if (St == EDeerState::Dead) { DeadFor += Dt; Fall = FMath::FInterpConstantTo(Fall, 1.f, Dt, 2.f); }
	else Fall = FMath::FInterpConstantTo(Fall, 0.f, Dt, 4.f);
	const float Hd = FMath::SmoothStep(0.f, 1.f, HeadDown);
	JBody->SetRelativeLocation(FVector(-8.f, 0.f, FMath::Lerp(85.f, 24.f, Fall)) * S);
	JNeck->SetRelativeLocation(FMath::Lerp(FVector(38.f, 0.f, 110.f), FVector(52.f, 0.f, 64.f), Hd) * S);
	JHead->SetRelativeLocation(FMath::Lerp(FVector(57.f, 0.f, 128.f), FVector(84.f, 0.f, 16.f), Hd) * S);
	if (!Body) return;
	int32 Clip = JeebDeer::C_Idle;
	float Rate = 1.f;
	switch (St)
	{
	case EDeerState::Graze: Clip = JeebDeer::C_Graze; break;
	case EDeerState::Look:  Clip = JeebDeer::C_Look; break;
	case EDeerState::Alert: Clip = JeebDeer::C_Alert; break;
	case EDeerState::Dead:  Clip = JeebDeer::C_Death; break;
	default: break;
	}
	if (St != EDeerState::Dead && MoveSpeed > 25.f)
	{
		const bool bRun = MoveSpeed > D.WalkSpeed * 2.4f;
		Clip = bRun ? JeebDeer::C_Run : JeebDeer::C_Walk;
		Rate = FMath::Clamp(MoveSpeed / ((bRun ? D.RunClip : D.WalkClip) * S), 0.5f, 2.6f);
	}
	if (Anim) Anim->Drive(Clip, Rate);
	// lean with the ground under it, and lie on it when down (the lying pose is modelled 42 cm up)
	const FVector L = GetActorLocation();
	const FVector Fw = GetActorForwardVector();
	const float Ahead = JeebFoes::Ground(L.X + Fw.X * 55.f * S, L.Y + Fw.Y * 55.f * S), Back = JeebFoes::Ground(L.X - Fw.X * 55.f * S, L.Y - Fw.Y * 55.f * S);
	SlopePitch = FMath::FInterpTo(SlopePitch, FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2(Ahead - Back, 110.f * S)), -22.f, 22.f), Dt, 6.f);
	const float Sink = (St == EDeerState::Dead && DeadFor > 20.f) ? FMath::Min(DeadFor - 20.f, 1.6f) * 70.f : 0.f;   // (after a while it sinks away; the server hides it then)
	Body->SetRelativeLocation(FVector(0.f, 0.f, -42.f * S * FMath::SmoothStep(0.f, 1.f, Fall) - Sink));
	// (the mesh is yawed to face forward, so its own roll axis is the animal's pitch)
	const float MeshYaw = CVarDeerMeshYaw.GetValueOnGameThread();
	Body->SetRelativeRotation((FRotator(SlopePitch, 0.f, 0.f).Quaternion() * FRotator(0.f, MeshYaw, 0.f).Quaternion()).Rotator());
	// a shadow only within 60 m of this machine's own view (far ones cost the most and show the least)
	if (const APlayerController* PC = GetWorld()->GetFirstPlayerController())   // (looks only: this machine's own camera)
	{
		FVector CamL; FRotator CamR;
		PC->GetPlayerViewPoint(CamL, CamR);
		const bool bShadow = FVector::DistSquared(CamL, L) < FMath::Square(6000.f);
		if (Body->CastShadow != bShadow) Body->SetCastShadow(bShadow);
	}
}

void ADeer::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float Dt = FMath::Min(DeltaSeconds, 1.2f);
	if (HasAuthority())
	{
		if (IsDown()) TickDown(Dt);
		else TickMind(Dt);
		SendMove();
		TickLook(FMath::Min(Dt, 0.1f), Speed);
		return;
	}
	// A player's machine: glide toward where the server says it is by now, on this machine's own ground
	if (!bPlaced) return;
	if (!Body) BuildBody();
	const UWorld* W = GetWorld();
	const float Yaw = RepMove.Yaw * (360.f / 65536.f);
	const float Since = FMath::Min(float(W->GetTimeSeconds() - MoveAt), 0.25f);
	const FVector2D Fw(FMath::Cos(FMath::DegreesToRadians(Yaw)), FMath::Sin(FMath::DegreesToRadians(Yaw)));
	const FVector2D Target = FVector2D(RepMove.Loc.X, RepMove.Loc.Y) + Fw * (float(RepMove.Speed) * Since);
	FVector2D Me(GetActorLocation());
	if (FVector2D::DistSquared(Me, Target) > FMath::Square(900.f)) Me = Target;   // (it came back to life somewhere else: no gliding across the map)
	else Me += (Target - Me) * (1.f - FMath::Exp(-14.f * Dt));
	const float NewYaw = FMath::FixedTurn(GetActorRotation().Yaw, Yaw, 720.f * Dt);
	SetActorLocationAndRotation(FVector(Me.X, Me.Y, JeebFoes::Ground(Me.X, Me.Y)), FRotator(0.f, NewYaw, 0.f));
	TickLook(FMath::Min(Dt, 0.1f), float(RepMove.Speed));
}

// ---------------------------------------------------------------------------------------------- the server's side

bool ADeer::Sense(FVector2D& OutAt, float& OutDist) const
{
	const JeebFoes::FAnimalDef& D = DefOf(RepKind);
	const FVector2D Me(GetActorLocation());
	const FVector2D Fw(GetActorForwardVector());
	const bool bHeadDown = StateNow() == EDeerState::Graze;
	bool bAny = false;
	OutDist = 1.0e9f;
	for (TActorIterator<AKnightCharacter> It(GetWorld()); It; ++It)
	{
		if (It->GetHealth() <= 0.f) continue;
		const FVector2D P(It->GetActorLocation());
		const float Dist = FVector2D::Distance(P, Me);
		if (Dist > FMath::Max(D.Sight, D.HearRun)) continue;
		// hearing: by how fast he moves (sprinting carries far; crouched or standing still hardly at all)
		const float V = It->GetVelocity().Size2D();
		const float Hear = V > 600.f ? D.HearRun : V > 260.f ? D.HearWalk : V > 40.f ? D.HearSneak * 1.5f : D.HearSneak;
		// sight: in front and to the sides (a deer sees most of the way round), less with its head in the grass, less of a crouched man
		const bool bFront = FVector2D::DotProduct(Fw, (P - Me).GetSafeNormal()) > -0.45f;
		float See = bFront ? D.Sight : D.Sight * D.Behind;
		if (bHeadDown) See *= 0.75f;
		if (It->IsCrouchedNow()) See *= 0.55f;
		if (Dist < FMath::Max(Hear, See) && Dist < OutDist) { OutDist = Dist; OutAt = P; bAny = true; }
	}
	return bAny;
}

bool ADeer::Clear(const FVector2D& From, const FVector2D& To) const
{
	if (!InWorld(To, 1500.f)) return false;
	if (JeebFoes::InTown(To.X, To.Y, TownKeep) && !JeebFoes::InTown(From.X, From.Y, TownKeep)) return false;   // never toward a town (one already near may walk off)
	if (JeebFoes::DeepWater(To.X, To.Y) && !JeebFoes::DeepWater(From.X, From.Y)) return false;
	FCollisionObjectQueryParams Obj;
	Obj.AddObjectTypesToQuery(ECC_WorldStatic);
	FCollisionQueryParams Q(SCENE_QUERY_STAT(DeerStep), false, this);
	const FVector A(From.X, From.Y, JeebFoes::Ground(From.X, From.Y) + 130.f), B(To.X, To.Y, JeebFoes::Ground(To.X, To.Y) + 130.f);
	FHitResult Hit;
	return !(GetWorld()->SweepSingleByObjectType(Hit, A, B, FQuat::Identity, Obj, FCollisionShape::MakeSphere(45.f), Q) && !Hit.bStartPenetrating);
}

bool ADeer::Step(const FVector2D& Dir, float Want, float TurnRate, float Dt)
{
	const FVector2D At(GetActorLocation());
	// which way is open: straight, or round whatever stands in the way (the side changes at most every 0.6 s, so it never jitters)
	const double NowT = GetWorld()->GetTimeSeconds();
	const float Look = FMath::Clamp(Want * 0.45f, 120.f, 420.f);
	static const float Turns[] = { 0.f, 35.f, 70.f, 110.f, 150.f };
	FVector2D Open = Dir;
	bool bFound = false;
	for (int32 i = 0; i < 9 && !bFound; ++i)
	{
		const float Ang = (i == 0) ? 0.f : Turns[(i + 1) / 2] * ((i & 1) ? StepSide : -StepSide);
		if (i >= 2 && i % 2 == 0 && NowT - StepSideAt < 0.6) continue;
		const FVector2D Dd = Dir.GetRotated(Ang);
		if (!Clear(At, At + Dd * Look)) continue;
		if (i >= 2 && i % 2 == 0) { StepSide = -StepSide; StepSideAt = NowT; }
		Open = Dd; bFound = true;
	}
	const float WantYaw = FMath::RadiansToDegrees(FMath::Atan2(Open.Y, Open.X));
	const float Yaw = FMath::FixedTurn(GetActorRotation().Yaw, WantYaw, TurnRate * Dt);
	const FVector2D Fw(FMath::Cos(FMath::DegreesToRadians(Yaw)), FMath::Sin(FMath::DegreesToRadians(Yaw)));
	// forward along its own nose, slower while it still turns (no sliding sideways)
	const float Align = FMath::Clamp(FVector2D::DotProduct(Fw, Open), 0.f, 1.f);
	const float Go = bFound ? Want * FMath::Lerp(0.25f, 1.f, Align * Align) : 0.f;
	Speed = FMath::FInterpTo(Speed, Go, Dt, Want > 400.f ? 5.f : 3.f);
	FVector2D N = At + Fw * Speed * Dt;
	if (Speed * Dt > 0.5f && !Clear(At, At + Fw * FMath::Max(Speed * Dt, 60.f))) { N = At; Speed = 0.f; }
	SetActorLocationAndRotation(FVector(N.X, N.Y, JeebFoes::Ground(N.X, N.Y)), FRotator(0.f, Yaw, 0.f));
	return bFound;
}

void ADeer::Startle(const FVector2D& From, bool bBolt, const FVector2D& HerdDir)
{
	if (IsDown() || !HasAuthority()) return;
	ThreatAt = From;
	if (bBolt)
	{
		HerdFlee = HerdDir;
		CalmT = 0.f;
		SetState(EDeerState::Flee, TEXT("the herd bolts"));
	}
	else if (StateNow() != EDeerState::Flee && StateNow() != EDeerState::Alert)
	{
		AlertD0 = FVector2D::Distance(From, FVector2D(GetActorLocation()));
		SetState(EDeerState::Alert, TEXT("the herd looks up"));
	}
}

void ADeer::TickMind(float Dt)
{
	const JeebFoes::FAnimalDef& D = DefOf(RepKind);
	StateT += Dt;
#if !UE_BUILD_SHIPPING
	if (LabRole >= 0)   // (test pictures: no mind, a fixed part to play)
	{
		const FVector2D Me(GetActorLocation());
		if (LabRole == 0)
		{
			static const EDeerState Order[] = { EDeerState::Graze, EDeerState::Idle, EDeerState::Look, EDeerState::Alert };
			RepState = uint8(Order[FMath::FloorToInt(GetWorld()->GetTimeSeconds() / 6.f) % 4]);
			Speed = 0.f;
		}
		else if (LabRole == 1 || LabRole == 2)
		{
			if (FVector2D::Distance(Me, Goal) < 150.f) Goal = (FVector2D::Distance(Goal, LabA) < 10.f) ? LabB : LabA;
			RepState = uint8(LabRole == 1 ? EDeerState::Flee : EDeerState::Walk);
			Step((Goal - Me).GetSafeNormal(), LabRole == 1 ? D.RunSpeed : D.WalkSpeed, LabRole == 1 ? 420.f : 200.f, Dt);
		}
		else { Speed = 0.f; if (GetWorld()->GetTimeSeconds() > 16.f) Hp->ApplyDamage(999, nullptr, false); }
		if (FMath::FloorToInt(GetWorld()->GetTimeSeconds() / 4.f) != FMath::FloorToInt((GetWorld()->GetTimeSeconds() - Dt) / 4.f))
			UE_LOG(LogTemp, Log, TEXT("JEEBDEER lab %s role %d at %s yaw %.0f state %d speed %.0f; body %s, bounds %s r %.0f, seen %d, scale %.2f"), *GetName(), LabRole, *GetActorLocation().ToCompactString(), GetActorRotation().Yaw,
				int32(RepState), Speed, Body ? (Body->IsVisible() ? TEXT("visible") : TEXT("hidden")) : TEXT("NONE"), Body ? *Body->Bounds.Origin.ToCompactString() : TEXT("-"), Body ? Body->Bounds.SphereRadius : 0.f,
				Body && Body->WasRecentlyRendered(0.5f) ? 1 : 0, Scale());
		return;
	}
#endif
	if (bSleeping)   // nobody within reach: it stands where it is
	{
		Speed = 0.f;
		if (StateNow() == EDeerState::Flee || StateNow() == EDeerState::Alert || StateNow() == EDeerState::Walk) SetState(EDeerState::Graze, TEXT("nobody near"));
		return;
	}
	const FVector2D Me(GetActorLocation());
	SenseT -= Dt;
	FVector2D At = ThreatAt; float Dist = 1.0e9f;
	bool bSensed = false;
	const bool bSenseNow = SenseT <= 0.f;
	if (bSenseNow) { SenseT = 0.2f; bSensed = Sense(At, Dist); if (bSensed) ThreatAt = At; }
	const EDeerState St = StateNow();

	if (St == EDeerState::Flee)
	{
		// away from whoever it ran from, with the herd; slower when badly hurt
		if (bSenseNow) { if (bSensed) CalmT = 0.f; }
		CalmT += Dt;
		const float Far = FVector2D::Distance(Me, ThreatAt);
		const float FleeMul = FMath::Max(0.5f, JeebTune::V().AnimalFleeMul);   // (2026-11-23 owner: "run further away after being alerted": every animal runs this many times as long and as far as its Animals.csv Calm and Sight say)
		if (CalmT > D.Calm * FleeMul || (StateT > 4.f * FleeMul && Far > D.Sight * 2.2f * FleeMul && CalmT > 2.5f * FleeMul))
		{
			bGoHome = true;
			StateFor = Rand.FRandRange(4.f, 8.f);
			SetState(EDeerState::Look, TEXT("it has got away"));
			return;
		}
		FVector2D Away = (Me - ThreatAt).GetSafeNormal();
		if (Away.IsNearlyZero()) Away = FVector2D(GetActorForwardVector());
		const FVector2D Dir = (Away * 0.6f + HerdFlee * 0.4f).GetSafeNormal();
		const float Run = D.RunSpeed * (Hp->Cur * 2 < Hp->Max ? 0.8f : 1.f);
		if (!Step(Dir.IsNearlyZero() ? Away : Dir, Run, 520.f, Dt)) { StuckT += Dt; if (StuckT > 0.6f) { StuckT = 0.f; StepSide = -StepSide; HerdFlee = Away.GetRotated(Rand.FRandRange(-80.f, 80.f)); } }
		else StuckT = 0.f;
		return;
	}

	if (bSenseNow && bSensed)
	{
		if (Dist < D.FleeAt)
		{
			if (ADeerHerds* H = Herds.Get()) H->Alarm(Herd, this, At, true);
			else Startle(At, true, (Me - At).GetSafeNormal());
			return;
		}
		if (St != EDeerState::Alert)
		{
			AlertD0 = Dist;
			SetState(EDeerState::Alert, TEXT("notices a player"));
			if (ADeerHerds* H = Herds.Get()) H->Alarm(Herd, this, At, false);
			return;
		}
	}
	if (St == EDeerState::Alert)
	{
		// stands and watches, turning to face; bolts if he keeps coming (3 m nearer than when first noticed) or stays in plain view too long
		Speed = FMath::FInterpTo(Speed, 0.f, Dt, 6.f);
		const float WantYaw = FMath::RadiansToDegrees(FMath::Atan2(ThreatAt.Y - Me.Y, ThreatAt.X - Me.X));
		SetActorRotation(FRotator(0.f, FMath::FixedTurn(GetActorRotation().Yaw, WantYaw, 150.f * Dt), 0.f));
		if (bSenseNow && bSensed)
		{
			CalmT = 0.f;
			if (Dist < AlertD0 - 300.f || (StateT > 6.f && Dist < D.FleeAt * 1.5f))
			{
				if (ADeerHerds* H = Herds.Get()) H->Alarm(Herd, this, At, true);
				else Startle(At, true, (Me - At).GetSafeNormal());
				return;
			}
		}
		CalmT += Dt;
		if (CalmT > 4.f) { StateFor = Rand.FRandRange(2.f, 4.f); SetState(EDeerState::Look, TEXT("nothing there")); }
		return;
	}

	// The quiet life: graze, stand, look round, amble to a new patch near the herd's home (or back to it after a flight)
	if (St == EDeerState::Walk)
	{
		const float Left = FVector2D::Distance(Me, Goal);
		const bool bFar = bGoHome && Left > D.Roam * 2.f;
		if (Left < 70.f || StateT > (bGoHome ? 240.f : 25.f)) { bGoHome = false; StateFor = Rand.FRandRange(6.f, 16.f); SetState(EDeerState::Graze, TEXT("there")); return; }
		if (!Step((Goal - Me).GetSafeNormal(), D.WalkSpeed * (bFar ? 1.7f : 1.f), 160.f, Dt)) { StuckT += Dt; if (StuckT > 2.f) { StuckT = 0.f; StateFor = 4.f; SetState(EDeerState::Idle, TEXT("no way through")); } }
		else StuckT = 0.f;
		return;
	}
	Speed = FMath::FInterpTo(Speed, 0.f, Dt, 6.f);
	if (StateT < StateFor) return;
	const float FromHome = FVector2D::Distance(Me, Home);
	const float R = Rand.FRand();
	if (bGoHome || FromHome > D.Roam * 1.3f || (St == EDeerState::Graze && R < 0.45f) || (St != EDeerState::Graze && R < 0.3f))
	{
		// a new patch: a few tries for clear, dry ground within the herd's range
		for (int32 t = 0; t < 6; ++t)
		{
			const float A = Rand.FRandRange(0.f, 2.f * PI), Rr = D.Roam * FMath::Sqrt(Rand.FRandRange(0.05f, 1.f));
			const FVector2D P = Home + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Rr;
			if (!bGoHome && FromHome < D.Roam && FVector2D::Distance(P, Me) > 900.f) continue;   // (a short amble, not a march across the meadow)
			if (JeebFoes::DeepWater(P.X, P.Y) || JeebWorld::InWater(P.X, P.Y)) continue;
			Goal = P;
			SetState(EDeerState::Walk, TEXT("ambles"));
			return;
		}
	}
	if (St == EDeerState::Graze) { StateFor = Rand.FRandRange(2.7f, 6.f); SetState(R < 0.72f ? EDeerState::Idle : EDeerState::Look, TEXT("lifts its head")); }
	else { StateFor = Rand.FRandRange(6.f, 16.f); SetState(EDeerState::Graze, TEXT("grazes")); }
	StateT = 0.f;
}

void ADeer::OnHurt(AActor* From)
{
	if (Hp->Cur <= 0) { Die(From); return; }
	// hit and still standing: the whole herd runs from whoever did it
	const AKnightCharacter* K = HitterOf(From);
	const FVector2D Me(GetActorLocation());
	const FVector2D At = K ? FVector2D(K->GetActorLocation()) : (From ? FVector2D(From->GetActorLocation()) : Me - FVector2D(GetActorForwardVector()) * 500.f);
	if (ADeerHerds* H = Herds.Get()) H->Alarm(Herd, this, At, true);
	else Startle(At, true, (Me - At).GetSafeNormal());
}

void ADeer::Die(AActor* From)
{
	const JeebFoes::FAnimalDef& D = DefOf(RepKind);
	AKnightCharacter* K = HitterOf(From);
	const FVector2D Me(GetActorLocation());
	Speed = 0.f;
	DeadFor = 0.f;
	RespawnT = RespawnSecs;
	SetState(EDeerState::Dead, TEXT("is brought down"));
	// the rest of the herd runs
	if (ADeerHerds* H = Herds.Get()) H->Alarm(Herd, this, K ? FVector2D(K->GetActorLocation()) : Me, true);
	// Loot (the server's roll from Animals.csv), each thing lying beside the body
	TArray<FInvSlot> Items;
	int32 Ingots = 0;
	JeebFoes::RollLoot(D.Loot, Rand, Items, Ingots);
	FString What;
	const FVector2D Side(-GetActorForwardVector().Y, GetActorForwardVector().X);
	for (int32 i = 0; i < Items.Num(); ++i)
	{
		const FVector2D P = Me + Side * (105.f * Scale()) + FVector2D(GetActorForwardVector()) * ((i - (Items.Num() - 1) * 0.5f) * 70.f);   // (behind its back as it lies: its legs point the other way)
		if (ALootDrop* Drop = GetWorld()->SpawnActor<ALootDrop>(FVector(P.X, P.Y, JeebFoes::Ground(P.X, P.Y)), FRotator::ZeroRotator))
			Drop->Setup(ELootLook::Item, TArray<FInvSlot>{ Items[i] }, 0);
		What += FString::Printf(TEXT("%s%d %s"), i ? TEXT(", ") : TEXT(""), Items[i].Count, *JeebItems::ItemName(Items[i].Id));
	}
	// The kill's XP: Archery from range, Attack up close (the XP for every hit came with the hits)
	const bool bRanged = K && FVector2D::Distance(FVector2D(K->GetActorLocation()), Me) > 450.f;
	if (USkillComponent* Sk = K ? K->FindComponentByClass<USkillComponent>() : nullptr)
	{
		if (D.XP > 0) Sk->AddXP(bRanged ? ESkillType::Archery : ESkillType::Attack, D.XP);
		Sk->Say(4.f, FColor(255, 210, 120), D.XP > 0 ? FString::Printf(TEXT("You brought down a %s! (+%d %s XP)"), *D.Name, D.XP, bRanged ? TEXT("Archery") : TEXT("Attack"))
			: FString::Printf(TEXT("You brought down a %s!"), *D.Name));
	}
	UE_LOG(LogTemp, Log, TEXT("JEEBDEER %s (%s, herd %d) brought down by %s at %.0f %.0f: drops %s; +%d %s XP; back in %.0f s"), *D.Name, *GetName(), Herd,
		K ? *K->GetName() : TEXT("nobody"), Me.X, Me.Y, What.IsEmpty() ? TEXT("nothing") : *What, K ? D.XP : 0, bRanged ? TEXT("Archery") : TEXT("Attack"), RespawnSecs);
	if (AActor* By = K ? static_cast<AActor*>(K) : From)
	{
		UQuestComponent::OnFoeBeaten(By, *D.Key.ToString());
		if (!D.Group.IsNone() && D.Group != D.Key) UQuestComponent::OnFoeBeaten(By, *D.Group.ToString());
	}
}

void ADeer::TickDown(float Dt)
{
	Speed = 0.f;
	RespawnT -= Dt;
	if (DeadFor > 21.6f && !IsHidden()) SetActorHiddenInGame(true);   // (it has sunk away: TickLook)
	if (RespawnT > 0.f) return;
	// back at the herd's home, but never where a player could watch it appear
	for (TActorIterator<AKnightCharacter> It(GetWorld()); It; ++It)
		if (FVector2D::Distance(FVector2D(It->GetActorLocation()), Home) < 6000.f) { RespawnT = 2.f; return; }
	Rise();
}

void ADeer::Rise()
{
	const JeebFoes::FAnimalDef& D = DefOf(RepKind);
	FVector2D P = Home;
	for (int32 t = 0; t < 8; ++t)
	{
		const float A = Rand.FRandRange(0.f, 2.f * PI);
		const FVector2D Q = Home + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Rand.FRandRange(150.f, D.Roam * 0.6f);
		if (ADeerHerds::GoodGround(GetWorld(), Q, 400.f)) { P = Q; break; }
	}
	Hp->Reset();
	Hp->Cur = Hp->Max;
	DeadFor = 0.f; Fall = 0.f; CalmT = 0.f; bGoHome = false;
	SetActorLocationAndRotation(FVector(P.X, P.Y, JeebFoes::Ground(P.X, P.Y)), FRotator(0.f, Rand.FRandRange(0.f, 360.f), 0.f));
	SetActorHiddenInGame(false);
	StateFor = Rand.FRandRange(4.f, 10.f);
	SetState(EDeerState::Graze, TEXT("a new one has come"));
	ForceNetUpdate();
}

// ---------------------------------------------------------------------------------------------- the herds

ADeerHerds::ADeerHerds()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.25f;
	bReplicates = false;   // (the animals replicate on their own; players' machines keep this only for the test's log)
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

bool ADeerHerds::GoodGround(UWorld* World, const FVector2D& P, float MaxSpread)
{
	if (!InWorld(P, 3000.f)) return false;
	if (JeebFoes::InTown(P.X, P.Y, 7000.f)) return false;
	if (JeebWorld::InWater(P.X, P.Y) || JeebFoes::DeepWater(P.X, P.Y)) return false;
	float Half = 0.f;
	const float Rd = JeebWorld::RiverDistance(P.X, P.Y, Half);   // (call, then compare: ReviewLessons "Code pitfalls")
	if (Rd - Half < 1500.f) return false;
	if (NearRoad(P.X, P.Y, 1500.f) && JeebTerrain::PathClearance(P.X, P.Y) < 1500.f) return false;
	float Lo = JeebFoes::Ground(P.X, P.Y), Hi = Lo;
	for (int32 a = 0; a < 8; ++a)
	{
		const FVector2D Q = P + FVector2D(FMath::Cos(a * PI / 4.f), FMath::Sin(a * PI / 4.f)) * 700.f;
		if (JeebWorld::InWater(Q.X, Q.Y) || JeebFoes::DeepWater(Q.X, Q.Y)) return false;
		const float H = JeebFoes::Ground(Q.X, Q.Y);
		Lo = FMath::Min(Lo, H); Hi = FMath::Max(Hi, H);
	}
	if (Hi - Lo > MaxSpread) return false;
	if (Lo < JeebWorld::RiverLevel + 60.f) return false;   // (not on a beach or a river flat)
	if (World)
	{
		FCollisionObjectQueryParams Obj;
		Obj.AddObjectTypesToQuery(ECC_WorldStatic);
		const FVector C(P.X, P.Y, JeebFoes::Ground(P.X, P.Y) + 220.f);
		if (World->OverlapAnyTestByObjectType(C, FQuat::Identity, Obj, FCollisionShape::MakeSphere(160.f))) return false;
	}
	return true;
}

void ADeerHerds::BeginPlay()
{
	Super::BeginPlay();
	bool bTest = false, bLab = false;
#if !UE_BUILD_SHIPPING
	bTest = FParse::Param(FCommandLine::Get(), TEXT("JeebDeerTest"));
	bLab = FCString::Strifind(FCommandLine::Get(), TEXT("DeerLab=")) != nullptr;
#endif
	const bool bServer = HasAuthority() && GetNetMode() != NM_Client;
	if (JeebTownLab::Active() || (JeebHit::InMoveLab(GetWorld()) && !bLab)) { SetActorTickEnabled(false); return; }
	// (the herds are placed a moment later, from Tick: overlap tests made in the same frame as the world build do not see the trees and
	// rocks that were just made, ReviewLessons 2026-11-19)
	bWantBuild = bServer;
	if (!bServer && !bTest) SetActorTickEnabled(false);
}

void ADeerHerds::SpawnHerd(FName Key, int32 Kind, const FVector2D& Home, int32 Count, int32 Seed)
{
	FRandomStream R(Seed);
	FHerd H;
	H.Key = Key; H.Kind = Kind; H.Home = Home;
	const int32 HerdIdx = Herds.Num();
	float TestRespawn = 0.f;
#if !UE_BUILD_SHIPPING
	FParse::Value(FCommandLine::Get(), TEXT("DeerRespawn="), TestRespawn);   // (tests: a quicker respawn)
#endif
	for (int32 i = 0; i < Count; ++i)
	{
		FVector2D P = Home;
		for (int32 t = 0; t < 10; ++t)
		{
			const float A = 2.f * PI * (i + R.FRandRange(-0.3f, 0.3f)) / Count, Rr = R.FRandRange(250.f, 750.f);
			P = Home + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Rr;
			FCollisionObjectQueryParams Obj;
			Obj.AddObjectTypesToQuery(ECC_WorldStatic);
			if (!JeebFoes::DeepWater(P.X, P.Y) && !GetWorld()->OverlapAnyTestByObjectType(FVector(P.X, P.Y, JeebFoes::Ground(P.X, P.Y) + 150.f), FQuat::Identity, Obj, FCollisionShape::MakeSphere(70.f))) break;
			P = Home;
		}
		const FTransform Xf(FRotator(0.f, R.FRandRange(0.f, 360.f), 0.f), FVector(P.X, P.Y, JeebFoes::Ground(P.X, P.Y)));
		ADeer* Dr = GetWorld()->SpawnActorDeferred<ADeer>(ADeer::StaticClass(), Xf, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Dr) continue;
		Dr->SetupDeer(Kind, HerdIdx, this, Home, Seed * 31 + i * 7 + 1, i == 0 ? 1.06f : R.FRandRange(0.92f, 1.04f));   // (the first is the big one)
		if (TestRespawn > 0.f) Dr->RespawnSecs = TestRespawn;
		Dr->FinishSpawning(Xf);
		H.Deer.Add(Dr);
	}
	Herds.Add(H);
}

void ADeerHerds::Build()
{
	const double T0 = FPlatformTime::Seconds();
	const TArray<JeebFoes::FHerdDef>& Defs = JeebFoes::Herds();
	TArray<FVector2D> Camps;
	for (TActorIterator<AFoeCamps> It(GetWorld()); It; ++It) for (const AFoeCamps::FCamp& C : It->GetCamps()) Camps.Add(C.At);
	auto Ok = [&](const FVector2D& P, float Apart, float MaxSpread) -> bool
	{
		for (const FHerd& H : Herds) if (FVector2D::DistSquared(H.Home, P) < Apart * Apart) return false;
		for (const FVector2D& C : Camps) if (FVector2D::DistSquared(C, P) < FMath::Square(9000.f)) return false;
		return GoodGround(GetWorld(), P, MaxSpread);
	};
	const FVector2D Plaza(JeebOakHill::PosX, JeebOakHill::PosY);
	int32 Total = 0;
	for (int32 d = 0; d < Defs.Num(); ++d)
	{
		const JeebFoes::FHerdDef& D = Defs[d];
		FRandomStream R(9101 + d * 53);
		if (!D.bWild)
		{
			// the nearest good ground to its spot, on fixed rings
			bool bFound = false;
			FVector2D At = D.Near;
			for (int32 Pass = 0; Pass < 2 && !bFound; ++Pass)
				for (float Rr = 0.f; Rr <= 8000.f && !bFound; Rr += 400.f)
				{
					const int32 N = Rr < 1.f ? 1 : FMath::Max(6, FMath::RoundToInt(2.f * PI * Rr / 500.f));
					for (int32 k = 0; k < N; ++k)
					{
						const float A = 2.f * PI * k / N + Rr * 0.00041f;
						const FVector2D P = D.Near + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Rr;
						if (Ok(P, 12000.f, Pass == 0 ? 200.f : 380.f)) { At = P; bFound = true; break; }
					}
				}
			if (!bFound) { UE_LOG(LogTemp, Warning, TEXT("JEEBDEER herd %s: no good ground within 80 m of its spot; left out"), *D.Key.ToString()); continue; }
			const int32 Count = R.RandRange(D.Min, D.Max);
			SpawnHerd(D.Key, D.Animal, At, Count, 9101 + d * 53);
			Total += Count;
			UE_LOG(LogTemp, Log, TEXT("JEEBDEER herd %s at %.0f %.0f (%.0f m north, %.0f m east of Oak Hill's plaza; %.0f m from its spot): %d %s"), *D.Key.ToString(), At.X, At.Y,
				(At.X - Plaza.X) / 100.f, (At.Y - Plaza.Y) / 100.f, FVector2D::Distance(At, D.Near) / 100.f, Count, *DefOf(D.Animal).Name);
			continue;
		}
		// wild herds: spread over the whole world, by the same rules, off the mountains and the rocky hills
		int32 Made = 0;
		for (int32 Try = 0; Try < 6000 && Made < D.Herds; ++Try)
		{
			const FVector2D P(R.FRandRange(JeebWorld::MinX + 6000.f, JeebWorld::MaxX - 6000.f), R.FRandRange(JeebWorld::MinY + 6000.f, JeebWorld::MaxY - 6000.f));
			if (JeebWorld::MountainMask(P) > 0.25f || JeebWorld::RockMask(P) > 0.35f) continue;
			if (!Ok(P, 25000.f, 220.f)) continue;
			const int32 Count = R.RandRange(D.Min, D.Max);
			const FName Key(*FString::Printf(TEXT("%s%d"), *D.Key.ToString(), Made + 1));
			SpawnHerd(Key, D.Animal, P, Count, 9101 + d * 53 + Made * 17);
			++Made; Total += Count;
			UE_LOG(LogTemp, Log, TEXT("JEEBDEER herd %s at %.0f %.0f (%.0f m north, %.0f m east of Oak Hill's plaza): %d %s"), *Key.ToString(), P.X, P.Y,
				(P.X - Plaza.X) / 100.f, (P.Y - Plaza.Y) / 100.f, Count, *DefOf(D.Animal).Name);
		}
		if (Made < D.Herds) UE_LOG(LogTemp, Warning, TEXT("JEEBDEER %s: room for only %d of %d wild herds"), *D.Key.ToString(), Made, D.Herds);
	}
	UE_LOG(LogTemp, Log, TEXT("JEEBDEER %d animals in %d herds, placed in %.1f ms"), Total, Herds.Num(), (FPlatformTime::Seconds() - T0) * 1000.0);
}

void ADeerHerds::Alarm(int32 Herd, const ADeer* By, const FVector2D& From, bool bBolt)
{
	if (!Herds.IsValidIndex(Herd)) return;
	const FVector2D Src = By ? FVector2D(By->GetActorLocation()) : From;
	FVector2D Mid = FVector2D::ZeroVector; int32 N = 0;
	for (const TWeakObjectPtr<ADeer>& D : Herds[Herd].Deer) if (D.IsValid() && !D->IsDown()) { Mid += FVector2D(D->GetActorLocation()); ++N; }
	if (N == 0) return;
	Mid /= float(N);
	FVector2D Dir = (Mid - From).GetSafeNormal();
	if (Dir.IsNearlyZero()) Dir = (Src - From).GetSafeNormal();
	// (copy the list: Startle may log, never changes the herd)
	for (const TWeakObjectPtr<ADeer>& D : Herds[Herd].Deer)
		if (D.IsValid() && !D->IsDown() && FVector2D::Distance(FVector2D(D->GetActorLocation()), Src) < 4500.f) D->Startle(From, bBolt, Dir);
}

void ADeerHerds::TickSleep()
{
	TArray<FVector2D> Players;
	for (TActorIterator<AKnightCharacter> It(GetWorld()); It; ++It) Players.Add(FVector2D(It->GetActorLocation()));
	for (FHerd& H : Herds)
	{
		if (H.Key == TEXT("Lab")) continue;   // (test pictures: always awake)
		float Near = 1.0e9f;
		for (const FVector2D& P : Players)
		{
			Near = FMath::Min(Near, FVector2D::Distance(P, H.Home));
			for (const TWeakObjectPtr<ADeer>& D : H.Deer) if (D.IsValid()) Near = FMath::Min(Near, FVector2D::Distance(P, FVector2D(D->GetActorLocation())));
		}
		const bool bAwake = H.bAwake ? Near < SleepRange + 2000.f : Near < SleepRange;
		if (bAwake != H.bAwake)
		{
			H.bAwake = bAwake;
			if (Near < 1.0e8f) { UE_LOG(LogTemp, Log, TEXT("JEEBDEER herd %s %s (nearest player %.0f m)"), *H.Key.ToString(), bAwake ? TEXT("wakes") : TEXT("sleeps"), Near / 100.f); }   // (not the first pass on an empty server)
		}
		for (const TWeakObjectPtr<ADeer>& D : H.Deer) if (D.IsValid()) D->SetSleeping(!bAwake);
	}
}

#if !UE_BUILD_SHIPPING
// ===================================================================================================
// Test helpers (test builds only, never Shipping).
//  -DeerLab=X_Y_Yaw (with -JeebShot -ShotEyes=...): no herds; four stags with fixed parts at that spot for pictures and films: one standing
//   that goes through graze, idle, look, alert every 6 s; one running laps along a 30 m line (Yaw); one walking laps beside it; one that
//   falls at 16 s.
//  jeeb.Test.GoDeer N (console, single player): puts you 30 m from herd N, facing it.
//  -JeebDeerTest (a world server and one or two players' machines): the server stands the first player far from a herd (does it graze?),
//   then 22 m in front (does it look up?), then 9 m (does the whole herd bolt, and get away?), shoots a stag with a real arrow from the
//   player and finishes it with the player's own damage call (falls, loot, XP), waits for it to come back. A second player stands by and
//   watches. Every step logs JEEBDEERTEST PASS or FAIL; players' machines log what reaches them (JEEBDEERCLIENT) and take pictures.
// ===================================================================================================
static TAutoConsoleVariable<int32> CVarGoDeer(TEXT("jeeb.Test.GoDeer"), 0, TEXT("Test builds, single player: N puts you 30 m from herd N (1 = the first in AnimalHerds.csv), facing it."));

void ADeerHerds::BuildLab()
{
	FString V;
	FParse::Value(FCommandLine::Get(), TEXT("DeerLab="), V, false);
	TArray<FString> P;
	V.ParseIntoArray(P, TEXT("_"));
	if (P.Num() < 2 || JeebFoes::Animals().Num() == 0) return;
	const FVector2D At(FCString::Atof(*P[0]), FCString::Atof(*P[1]));
	const float Yaw = P.Num() > 2 ? FCString::Atof(*P[2]) : 0.f;
	const FVector2D Fw(FMath::Cos(FMath::DegreesToRadians(Yaw)), FMath::Sin(FMath::DegreesToRadians(Yaw))), Rt(-Fw.Y, Fw.X);
	FHerd H; H.Key = TEXT("Lab"); H.Home = At;
	const FVector2D Spots[4] = { At, At + Rt * 500.f - Fw * 1500.f, At + Rt * 900.f - Fw * 800.f, At - Rt * 450.f };
	for (int32 i = 0; i < 4; ++i)
	{
		const FTransform Xf(FRotator(0.f, i == 0 ? Yaw + 90.f : i == 3 ? Yaw + 60.f : Yaw, 0.f), FVector(Spots[i].X, Spots[i].Y, JeebFoes::Ground(Spots[i].X, Spots[i].Y)));
		ADeer* Dr = GetWorld()->SpawnActorDeferred<ADeer>(ADeer::StaticClass(), Xf, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Dr) continue;
		Dr->SetupDeer(0, 0, this, At, 500 + i, i == 0 ? 1.06f : 1.f);
		Dr->LabRole = i;
		Dr->LabA = Spots[i]; Dr->LabB = Spots[i] + Fw * 3000.f;
		Dr->FinishSpawning(Xf);
		Dr->Goal = Dr->LabB;
		H.Deer.Add(Dr);
	}
	Herds.Add(H);
	UE_LOG(LogTemp, Log, TEXT("JEEBDEER lab: four stags at %.0f %.0f (ground %.0f), laps toward yaw %.0f"), At.X, At.Y, JeebFoes::Ground(At.X, At.Y), Yaw);
}

#include "Projectile.h"
#include "KnightGameMode.h"
#include "KnightPlayerController.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "GameFramework/PlayerController.h"

struct FDeerTestDriver
{
	static void Log(const FString& S) { UE_LOG(LogTemp, Log, TEXT("JEEBDEERTEST %s"), *S); }
	static void Place(AKnightCharacter* K, const FVector2D& P, const FVector2D& LookAt)
	{
		if (!K) return;
		const FRotator R(0.f, FMath::RadiansToDegrees(FMath::Atan2(LookAt.Y - P.Y, LookAt.X - P.X)), 0.f);
		K->TeleportTo(FVector(P.X, P.Y, JeebFoes::Ground(P.X, P.Y) + 120.f), R, false, true);
		if (APlayerController* PC = Cast<APlayerController>(K->GetController())) PC->ClientSetRotation(FRotator(-6.f, R.Yaw, 0.f));
	}
	static FString States(const ADeerHerds::FHerd& H, const FVector2D& From)
	{
		static const TCHAR* const Names[] = { TEXT("graze"), TEXT("idle"), TEXT("look"), TEXT("walk"), TEXT("alert"), TEXT("flee"), TEXT("dead") };
		FString S;
		for (const TWeakObjectPtr<ADeer>& D : H.Deer) if (D.IsValid()) S += FString::Printf(TEXT(" [%s %.0f m hp %d]"), Names[uint8(D->StateNow())], FVector2D::Distance(FVector2D(D->GetActorLocation()), From) / 100.f, D->Hp->Cur);
		return S;
	}
	static int32 Count(const ADeerHerds::FHerd& H, EDeerState S) { int32 N = 0; for (const TWeakObjectPtr<ADeer>& D : H.Deer) if (D.IsValid() && D->StateNow() == S) ++N; return N; }
	static int32 Alive(const ADeerHerds::FHerd& H) { int32 N = 0; for (const TWeakObjectPtr<ADeer>& D : H.Deer) if (D.IsValid() && !D->IsDown()) ++N; return N; }
	static FVector2D Mid(const ADeerHerds::FHerd& H) { FVector2D M(0, 0); int32 N = 0; for (const TWeakObjectPtr<ADeer>& D : H.Deer) if (D.IsValid() && !D->IsDown()) { M += FVector2D(D->GetActorLocation()); ++N; } return N ? M / float(N) : H.Home; }
	static ADeer* Nearest(const ADeerHerds::FHerd& H, const FVector2D& P) { ADeer* B = nullptr; float Bd = 1.0e18f; for (const TWeakObjectPtr<ADeer>& D : H.Deer) if (D.IsValid() && !D->IsDown()) { const float Dd = FVector2D::DistSquared(FVector2D(D->GetActorLocation()), P); if (Dd < Bd) { Bd = Dd; B = D.Get(); } } return B; }
	static int32 DropsNear(UWorld* W, const FVector& P, float R, FString* What)
	{
		int32 N = 0;
		for (TActorIterator<ALootDrop> It(W); It; ++It) if (FVector::Dist2D(It->GetActorLocation(), P) < R) { ++N; if (What) *What += FString::Printf(TEXT(" [%s]"), *It->Describe()); }
		return N;
	}

	static void Server(ADeerHerds* C, float Dt)
	{
		static TWeakObjectPtr<AKnightCharacter> P1, P2;
		static TWeakObjectPtr<ADeer> Prey;
		static float StageT = 0.f, Far0 = 0.f;
		static int32 Passes = 0, Fails = 0, XP0 = 0, Hp0 = 0, Phase = 0;
		static FVector PreyAt = FVector::ZeroVector;
		C->TestClock += Dt; StageT += Dt;
		if (C->TestClock < C->TestNext || C->Herds.Num() == 0) return;
		UWorld* W = C->GetWorld();
		auto Next = [&](float Wait) { ++C->TestStage; StageT = 0.f; Phase = 0; C->TestNext = C->TestClock + Wait; };
		auto Check = [&](bool bOk, const FString& What) { Log(FString::Printf(TEXT("%s: %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What)); (bOk ? Passes : Fails)++; };
		AKnightCharacter* K = P1.Get();
		ADeerHerds::FHerd& H = C->Herds[0];
		const int32 Want = FMath::Max(1, []() { int32 N = 1; FParse::Value(FCommandLine::Get(), TEXT("DeerTestPlayers="), N); return N; }());
		const FVector2D HerdMid = Mid(H);
		switch (C->TestStage)
		{
		case 0:
		{
			TArray<AKnightCharacter*> Ks;
			for (TActorIterator<AKnightCharacter> It(W); It; ++It) if (It->GetController()) Ks.Add(*It);
			if ((Ks.Num() < Want && C->TestClock < 150.f) || Ks.Num() == 0) { C->TestNext = C->TestClock + 1.f; StageT = 0.f; return; }
			if (StageT < 8.f) return;
			P1 = Ks[0]; P2 = Ks.Num() > 1 ? Ks[1] : nullptr;
			int32 All = 0, Asleep = 0;
			for (const ADeerHerds::FHerd& Hh : C->Herds) for (const TWeakObjectPtr<ADeer>& D : Hh.Deer) if (D.IsValid()) { ++All; Asleep += D->IsSleeping() ? 1 : 0; }
			Log(FString::Printf(TEXT("start: %d players, %d herds, %d animals, %d asleep; herd %s at %.0f %.0f:%s"), Ks.Num(), C->Herds.Num(), All, Asleep, *H.Key.ToString(), H.Home.X, H.Home.Y, *States(H, H.Home)));
			Check(All > 0, FString::Printf(TEXT("%d animals in %d herds"), All, C->Herds.Num()));
			Next(0.5f); return;
		}
		case 1:   // far off (70 m), standing still: the herd grazes on
		{
			const FVector2D Away = (FVector2D(JeebOakHill::PosX, JeebOakHill::PosY) - H.Home).GetSafeNormal();
			Place(K, H.Home + Away * 7000.f, H.Home);
			if (P2.IsValid()) Place(P2.Get(), H.Home + Away * 7000.f + FVector2D(-Away.Y, Away.X) * 400.f, H.Home);
			Next(8.f); return;
		}
		case 2:
			Check(Count(H, EDeerState::Flee) == 0 && Count(H, EDeerState::Alert) == 0 && Alive(H) == H.Deer.Num(), FString::Printf(TEXT("players 70 m off: the herd is awake and calm:%s"), *States(H, FVector2D(K->GetActorLocation()))));
			{
				ADeer* D = Nearest(H, FVector2D(K->GetActorLocation()));
				if (!D) { C->TestStage = 900; return; }
				// 24 m in front of the nearest: inside its sight, outside the distance it bolts at
				const FVector2D Dp(D->GetActorLocation()), Fw(D->GetActorForwardVector());
				Place(K, Dp + Fw * 2400.f, Dp);
				Prey = D;
			}
			Next(0.2f); return;
		case 3:
			if (Count(H, EDeerState::Alert) > 0) { Check(true, FString::Printf(TEXT("a player 24 m in front: it looks up after %.1f s, and %d of %d watch:%s"), StageT, Count(H, EDeerState::Alert), Alive(H), *States(H, FVector2D(K->GetActorLocation())))); Next(1.5f); return; }
			if (Count(H, EDeerState::Flee) > 0) { Check(false, FString::Printf(TEXT("a player 24 m in front: it bolted without looking up first:%s"), *States(H, FVector2D(K->GetActorLocation())))); C->TestStage = 5; StageT = 0.f; return; }
			if (StageT > 12.f) { Check(false, FString::Printf(TEXT("a player 24 m in front is noticed within 12 s:%s"), *States(H, FVector2D(K->GetActorLocation())))); Next(0.2f); return; }
			C->TestNext = C->TestClock + 0.2f; return;
		case 4:   // nearer: 9 m
		{
			ADeer* D = Prey.IsValid() && !Prey->IsDown() ? Prey.Get() : Nearest(H, FVector2D(K->GetActorLocation()));
			if (!D) { C->TestStage = 900; return; }
			const FVector2D Dp(D->GetActorLocation());
			const FVector2D Dir = (FVector2D(K->GetActorLocation()) - Dp).GetSafeNormal();
			Place(K, Dp + Dir * 900.f, Dp);
			Far0 = 900.f;
			Next(0.2f); return;
		}
		case 5:
			if (Phase == 0)
			{
				if (Count(H, EDeerState::Flee) == Alive(H)) { Check(true, FString::Printf(TEXT("a player 9 m off: the whole herd bolts after %.1f s:%s"), StageT, *States(H, FVector2D(K->GetActorLocation())))); Phase = 1; StageT = 0.f; }
				else if (StageT > 5.f) { Check(false, FString::Printf(TEXT("the whole herd bolts within 5 s of a player 9 m off:%s"), *States(H, FVector2D(K->GetActorLocation())))); Phase = 1; StageT = 0.f; }
			}
			else if (StageT > 6.f)
			{
				const float Far = FVector2D::Distance(HerdMid, FVector2D(K->GetActorLocation()));
				Check(Far > 3500.f, FString::Printf(TEXT("6 s later the herd is %.0f m from the player (it runs %.1f m/s):%s"), Far / 100.f, DefOf(H.Kind).RunSpeed / 100.f, *States(H, FVector2D(K->GetActorLocation()))));
				Next(0.2f); return;
			}
			C->TestNext = C->TestClock + 0.2f; return;
		case 6:   // they calm down once nobody is near (the players stand still, far behind)
			if (Count(H, EDeerState::Flee) == 0) { Check(true, FString::Printf(TEXT("the herd stops running %.1f s later, %.0f m from the player and %.0f m from home:%s"), StageT, FVector2D::Distance(HerdMid, FVector2D(K->GetActorLocation())) / 100.f, FVector2D::Distance(HerdMid, H.Home) / 100.f, *States(H, FVector2D(K->GetActorLocation())))); Next(0.5f); return; }
			if (StageT > 40.f) { Check(false, FString::Printf(TEXT("the herd calms within 40 s:%s"), *States(H, FVector2D(K->GetActorLocation())))); Next(0.5f); return; }
			C->TestNext = C->TestClock + 0.5f; return;
		case 7:   // a real arrow from 20 m straight behind one of them (standing still there he is neither seen nor heard)
		{
			ADeer* D = Nearest(H, HerdMid);
			if (!D) { C->TestStage = 900; return; }
			Prey = D;
			const FVector2D Dp(D->GetActorLocation()), Fw(D->GetActorForwardVector());
			const FVector2D Side(-Fw.Y, Fw.X);
			Place(K, Dp - Fw * 2000.f, Dp);
			if (P2.IsValid()) Place(P2.Get(), Dp - Fw * 2300.f + Side * 1500.f, Dp);
			Hp0 = D->Hp->Cur;
			XP0 = K->FindComponentByClass<USkillComponent>()->GetXPTotal(ESkillType::Archery);
			Next(0.6f); return;
		}
		case 8:
		{
			ADeer* D = Prey.Get();
			if (!D) { C->TestStage = 900; return; }
			if (Phase == 0)
			{
				// the arrow the bow would loose: from the player's chest at the body's middle, 35 m/s, 8 damage
				const FVector Start = K->GetActorLocation() + FVector(0.f, 0.f, 40.f);
				const FVector Aim = D->GetActorLocation() + FVector(0.f, 0.f, 85.f * D->Scale()) + D->GetActorForwardVector() * (-8.f);
				FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				if (AProjectile* P = W->SpawnActor<AProjectile>(AProjectile::StaticClass(), Start, (Aim - Start).Rotation(), SP))
					P->Launch(K, (Aim - Start).GetSafeNormal(), 3500.f, 6000.f, 8, false, ESkillType::Archery, false, 1, 0, 0.f, 6000.f);
				Log(FString::Printf(TEXT("an arrow loosed at %s from %.1f m (it is %s, health %d)"), *D->GetName(), FVector::Dist(Start, Aim) / 100.f, *States(H, FVector2D(K->GetActorLocation())).Left(60), Hp0));
				Phase = 1; StageT = 0.f;
			}
			else if (D->Hp->Cur < Hp0 || StageT > 3.f)
			{
				Check(D->Hp->Cur < Hp0, FString::Printf(TEXT("the arrow hit it through its hurt boxes: health %d -> %d"), Hp0, D->Hp->Cur));
				Phase = 0;
				Next(1.0f); return;
			}
			C->TestNext = C->TestClock + 0.1f; return;
		}
		case 9:
			Check(Count(H, EDeerState::Flee) == Alive(H), FString::Printf(TEXT("hit, the whole herd runs:%s"), *States(H, FVector2D(K->GetActorLocation()))));
			if (ADeer* D = Prey.Get())
			{
				// finished with the player's own damage call (what an arrow's hit calls on the server), from where he stands
				PreyAt = D->GetActorLocation();
				K->DealDamage(D->Hp, 999, false, ESkillType::Archery, true);
				PreyAt = D->GetActorLocation();
			}
			Next(1.0f); return;
		case 10:
		{
			ADeer* D = Prey.Get();
			FString What;
			const int32 N = DropsNear(W, PreyAt, 400.f, &What);
			const int32 XP = K->FindComponentByClass<USkillComponent>()->GetXPTotal(ESkillType::Archery);
			Check(D && D->IsDown(), TEXT("the stag is down"));
			Check(N > 0, FString::Printf(TEXT("%d drops beside it:%s"), N, *What));
			Check(XP > XP0, FString::Printf(TEXT("Archery XP %d -> %d"), XP0, XP));
			// the player walks up to look (and the second player's machine photographs it)
			Place(K, FVector2D(PreyAt) + FVector2D(450.f, 250.f), FVector2D(PreyAt));
			if (P2.IsValid()) Place(P2.Get(), FVector2D(PreyAt) + FVector2D(-300.f, 520.f), FVector2D(PreyAt));
			Next(6.f); return;
		}
		case 11:   // everyone leaves; it comes back at home (the test's -DeerRespawn=)
			Place(K, H.Home + (FVector2D(JeebOakHill::PosX, JeebOakHill::PosY) - H.Home).GetSafeNormal() * 9000.f, H.Home);
			if (P2.IsValid()) Place(P2.Get(), H.Home + (FVector2D(JeebOakHill::PosX, JeebOakHill::PosY) - H.Home).GetSafeNormal() * 9300.f, H.Home);
			Next(1.f); return;
		case 12:
		{
			ADeer* D = Prey.Get();
			if (D && !D->IsDown() && !D->IsHidden()) { Check(FVector2D::Distance(FVector2D(D->GetActorLocation()), H.Home) < 1500.f, FString::Printf(TEXT("a new stag stands %.0f m from the herd's home after %.0f s, health %d/%d"), FVector2D::Distance(FVector2D(D->GetActorLocation()), H.Home) / 100.f, StageT, D->Hp->Cur, D->Hp->Max)); Next(0.5f); return; }
			if (StageT > 90.f) { Check(false, TEXT("the stag is back within 90 s")); Next(0.5f); return; }
			C->TestNext = C->TestClock + 1.f; return;
		}
		case 13:
			Place(K, FVector2D(JeebOakHill::SpawnX, JeebOakHill::SpawnY), FVector2D(JeebOakHill::PosX, JeebOakHill::PosY));
			if (P2.IsValid()) Place(P2.Get(), FVector2D(JeebOakHill::SpawnX + 300.f, JeebOakHill::SpawnY), FVector2D(JeebOakHill::PosX, JeebOakHill::PosY));
			Next(4.f); return;
		case 14:
		{
			int32 All = 0, Asleep = 0;
			for (const ADeerHerds::FHerd& Hh : C->Herds) for (const TWeakObjectPtr<ADeer>& D : Hh.Deer) if (D.IsValid()) { ++All; Asleep += D->IsSleeping() ? 1 : 0; }
			Check(Asleep == All, FString::Printf(TEXT("everyone back in Oak Hill: %d of %d animals sleep"), Asleep, All));
			Log(FString::Printf(TEXT("DONE: %d passed, %d failed"), Passes, Fails));
			C->TestStage = 1000; return;
		}
		case 900:
			Log(FString::Printf(TEXT("STOPPED early: %d passed, %d failed"), Passes, Fails));
			C->TestStage = 1000; return;
		default: return;
		}
	}

	// A player's machine: what reaches it
	static void Client(ADeerHerds* C, float Dt)
	{
		static float Clock = 0.f, NextLog = 0.f, QuitAt = -1.f;
		static TMap<TWeakObjectPtr<ADeer>, uint8> Seen;
		static bool bHerdShot = false, bFleeShot = false, bDownShot = false;
		Clock += Dt;
		APlayerController* PC = C->GetWorld()->GetFirstPlayerController();   // (test pictures and logs only: this machine's own view)
		AKnightCharacter* K = PC ? Cast<AKnightCharacter>(PC->GetPawn()) : nullptr;
		if (QuitAt < 0.f) { float S = 300.f; FParse::Value(FCommandLine::Get(), TEXT("DeerTestClientTime="), S); QuitAt = S; }
		if (Clock > QuitAt) { if (PC) PC->ConsoleCommand(TEXT("quit")); QuitAt = 1.0e9f; return; }
		if (!K) return;
		// a picture: this machine's own camera turns to the animal first, and the picture is taken half a second later
		static FString ShotName; static float ShotAt = -1.f; static TWeakObjectPtr<ADeer> ShotOf;
		auto Shot = [&](const TCHAR* Name, ADeer* Of) { ShotName = Name; ShotAt = Clock + 0.5f; ShotOf = Of; };
		if (ShotAt > 0.f)
		{
			if (ShotOf.IsValid()) PC->SetControlRotation(((ShotOf->GetActorLocation() + FVector(0.f, 0.f, 60.f)) - (K->GetActorLocation() + FVector(0.f, 0.f, 150.f))).Rotation());
			if (Clock >= ShotAt)
			{
				ShotAt = -1.f;
				FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Shots") / FString::Printf(TEXT("deer_%s_%s.png"), *ShotName, *K->GetName()), false, false);
				UE_LOG(LogTemp, Log, TEXT("JEEBDEERCLIENT picture deer_%s"), *ShotName);
			}
		}
		static const TCHAR* const Names[] = { TEXT("graze"), TEXT("idle"), TEXT("look"), TEXT("walk"), TEXT("alert"), TEXT("flee"), TEXT("dead") };
		const FVector Me = K->GetActorLocation();
		int32 N = 0, Fleeing = 0;
		ADeer* Fleer = nullptr;
		FString Near;
		for (TActorIterator<ADeer> It(C->GetWorld()); It; ++It)
		{
			ADeer* D = *It;
			++N;
			const uint8 State = uint8(D->StateNow()) | (D->IsHidden() ? 64 : 0);
			uint8& Was = Seen.FindOrAdd(D, 255);
			if (State != Was) { UE_LOG(LogTemp, Log, TEXT("JEEBDEERCLIENT %s is %s%s at %s, hp %d/%d"), *D->GetName(), Names[State & 15], (State & 64) ? TEXT(", hidden") : TEXT(""), *D->GetActorLocation().ToCompactString(), D->Hp->Cur, D->Hp->Max); Was = State; }
			const float Dist = FVector::Dist2D(D->GetActorLocation(), Me);
			if (Dist < 6000.f) Near += FString::Printf(TEXT(" [%s %.0f m speed %d hp %d]"), Names[State & 15], Dist / 100.f, int32(D->RepMove.Speed), D->Hp->Cur);
			if (D->StateNow() == EDeerState::Flee && Dist > 1200.f && Dist < 3000.f) { ++Fleeing; Fleer = D; }
			if (ShotAt > 0.f) continue;
			if (!bDownShot && D->StateNow() == EDeerState::Dead && Dist < 900.f && D->DeadFor > 2.5f && !D->IsHidden()) { bDownShot = true; Shot(TEXT("down"), D); }
			else if (!bHerdShot && Dist < 3000.f && D->StateNow() == EDeerState::Alert) { bHerdShot = true; Shot(TEXT("alert"), D); }
		}
		if (!bFleeShot && Fleeing >= 2 && ShotAt < 0.f) { bFleeShot = true; Shot(TEXT("flee"), Fleer); }
		if (Clock < NextLog) return;
		NextLog = Clock + 2.f;
		UE_LOG(LogTemp, Log, TEXT("JEEBDEERCLIENT t=%.0f %s at %.0f %.0f: %d animals sent here;%s"), Clock, *K->GetName(), Me.X, Me.Y, N, *Near);
	}
};
#endif

void ADeerHerds::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const bool bServer = HasAuthority() && GetNetMode() != NM_Client;
	if (bServer && bWantBuild)
	{
		BuildClock += DeltaSeconds;
		if (BuildClock < 1.5f) return;
		bWantBuild = false;
		if (CVarDeer.GetValueOnGameThread() == 0) { UE_LOG(LogTemp, Log, TEXT("JEEBDEER jeeb.Deer 0: no herds")); }
#if !UE_BUILD_SHIPPING
		else if (FCString::Strifind(FCommandLine::Get(), TEXT("DeerLab=")) != nullptr) BuildLab();
#endif
		else Build();
	}
	if (bServer)
	{
		SleepClock += DeltaSeconds;
		if (SleepClock >= 0.5f) { SleepClock = 0.f; TickSleep(); }
	}
#if !UE_BUILD_SHIPPING
	if (GetNetMode() == NM_Standalone)
	{
		const int32 Go = CVarGoDeer.GetValueOnGameThread();
		if (Go >= 1)
		{
			CVarGoDeer->Set(0, ECVF_SetByConsole);
			APlayerController* Pc = GetWorld()->GetFirstPlayerController();   // (a test command: this machine's own player)
			if (APawn* Me = Pc ? Pc->GetPawn() : nullptr)
				if (Herds.IsValidIndex(Go - 1))
				{
					const FVector2D H = Herds[Go - 1].Home;
					const FVector2D From = H + (FVector2D(Me->GetActorLocation()) - H).GetSafeNormal() * 3000.f;
					const FRotator R(0.f, FMath::RadiansToDegrees(FMath::Atan2(H.Y - From.Y, H.X - From.X)), 0.f);
					Me->SetActorLocation(FVector(From.X, From.Y, JeebFoes::Ground(From.X, From.Y) + 120.f), false, nullptr, ETeleportType::TeleportPhysics);
					Me->SetActorRotation(R);
					Pc->SetControlRotation(FRotator(-6.f, R.Yaw, 0.f));
					UE_LOG(LogTemp, Log, TEXT("JEEBDEER test: put 30 m from herd %s at %.0f %.0f"), *Herds[Go - 1].Key.ToString(), H.X, H.Y);
				}
		}
	}
	static const bool bTest = FParse::Param(FCommandLine::Get(), TEXT("JeebDeerTest"));
	if (!bTest) return;
	if (GetNetMode() == NM_DedicatedServer) FDeerTestDriver::Server(this, DeltaSeconds);
	else if (GetNetMode() == NM_Client) FDeerTestDriver::Client(this, DeltaSeconds);
#endif
}

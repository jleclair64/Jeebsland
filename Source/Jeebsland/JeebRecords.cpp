#include "JeebRecords.h"
#include "KnightPlayerController.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

// The numbers (2026-11-26, owner's request in Docs/ThursdayList.md item 9):
//   trout and salmon: mean 27 in, 36 in at the 0.03% upper tail. z(0.9997) = 3.4316, so SD = 9 / 3.4316 = 2.623 in.
//   bass: mean 15 in, 21 in at 0.03%: SD = 6 / 3.4316 = 1.748 in.
//   stag antlers: mean 8 points (a mature red deer stag; 10 to 12 is a good head, 12 a "royal", 14 an "imperial"). Board at 14 points,
//   made the same 0.03% tail: points are the normal size rounded, so 14 or more is 13.5 or more: SD = 5.5 / 3.4316 = 1.603.
//   That gives about 1.5% royals (12+), 0.03% imperials (14+); 2 points (a spiker) at the low end.
namespace
{
	constexpr float Zt = 3.4316f;
	struct FSpec { const TCHAR* Name; float Mean, Sd, Board, Lo, Hi; bool bWhole; };
	const FSpec Specs[JeebRecords::S_Num] = {
		{ TEXT("Trout"),   27.f, 9.f / Zt,   36.f, 16.f, 42.f, false },
		{ TEXT("Salmon"),  27.f, 9.f / Zt,   36.f, 16.f, 42.f, false },
		{ TEXT("Bass"),    15.f, 6.f / Zt,   21.f,  8.f, 25.f, false },
		{ TEXT("Antlers"),  8.f, 5.5f / Zt,  14.f,  2.f, 20.f, true },
	};
	constexpr int32 KeepTop = 10;     // per species, per world file (the all-time board shows the top 5 of all worlds)
	constexpr int32 KeepBoard = 400;  // board entries per world file (only the last week's are shown)

	struct FEntry { int32 S = 0; float Size = 0.f; FString Who, Key; int64 T = 0; };
	struct FBest { FString Name; float B[JeebRecords::S_Num] = { 0.f, 0.f, 0.f, 0.f }; };
	struct FFile { TArray<FEntry> Top, Board; TMap<FString, FBest> Best; };

	FString Dir() { return FPaths::ProjectSavedDir() / TEXT("Boards"); }
	int32 PortOf(UWorld* W) { return (W && W->GetNetMode() != NM_Standalone) ? W->URL.Port : 0; }
	FString FileFor(int32 Port) { return Dir() / FString::Printf(TEXT("World_%d.json"), Port); }

	TSharedRef<FJsonObject> EntryJson(const FEntry& E)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("s"), E.S); O->SetNumberField(TEXT("size"), E.Size);
		O->SetStringField(TEXT("who"), E.Who); O->SetStringField(TEXT("key"), E.Key); O->SetNumberField(TEXT("t"), double(E.T));
		return O;
	}
	FEntry EntryFrom(const TSharedPtr<FJsonObject>& O)
	{
		FEntry E;
		if (!O) return E;
		E.S = FMath::Clamp(int32(O->GetNumberField(TEXT("s"))), 0, JeebRecords::S_Num - 1);
		E.Size = float(O->GetNumberField(TEXT("size")));
		E.Who = O->GetStringField(TEXT("who")); E.Key = O->GetStringField(TEXT("key"));
		E.T = int64(O->GetNumberField(TEXT("t")));
		return E;
	}

	bool Load(const FString& Path, FFile& Out)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path)) return false;
		TSharedPtr<FJsonObject> Root;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root) return false;
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (Root->TryGetArrayField(TEXT("top"), A)) for (const TSharedPtr<FJsonValue>& V : *A) Out.Top.Add(EntryFrom(V->AsObject()));
		if (Root->TryGetArrayField(TEXT("board"), A)) for (const TSharedPtr<FJsonValue>& V : *A) Out.Board.Add(EntryFrom(V->AsObject()));
		const TSharedPtr<FJsonObject>* B = nullptr;
		if (Root->TryGetObjectField(TEXT("best"), B))
			for (const auto& KV : (*B)->Values)
			{
				const TSharedPtr<FJsonObject> O = KV.Value->AsObject();
				if (!O) continue;
				FBest& P = Out.Best.FindOrAdd(FString(KV.Key));
				P.Name = O->GetStringField(TEXT("name"));
				const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
				if (O->TryGetArrayField(TEXT("b"), Arr)) for (int32 i = 0; i < FMath::Min(Arr->Num(), int32(JeebRecords::S_Num)); ++i) P.B[i] = float((*Arr)[i]->AsNumber());
			}
		return true;
	}

	void Save(const FString& Path, const FFile& F)
	{
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Top, Board;
		for (const FEntry& E : F.Top) Top.Add(MakeShared<FJsonValueObject>(EntryJson(E)));
		for (const FEntry& E : F.Board) Board.Add(MakeShared<FJsonValueObject>(EntryJson(E)));
		Root->SetArrayField(TEXT("top"), Top);
		Root->SetArrayField(TEXT("board"), Board);
		TSharedRef<FJsonObject> Best = MakeShared<FJsonObject>();
		for (const auto& KV : F.Best)
		{
			TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("name"), KV.Value.Name);
			TArray<TSharedPtr<FJsonValue>> Arr;
			for (int32 i = 0; i < JeebRecords::S_Num; ++i) Arr.Add(MakeShared<FJsonValueNumber>(KV.Value.B[i]));
			O->SetArrayField(TEXT("b"), Arr);
			Best->SetObjectField(KV.Key, O);
		}
		Root->SetObjectField(TEXT("best"), Best);
		FString Text;
		FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Text));
		// written whole to a side file, then moved over the old one, so a reader never sees half a file
		const FString Tmp = Path + TEXT(".tmp");
		IFileManager::Get().MakeDirectory(*Dir(), true);
		if (FFileHelper::SaveStringToFile(Text, *Tmp, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
			if (!IFileManager::Get().Move(*Path, *Tmp, true, true)) UE_LOG(LogTemp, Error, TEXT("JEEBRECORDS could not write %s"), *Path);
	}

	// This world's own file, held in memory once loaded (only this server writes it)
	FFile& Own(int32 Port)
	{
		static TMap<int32, FFile> Files;
		if (FFile* F = Files.Find(Port)) return *F;
		FFile& F = Files.Add(Port);
		Load(FileFor(Port), F);
		return F;
	}

	// Every world's file merged (re-read at most every 15 s; this world's own copy is always the live one)
	void All(int32 Port, TArray<FFile>& Out)
	{
		static double ReadAt = -1.0e9;
		static TArray<TPair<int32, FFile>> Others;
		const double Now = FPlatformTime::Seconds();
		if (Now - ReadAt > 15.0)
		{
			ReadAt = Now;
			Others.Reset();
			TArray<FString> Names;
			IFileManager::Get().FindFiles(Names, *(Dir() / TEXT("World_*.json")), true, false);
			for (const FString& N : Names)
			{
				const int32 P = FCString::Atoi(*N.Mid(6));
				if (P == Port) continue;
				TPair<int32, FFile> E; E.Key = P;
				if (Load(Dir() / N, E.Value)) Others.Add(MoveTemp(E));
			}
		}
		Out.Reset();
		Out.Add(Own(Port));
		for (const TPair<int32, FFile>& E : Others) Out.Add(E.Value);
	}

	// Unix time (UTC) of the last Sunday midnight, the host's local time
	int64 WeekStart()
	{
		const FDateTime Local = FDateTime::Now();
		const FTimespan Offset = Local - FDateTime::UtcNow();
		const int32 SinceSunday = (int32(Local.GetDayOfWeek()) + 1) % 7;   // (EDayOfWeek: Monday = 0 ... Sunday = 6)
		const FDateTime Start = Local.GetDate() - FTimespan::FromDays(SinceSunday);
		return (Start - Offset).ToUnixTimestamp();
	}
}

namespace JeebRecords
{
	const TCHAR* SpeciesName(int32 S) { return Specs[FMath::Clamp(S, 0, S_Num - 1)].Name; }
	float BoardAt(int32 S) { return Specs[FMath::Clamp(S, 0, S_Num - 1)].Board; }

	float RollSize(int32 S)
	{
		const FSpec& P = Specs[FMath::Clamp(S, 0, S_Num - 1)];
		// Box-Muller: one standard normal from two even randoms
		const float U1 = FMath::Max(FMath::FRand(), 1.0e-7f), U2 = FMath::FRand();
		const float Z = FMath::Sqrt(-2.f * FMath::Loge(U1)) * FMath::Cos(2.f * PI * U2);
		const float X = FMath::Clamp(P.Mean + P.Sd * Z, P.Lo, P.Hi);
		return P.bWhole ? float(FMath::RoundToInt(X)) : FMath::RoundToFloat(X * 10.f) / 10.f;
	}

	FString SizeText(int32 S, float Size)
	{
		if (S == S_Antlers) return FString::Printf(TEXT("%d points"), FMath::RoundToInt(Size));
		return FString::Printf(TEXT("%.1f in"), Size);
	}

	void WhoIs(AActor* Knight, FString& OutKey, FString& OutName)
	{
		OutKey = TEXT("local"); OutName = TEXT("You");
		const APawn* P = Cast<APawn>(Knight);
		if (const AKnightPlayerController* PC = P ? Cast<AKnightPlayerController>(P->GetController()) : nullptr)
			if (PC->bLoggedIn && !PC->AccountKey.IsEmpty()) OutKey = PC->AccountKey;
		if (const APlayerState* PS = P ? P->GetPlayerState() : nullptr)
			if (!PS->GetPlayerName().IsEmpty()) OutName = PS->GetPlayerName();
	}

	void Record(UWorld* World, const FString& AccountKey, const FString& Name, int32 S, float Size, FString& OutNote)
	{
		OutNote.Reset();
		if (S < 0 || S >= S_Num || Size <= 0.f) return;
		const int32 Port = PortOf(World);
		FFile& F = Own(Port);
		bool bChanged = false;
		FEntry E; E.S = S; E.Size = Size; E.Who = Name; E.Key = AccountKey; E.T = FDateTime::UtcNow().ToUnixTimestamp();

		// the all-time board (every world): is it the largest anyone has?
		TArray<FFile> Every;
		All(Port, Every);
		float Largest = 0.f;
		for (const FFile& X : Every) for (const FEntry& T : X.Top) if (T.S == S) Largest = FMath::Max(Largest, T.Size);

		// this world's top 10 per species
		int32 Mine = 0; float Smallest = 1.0e9f; int32 SmallestAt = -1;
		for (int32 i = 0; i < F.Top.Num(); ++i) if (F.Top[i].S == S) { ++Mine; if (F.Top[i].Size < Smallest) { Smallest = F.Top[i].Size; SmallestAt = i; } }
		if (Mine < KeepTop) { F.Top.Add(E); bChanged = true; }
		else if (Size > Smallest) { F.Top[SmallestAt] = E; bChanged = true; }

		if (Size >= BoardAt(S))
		{
			F.Board.Add(E);
			if (F.Board.Num() > KeepBoard) F.Board.RemoveAt(0, F.Board.Num() - KeepBoard);
			bChanged = true;
		}

		FBest& B = F.Best.FindOrAdd(AccountKey);
		float Before = B.B[S];
		for (const FFile& X : Every) if (const FBest* O = X.Best.Find(AccountKey)) Before = FMath::Max(Before, O->B[S]);
		if (Size > B.B[S] || B.Name != Name) { B.B[S] = FMath::Max(B.B[S], Size); B.Name = Name; bChanged = true; }

		if (Size > Largest) OutNote = FString::Printf(TEXT("The largest %s anyone has ever landed!"), S == S_Antlers ? TEXT("set of antlers") : *FString(SpeciesName(S)).ToLower());
		else if (Size >= BoardAt(S)) OutNote = TEXT("A trophy! It goes on the weekly board.");
		else if (Size > Before) OutNote = TEXT("A new personal best!");
		if (bChanged) Save(FileFor(Port), F);
		UE_LOG(LogTemp, Log, TEXT("JEEBRECORDS %s (%s) %s %s on world %d (best before %.1f, all-time %.1f, board at %.0f) %s"), *Name, *AccountKey, SpeciesName(S), *SizeText(S, Size), Port, Before, Largest, BoardAt(S), *OutNote);
	}

	FString Report(UWorld* World, const FString& AccountKey)
	{
		const int32 Port = PortOf(World);
		TArray<FFile> Every;
		All(Port, Every);
		FString R;
		auto Line = [&](const FString& L) { R += L; R += TEXT("\n"); };

		Line(TEXT("T|Your largest"));
		float Mine[S_Num] = { 0.f, 0.f, 0.f, 0.f };
		for (const FFile& X : Every) if (const FBest* B = X.Best.Find(AccountKey)) for (int32 s = 0; s < S_Num; ++s) Mine[s] = FMath::Max(Mine[s], B->B[s]);
		for (int32 s = 0; s < S_Num; ++s)
			Line(FString::Printf(TEXT("R|%s|%s|%s|"), SpeciesName(s), Mine[s] > 0.f ? *SizeText(s, Mine[s]) : TEXT("-"),
				Mine[s] >= BoardAt(s) ? TEXT("trophy") : *FString::Printf(TEXT("trophy at %s"), *SizeText(s, BoardAt(s)))));

		const int64 Week = WeekStart();
		for (int32 pass = 0; pass < 2; ++pass)
		{
			Line(pass == 0 ? TEXT("T|All-time largest (every world)") : TEXT("T|This week's trophies (since Sunday midnight)"));
			for (int32 s = 0; s < S_Num; ++s)
			{
				TArray<FEntry> L;
				for (const FFile& X : Every) for (const FEntry& E : (pass == 0 ? X.Top : X.Board)) if (E.S == s && (pass == 0 || E.T >= Week)) L.Add(E);
				L.Sort([](const FEntry& A, const FEntry& B) { return A.Size > B.Size; });
				FString Cells;
				for (int32 i = 0; i < FMath::Min(L.Num(), 3); ++i) Cells += FString::Printf(TEXT("|%d. %s  %s"), i + 1, *L[i].Who, *SizeText(s, L[i].Size));
				if (L.Num() == 0) Cells = pass == 0 ? TEXT("|none yet") : FString::Printf(TEXT("|none yet (%s or more)"), *SizeText(s, BoardAt(s)));
				Line(FString::Printf(TEXT("R|%s%s"), SpeciesName(s), *Cells));
			}
		}
		return R;
	}
}

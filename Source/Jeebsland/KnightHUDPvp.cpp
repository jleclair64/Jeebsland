// PvP zones on the screen (owner, 2026-11-23): a red title when you walk into one (the way the towns' names show, in red) and a small
// crossed-swords icon at the top of the screen for as long as you are inside. PvpZones.h has the zones; the server decides the damage.
#include "KnightHUD.h"
#include "PvpZones.h"
#include "Engine/Canvas.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

void AKnightHUD::DrawPvp(const FVector& Where, float Now)
{
	const int32 Z = JeebPvp::ZoneAt(float(Where.X), float(Where.Y));
	if (Z != PvpIn)
	{
		if (Z != INDEX_NONE && Now > 1.5f) { PvpBannerT = Now; PvpTitle = JeebPvp::Zones()[Z].Title; }
		PvpIn = Z;
	}
	const FLinearColor Red(0.92f, 0.10f, 0.08f, 1.f);
	const float Age = Now - PvpBannerT;
	static const bool bShot = FParse::Param(FCommandLine::Get(), TEXT("ShotPvpTitle"));   // (test pictures: the title stays up)
	if (PvpBannerT > 0.f && (Age < 5.5f || (bShot && Z != INDEX_NONE)))
	{
		const float A = bShot ? 1.f : FMath::Clamp(FMath::Min(Age / 0.6f, (5.5f - Age) / 1.2f), 0.f, 1.f);
		const float H = FMath::Clamp(Canvas->ClipY * 0.049f, 30.f, 62.f);
		TitleTint = Red;
		TitleBanner(PvpTitle, Canvas->ClipY * 0.15f, H, A);
		TitleTextAt(TEXT("PVP ZONE"), -1.f, Canvas->ClipY * 0.15f + H * 1.75f, H * 0.5f, A);
		TitleTint.Reset();
	}
	if (Z == INDEX_NONE) return;
	// the icon: a dark red plate with two crossed swords, a gentle pulse
	const float S = FMath::Clamp(Canvas->ClipY * 0.06f, 42.f, 66.f);
	const float X = Canvas->ClipX * 0.5f - S * 0.5f, Y = 10.f;
	const float Pulse = 0.5f + 0.5f * FMath::Sin(Now * 3.2f);
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f), X - 2.f, Y - 2.f, S + 4.f, S + 4.f);
	DrawRect(FLinearColor(0.30f, 0.03f, 0.03f, 0.92f), X, Y, S, S);
	const FLinearColor Rim(0.95f, 0.18f, 0.12f, 0.65f + 0.35f * Pulse);
	const float T = 2.f;
	DrawRect(Rim, X, Y, S, T); DrawRect(Rim, X, Y + S - T, S, T); DrawRect(Rim, X, Y, T, S); DrawRect(Rim, X + S - T, Y, T, S);
	const FLinearColor Blade(0.95f, 0.95f, 0.97f, 1.f), Hilt(1.f, 0.78f, 0.35f, 1.f);
	const float P = S * 0.20f;
	for (int32 Sign = -1; Sign <= 1; Sign += 2)
	{
		const FVector2D A(X + (Sign < 0 ? P : S - P), Y + P), B(X + (Sign < 0 ? S - P : P), Y + S - P);   // (tip to hilt)
		DrawLine(A.X, A.Y, B.X, B.Y, Blade, FMath::Max(2.f, S * 0.07f));
		const FVector2D D = (B - A).GetSafeNormal(), Pr(-D.Y, D.X), G = A + (B - A) * 0.68f;   // (the crossguard)
		DrawLine(G.X - Pr.X * S * 0.14f, G.Y - Pr.Y * S * 0.14f, G.X + Pr.X * S * 0.14f, G.Y + Pr.Y * S * 0.14f, Hilt, FMath::Max(2.f, S * 0.07f));
	}
}

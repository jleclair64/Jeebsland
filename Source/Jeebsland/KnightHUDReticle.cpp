#include "KnightHUD.h"
#include "KnightCharacter.h"
#include "Engine/Canvas.h"

// The wand's and staff's reticle (owner 2026-11-18: "a reticle for range items (that don't zoom in, so not including the bows) like wands and
// staffs"): a small clean ring with a dot, up and right of the middle of the screen where the owner marked it, clear of the knight. The bolt
// flies to whatever is under it (AKnightCharacter::CastAimPoint). A thin dark edge keeps it readable on bright sky and dark ground alike.
void AKnightHUD::DrawCastReticle(AKnightCharacter* Knight)
{
	if (!Knight || !Canvas || !Knight->ShowsCastReticle()) return;
	const FVector2D At = AKnightCharacter::CastReticleAt();
	const float Cx = Canvas->ClipX * At.X, Cy = Canvas->ClipY * At.Y;
	const float S = FMath::Clamp(Canvas->ClipY / 1080.f, 0.75f, 2.f);   // (the same size on any screen)
	const float R = 9.f * S, Th = 1.1f * S;
	const FLinearColor Edge(0.f, 0.f, 0.f, 0.45f), Col(1.f, 1.f, 1.f, 0.85f);
	auto Ring = [&](float Rad, float Half, const FLinearColor& C)
	{
		constexpr int32 N = 24;
		for (int32 s = 0; s < N; ++s)
		{
			const float A0 = 2.f * PI * s / N, A1 = 2.f * PI * (s + 1) / N;
			Poly({ FVector2D(Cx + (Rad + Half) * FMath::Cos(A0), Cy + (Rad + Half) * FMath::Sin(A0)), FVector2D(Cx + (Rad + Half) * FMath::Cos(A1), Cy + (Rad + Half) * FMath::Sin(A1)),
			       FVector2D(Cx + (Rad - Half) * FMath::Cos(A1), Cy + (Rad - Half) * FMath::Sin(A1)), FVector2D(Cx + (Rad - Half) * FMath::Cos(A0), Cy + (Rad - Half) * FMath::Sin(A0)) }, C);
		}
	};
	Ring(R, Th + 0.9f * S, Edge);
	Ring(R, Th, Col);
	Disc(Cx, Cy, 2.6f * S, 2.6f * S, Edge);
	Disc(Cx, Cy, 1.6f * S, 1.6f * S, Col);
}

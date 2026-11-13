// Oak Hill's building footprints for the grass: where each model really stands on the ground (its walls filled solid, plus the
// steps, porch floors, posts and plinths lower than 60 cm), in 25 cm cells of the model's own frame (Blender's, cm, before
// Scale; Unreal mirrors its Y). Porch corners, forge yards and the ground under eaves are not in it, so the grass grows there.
// Made from Saved/KnightBuild/Buildings/<Model>.fbx by a Blender scan (ground agent, 2026-11-13): remake it when a model changes.
// Each Hex: NX columns (along X) of NY bits (along Y, from Y0 up), 4 bits a digit, the first bit the highest.
#pragma once

namespace JeebOakHill
{
	struct FFootprint { const TCHAR* Model; float X0, Y0; int32 NX, NY; const char* Hex; };
	constexpr float FootCell = 25.f;
	inline const FFootprint* Footprints(int32& OutNum)
	{
		static const FFootprint F[] = {
			{ TEXT("OakHillHouse"), -622.3f, -318.5f, 49, 30,
				"000fc000000fc0007ffffffc7ffffffcfffffffcfffffffc7ffffffc7ffffffc7ffffffc7ffffffc7ffffffc7ffffffc7ffffffc7ffffffc7ffffffc"
				"7ffffffcfffffffcfffffffc7ffffffc7ffffffc7ffffffc7ffffffcfffffffcfffffffcfffffffcfffffffcfffffffcfffffffcfffffffcfffffffc"
				"7ffffffc7ffffffc7ffffffc7ffffffcfffffffcfffffffc7ffffffc7ffffffc7ffffffc7ffffffc7ffffffc7ffffffc7ffffffc7ffffffc7ffffffc"
				"fffffffcfffffffc7ffffffc7ffffffc"
			},
			{ TEXT("OakHillCornerHouse"), -435.5f, -484.5f, 32, 38,
				"0701ffc00007fffffff807fffffff807fffffff807fffffff807fffffff87ffffffff8fffffffff8fffffffff8fffffffff8fffffffff8fffffffff8"
				"fffffffff8fffffffff8fffffffff8fffffffff8fffffffff8fffffffff80ffffffff80ffffffff80ffffffff80ffffffff80ffffffff80ffffffff8"
				"07fffffff807fffffff807fffffff807fffffff807fffffff807fffffff807fffffff80000000000"
			},
			{ TEXT("OakHillSmith"), -734.1f, -562.9f, 59, 45,
				"000081fffff0701ffffffff0f01ffffffff0f01ffffffff0f03ffffffff0003ffffffff0003ffffffff0003ffffffff0003ffffffff0003ffffffff0"
				"00fffffffff000fffffffff000fffffffff000fffffffff000fffffffff000fffffffff000fffffffff000fffffffff000fffffffff000fffffffff0"
				"00fffffffff000fffffffff000fffffffff000fffffffff000fffffffff000fffffffff000fffffffff0001ffffffff0001ffffffff0f01ffffffff0"
				"f01ffffffff0f01ffffffff0001ffffffff0001ffffffff0001ffffffff0001ffffffff0001ffffffff0001ffffffff0001ffffffff0001ffffffff0"
				"001ffffffff0001ffffffff0001ffffffff0001ffffffff0001ffffffff0001ffffffff0001ffffffff0001ffffffff0001ffffffff0001ffffffff0"
				"001ffffffff0001ffffffff0001ffffffff0001ffffffff0001ffffffff0001ffffffff0001ffffffff0001ffffffff0000000000000"
			},
			{ TEXT("OakHillBank"), -645.5f, -702.7f, 52, 55,
				"000201c040007c000ffffffffffc001ffffffffffc001ffffffffffc000ffffffffffc000ffffffffffc000ffffffffffc000ffffffffffc000fffff"
				"fffffc000ffffffffffc000ffffffffffc000ffffffffffc000ffffffffffc000ffffffffffc000ffffffffffc000ffffffffffc03fffffffffffcff"
				"fffffffffffcfffffffffffffcfffffffffffffc7ffffffffffffc7ffffffffffffc7ffffffffffffc7ffffffffffffc7ffffffffffffc7fffffffff"
				"fffc7ffffffffffffc7ffffffffffffc7ffffffffffffc7ffffffffffffc7ffffffffffffc7ffffffffffffcfffffffffffffcfffffffffffffcffff"
				"fffffffffc01fffffffffffc000ffffffffffc000ffffffffffc000ffffffffffc000ffffffffffc000ffffffffffc000ffffffffffc000fffffffff"
				"fc000ffffffffffc000ffffffffffc000ffffffffffc000ffffffffffc000ffffffffffc001ffffffffffc001ffffffffffc000ffffffffffc000000"
				"00000000"
			},
			{ TEXT("OakHillStore"), -635.3f, -608.1f, 49, 48,
				"007fc0001fc03fffe0003fe03ffff0007ff03ffff01ffff03ffff03ffff03ffff03ffff03ffffffffffc7ffffffffffcfffffffffffcffffffe083fc"
				"ffffffe083fcffff002083fcfff0002083f80c100007fff80c00000003f80c00000003f80c00208003f80c00000003fc0c00000200fcfc00000000fc"
				"fc10084010fcfc000000001c7c000000001c7f010004021c7e008100001c7e000000007c7e000000407cfe0000000078fe04041000787fe4000003f8"
				"7ff4800000387ff4800080387fff8080003c7fff87fe003c7ffffffe1ffc301ffffe10fc001ffffff0fc001ffffff0fc001ffffff0fc001ffffff0ff"
				"001fffffffff001fffffffff001fffffffff0000007ffffe0000007ffffe0000007ffffe0000007ffffe0000000fffc000000007ff80"
			},
			{ TEXT("OakHillPotionShop"), -489.6f, -435.4f, 42, 35,
				"0f800007c0fffffffc0fffffffc0fffffffc0fffffffc0fffffffc0fffffffc0fffffffc0fffffffc0fffffffc0fffffffc0fffffffcffffffffcfff"
				"fffffcffffffffcffffffffcffffffffcffffffffcffffffffcffffffffcffffffffcffffffffc0fffffffc0fffffffc0fffffffc0fffffffc0fffff"
				"ffc0fffffffc0fffffffc0fffffffc0fffffffc0fffffffc0fffffffc0fffffffc0fffffffc0fffffffc0fffffffc0fffffffc0fffffffc0fffffffc"
				"0fffffffc000000000"
			},
			{ TEXT("OakHillTownHall"), -1014.3f, -610.3f, 82, 48,
				"000f0000003e001fffffffff001fffffffff001ffffffffe001ffffffffe001ffffffffe001ffffffffe001ffffffffe001ffffffffe001ffffffffe"
				"001ffffffffe001ffffffffe001ffffffffe001ffffffffe001ffffffffe001ffffffffe001ffffffffe001ffffffffe001ffffffffe001ffffffffe"
				"001ffffffffe001ffffffffe001ffffffffe001ffffffffe001ffffffffe001ffffffffe001ffffffffe001ffffffffe001ffffffffe001ffffffffe"
				"001ffffffffe001ffffffffefffffffffffefffffffffffefffffffffffe7ffffffffffe7ffffffffffe7ffffffffffe7ffffffffffefffffffffffe"
				"fffffffffffefffffffffffefffffffffffefffffffffffe7ffffffffffe7ffffffffffe7ffffffffffe7ffffffffffefffffffffffefffffffffffe"
				"fffffffffffe001ffffffffe001ffffffffe001ffffffffe001ffffffffe001ffffffffe001ffffffffe001ffffffffe001ffffffffe001ffffffffe"
				"001ffffffffe001ffffffffe001ffffffffe001ffffffffe001ffffffffe001ffffffffe001ffffffffe001ffffffffe001ffffffffe001ffffffffe"
				"001ffffffffe001ffffffffe001ffffffffe001ffffffffe001ffffffffe001ffffffffe001ffffffffe001fffffffff001fffffffff001fffffffff"
				"001f0f80f83e000000000000"
			},
			{ TEXT("OakHillChurch"), -616.5f, -734.1f, 49, 60,
				"001fffffff00000001fffffff0000007ffffffffc00000fffffffffc00001fffffffffffff81fffffffffffff81fffffffffffff81fffffffffffff8"
				"1fffffffffffff83fffffffffffff83fffffffffffff87fffffffffffff87fffffffffffffc7fffffffffffffc7fffffffffffffcffffffffffffffc"
				"ffffffffffffffcffffffffffffffcffffffffffffffcffffffffffffffeffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
				"ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
				"7fffffffffffffe3fffffffffffffe3fffffffffffffe3fffffffffffffc1fffffffffffff81fffffffffffff81fffffffffffff81fffffffffffff8"
				"1fffffffffffff81fffffffffffffc1fffffffffffffe1fffffffffffffc1fffffffffffff81fffffffffffff81fffffffffffff81fffffffffffff8"
				"003ffffffffe000"
			},
			{ TEXT("OakHillWell"), -176.9f, -143.5f, 13, 12,
				"7f8ffcfffffffffffffffffffffffffffffffff"
			},
		};
		OutNum = UE_ARRAY_COUNT(F);
		return F;
	}
}

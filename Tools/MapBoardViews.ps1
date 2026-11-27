# More stages for Tools\MapBoard.ps1 (it reads this file if it is here).
# fixes (2026-11-26, agent mapfix): the follow-ups to the map checks. The first four are views of the cliffs board, so their "before"
# is the second pass's picture; the rest are new views (after only).
$Stages['fixes'] = @(
	@{ N = 'The first cove''s beach, looking north';      E = '46700_93000_220_25_2_0' },
	@{ N = 'On the cliff top by the lip';                E = '60300_95300_190_165_-14_0' },
	@{ N = 'Ledges, from the sea';                       E = '60500_101800_2800_-80_6_0' },
	@{ N = 'Foot of the wall, close';                    E = '66500_97200_2300_-95_8_0' },
	@{ N = 'Heather, close (new view)';                  E = '7250_90750_170_40_-14_0' },
	@{ N = 'Heather at your feet (new view)';            E = '7250_90750_110_40_-35_0' },
	@{ N = 'Heather drift at 10 to 60 m (new view)';     E = '6400_90000_500_40_-9_0' },
	@{ N = 'Heather at the low sun (new view)';          E = '1750_80500_170_200_-14_0.26' },
	@{ N = 'Down the first cove''s glen to the sea (new view)'; E = '47500_87800_260_90_-14_0' },
	@{ N = 'The first cove from the sea (new view)';     E = '47500_96500_2200_-90_-6_0' },
	@{ N = 'The first cove from the air (new view)';     E = '47500_99500_4500_-90_-22_0' },
	@{ N = 'The glen from its north side (new view)';    E = '51000_90500_500_195_-10_0' }
)

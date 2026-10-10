#pragma once

#include "CoreMinimal.h"

// ===================================================================================================
// The lore of Jeebsland, told in fragments. Villagers each carry a few; talk to them again to hear the next one.
// Heard fragments are kept in the Lore tab (press O). A fragment's text uses '|' to split it into dialogue lines.
// The full picture (never told outright) is in LORE.md.
// ===================================================================================================
namespace JeebLore
{
	enum ECat : int32 { C_Events, C_Figures, C_Places, C_Rumours, NumCats };
	inline const TCHAR* CatName(int32 C)
	{
		static const TCHAR* N[NumCats] = { TEXT("EVENTS"), TEXT("FIGURES"), TEXT("PLACES"), TEXT("RUMOURS") };
		return N[FMath::Clamp(C, 0, NumCats - 1)];
	}

	// Who tells it: 0 Brindle, 1 Hobb, 2 Sir Aldous, 3..12 the ten villagers, 13 the banker
	constexpr int32 NumVillagers = 10;
	constexpr int32 BankerSpeaker = 3 + NumVillagers;
	inline const TCHAR* SpeakerName(int32 S)
	{
		static const TCHAR* N[4 + NumVillagers] = {
			TEXT("Brindle, the Trader"), TEXT("Hobb Ironhand"), TEXT("Sir Aldous, Gate Knight"),
			TEXT("Mabry Thistlewick"), TEXT("Tobin Quill"), TEXT("Dame Orsolya Vane"), TEXT("Garrick Lowe"), TEXT("Wren Ashby"),
			TEXT("Old Fenwick"), TEXT("Sable Marlowe"), TEXT("Brother Ansel"), TEXT("Peregrine Pott"), TEXT("Hettie Bram"), TEXT("Quillon Pell, the Banker") };
		return N[FMath::Clamp(S, 0, 3 + NumVillagers)];
	}

	struct FEntry { int32 Cat; int32 Speaker; const TCHAR* Title; const TCHAR* Text; };
	inline const TArray<FEntry>& Entries()
	{
		static const TArray<FEntry> E = {
			// ---- Brindle ----
			{ C_Events,  0, TEXT("The Long Fall"),            TEXT("They say the last leaf fell the day a handshake went on too long.|Nobody has seen a bud since. We call it the Long Fall, as if it might finish someday.") },
			{ C_Places,  0, TEXT("The Mark on the Ingot"),    TEXT("Every ingot has a tiny mark pressed in one corner. Three kinds, and I would rather not guess what they stand for.|I have sold thousands of them and never once asked where they end up.") },
			{ C_Figures, 0, TEXT("The Collectors"),               TEXT("Men came each harvest with smooth gold faces, counting our sacks twice and smiling once.|My father stopped meeting their eyes. I started counting his sacks instead.") },
			{ C_Rumours, 0, TEXT("The Midnight Fountain"),    TEXT("The fountain in the plaza runs uphill at midnight. The pigs will not drink from it after dark.|Something below breathes, they say, and the water goes wherever it breathes.") },
			{ C_Rumours, 0, TEXT("The Polite Candles"),       TEXT("The candles that float over the plaza at dusk were never lit by anyone. A lamplighter once tried to catch one.|It let him hold it, quite politely, and then went back up. Manners, you see.") },
			// ---- Hobb ----
			{ C_Places,  1, TEXT("The Warm Ore"),             TEXT("Dig deep enough in the copper hills and the rock is warm as a loaf.|Warm rock means something is sleeping close, and it would prefer not to be woken.") },
			{ C_Figures, 1, TEXT("The Warm Wind"),                   TEXT("My fire needs no bellows when the wind blows from the hills. I have stopped asking whose breath it is.|The old miners never say what lives under there. They say 'her', and lower their voices.") },
			{ C_Events,  1, TEXT("The Ember Year"),           TEXT("One year the sky was the colour of my forge for forty days. Roofs burned. Wells boiled.|Somebody paid something to make it stop, and it stopped. Funny that nobody I know was ever paid.") },
			{ C_Rumours, 1, TEXT("The Singing Blade"),        TEXT("I forged a sword once from the ingots the Collectors left behind. It hummed at night.|I melted it. A sword that sings is a sword that wants something.") },
			{ C_Figures, 1, TEXT("The Empty Helm"),           TEXT("A helm came to my forge once with nobody in it and asked me to straighten a dent.|I did. The dent was on the inside.") },
			// ---- Sir Aldous ----
			{ C_Figures, 2, TEXT("Sir Morrow, the Late Knight"), TEXT("After dusk we say 'the Late Knight'. His name was Morrow, and he once refused an order.|One order. The moon went out for nine nights over it.") },
			{ C_Events,  2, TEXT("The Hollow Night"),         TEXT("Nine nights with no moon. The bats came down out of the sky like soot.|On the tenth morning every gold mask lay in the road, empty and smiling. The men who wore them were simply gone.") },
			{ C_Places,  2, TEXT("The Door in the Graveyard"), TEXT("Behind the village the graveyard has a door in it. Not a gate. A door.|I stood watch there once. The door stood watch back.") },
			{ C_Rumours, 2, TEXT("The Rider on the East Road"), TEXT("On still nights a rider passes on the east road. No shadow. The horse makes no sound.|If he passes, nod. It is only polite.") },
			{ C_Places,  2, TEXT("The Tithe Road"),           TEXT("Before the walls there was the Tithe Road, wide as three carts. Every autumn it filled with sacks, bound for the hill.|The village grew right over it. A people can build a home on top of its own shame.") },
			// ---- Mabry ----
			{ C_Events,  3, TEXT("A Tea for the Hollow Night"), TEXT("I brewed a tea once that let you taste what the moon forgot.|It tasted like a name. I will not say whose.") },
			{ C_Rumours, 3, TEXT("The Winking Pumpkin"),      TEXT("The big pumpkins in the field wink. Only in the rain, and only at children.|Grown folk who say they have seen it are either lying or very lucky.") },
			{ C_Places,  3, TEXT("The Shy Moon"),             TEXT("She goes from full to fingernail in a quarter of an hour, you will have noticed. She is hiding from something.|Since the Hollow Night she never stays up long. Can you blame her?") },
			// ---- Tobin ----
			{ C_Figures, 4, TEXT("Pip and the Lantern"),      TEXT("There is a lantern at the top of the biggest redwood and it has never gone out. Gran says a boy named Pip carried it up.|Gran says he is still up there. Gran says a lot of things.") },
			{ C_Rumours, 4, TEXT("The Pig Who Knew"),         TEXT("Hettie's pig told me a secret. It said: 'Do not eat what is in the middle of the pond.'|Then it ate my sandwich. I think it is a spy.") },
			{ C_Places,  4, TEXT("The Rope That Climbs"),     TEXT("The rope ladders in the redwoods were already there when the first Jeeb came. Nobody tied them.|They go up, and then up again, and sometimes you wake at the bottom with leaves in your hair.") },
			// ---- Dame Orsolya ----
			{ C_Figures, 5, TEXT("Eleven Sacks"),            TEXT("They called it a tax. Eleven sacks of every twelve, and the twelfth already spoiled.|My mother carried sacks to the hill until her back bent like a bow. She never learned who ate them.") },
			{ C_Events,  5, TEXT("The Count of Sacks"),       TEXT("The year my brother starved, the collectors counted ten thousand sacks and a thousand ingots into the hill.|I know because I held the tally-stick. I hold it still. See my knuckles?") },
			{ C_Rumours, 5, TEXT("Who Was Fed"),              TEXT("Folk say the sacks went to feed something below the hill. Perhaps they did. But monsters do not wear gold masks.|Somebody ate well while we starved. Find me that somebody.") },
			// ---- Garrick ----
			{ C_Places,  6, TEXT("The Copper Hills"),         TEXT("Three hundred years of picks and nobody has found the bottom. The deeper you go, the older the pick marks.|Last week I found a mark made by something with claws.") },
			{ C_Figures, 6, TEXT("Breath on the Hill"),     TEXT("On cold autumn mornings the hills sigh out a warm wind. Nobody says whose breath it is.|When it stops, miners go home early. When it stutters, they go home and do not speak.") },
			{ C_Events,  6, TEXT("Deepgate"),                 TEXT("Forty miners went down to fill the sacks. Thirty-nine came up. The collectors counted heads and wrote 'forty'.|The extra one is still counted. They say he waves.") },
			// ---- Wren ----
			{ C_Rumours, 7, TEXT("Midnight Dough"),           TEXT("Leave dough out on the anniversary of the Hollow Night and it rises into shapes. Mine made a crown once.|I ate it quickly. You do not leave a crown lying about.") },
			{ C_Events,  7, TEXT("The Last Spring"),          TEXT("My great-grandmother kept a seed in a button. She said it was from before the Long Fall.|It has never sprouted. But on cold mornings the button is warm.") },
			{ C_Rumours, 7, TEXT("The Coin in the Crumbs"),   TEXT("Crumbs I leave on the sill are gone by morning, and sometimes there is a coin. A very old coin, with something coiled on it.|I stopped asking. I started leaving more crumbs.") },
			// ---- Old Fenwick ----
			{ C_Places,  8, TEXT("The Door Beneath"),         TEXT("Forty-one years I have dug here and never struck bottom in the north-east corner. The earth just keeps being earth.|Last autumn, something dug back.") },
			{ C_Figures, 8, TEXT("The Tired One"),               TEXT("Something keeps a door shut down there. That is all it does. I think it is tired.|On feast days I leave it a pipe. The pipe is always gone. The pipe is always, somehow, lit.") },
			{ C_Rumours, 8, TEXT("The Nameless Stones"),      TEXT("Some of the oldest graves have no names. Just a gold mask carved where the name should be.|The mask has been smiling wider every year.") },
			// ---- Sable ----
			{ C_Figures, 9, TEXT("The Knot"),                 TEXT("The pond is deeper than the hill is tall. I lowered a line for a day and a night. It came up warm and wet and tied in a knot I could not undo.|Only a very patient thing ties a knot like that.") },
			{ C_Rumours, 9, TEXT("The Golden Carp"),          TEXT("The golden carp is not a fish. It is a scale that learned to swim.|Catch it and you have borrowed something. Something will want it back.") },
			{ C_Events,  9, TEXT("The Drowned Harvest"),      TEXT("Gold-faced men tipped a whole season's sacks into the pond one night. The water took it the way a throat does.|The pond has been quieter, and fuller, ever since.") },
			// ---- Brother Ansel ----
			{ C_Figures, 10, TEXT("The Polite Man"),                TEXT("The first Jeeb was no king. He was a very polite man who once asked for a favour, and was granted it.|The favour was autumn. Nobody likes to talk about the bill.") },
			{ C_Places,  10, TEXT("The Lantern Moon"),        TEXT("I light the lamps so the dark knows where we are, politely. The moon is an old lantern someone left us, and she is running low.|When she goes out for good, I would like everything to have been tidy.") },
			{ C_Events,  10, TEXT("What Is Owed"),         TEXT("Something is owed every generation, in three kinds of coin. The elders know which, and will not say it twice.|Pay, and the autumn holds. Forget, and whoever was owed remembers.") },
			// ---- Peregrine ----
			{ C_Figures, 11, TEXT("The Song of Morrow"),      TEXT("Verse six is where the knight takes off his helm and nobody screams, because there is nothing there to scream at.|Verse seven I have never been allowed to finish. The tavern goes quiet. The candles lean.") },
			{ C_Events,  11, TEXT("The Hollow Night, Sung"),  TEXT("Nine nights unlit, and on the tenth the sun rose backwards, which is to say it rose and apologised.|Do not ask a bard what that means. We only rhyme it.") },
			{ C_Rumours, 11, TEXT("Three Things in the Dark"), TEXT("Every old song has three things in the dark: one that burns, one that coils, one that waits behind a door.|I used to think it was just a good rhythm.") },
			// ---- Hettie ----
			{ C_Rumours, 12, TEXT("The Royal Pigs"),          TEXT("King Jeeb the Third was, depending on who you ask, a pig. The pigs agree and refuse to say more.|They hold a meeting in the pen at midnight. Very dignified. I stand at the back.") },
			{ C_Events,  12, TEXT("The Tithe Pigs"),          TEXT("The Collectors took every fat pig they could find, right up until the winter the pigs began to look back at them.|After that the Collectors came in groups. Then they stopped coming.") },
			// ---- Quillon Pell, the banker ----
			{ C_Places,  13, TEXT("The Vault Below"),         TEXT("The vault goes down further than the building goes up. Every ingot you leave me sinks a little deeper.|I have never counted the stairs. The last man who did is still counting.") },
			{ C_Events,  13, TEXT("What the Collectors Banked"),  TEXT("The Collectors kept their ledgers in this vault. Every page is blank now, save one word on each, written small: 'Owed'.|I keep the books dry. It seems the polite thing to do.") },
			{ C_Rumours, 13, TEXT("The Unclaimed Boxes"),     TEXT("Some boxes in the vault have no owner and no key. The keyhole is shaped like a smiling mask.|They hum when it rains.") },
			{ C_Figures, 12, TEXT("The Mask in the Mud"),     TEXT("I found a gold mask half-buried by the pen. Smiling. Heavy as a promise.|The pigs would not go near it. Pigs know.") },
			// ---- Named figures of Jeebsland ----
			{ C_Figures, 2,  TEXT("Koehler the Great"),        TEXT("Koehler the Great stood at the graveyard door alone, the night something first knocked.|They say he never drew his sword. He simply asked it to wait, and it did. I have tried the same and it has never once worked.") },
			{ C_Figures, 2,  TEXT("Max the Warrior"),          TEXT("Max is the best blade this gate ever trained, and the loudest about it.|He left for the east road to look for Sir Morrow. He wrote once. The letter was only a drawing of an empty helmet.") },
			{ C_Figures, 0,  TEXT("Tiki the Merchant"),        TEXT("Tiki sells from a painted cart on the ring road and haggles entirely in rhyme.|Her prices are fair and her scales are not. I have never caught her at it, which worries me.") },
			{ C_Figures, 0,  TEXT("Matthew and the Price War"),TEXT("Matthew sells exactly what Tiki sells, one ingot cheaper, from a cart painted exactly like hers.|They have been at war for eleven years. The village has never eaten so well.") },
			{ C_Figures, 11, TEXT("Kieran the Bard"),          TEXT("Kieran sings the Ballad of Sir Morrow better than I do, which I forgive him for, slowly.|But he always stops before the last verse, and when I ask, he says the song asked him to.") },
			{ C_Figures, 7,  TEXT("Randy, Who Cannot Help It"),TEXT("Randy has courted every baker, every widow and one very startled scarecrow in this valley.|He swears he is sincere every time. The odd thing is, he is. The scarecrow remains undecided.") },
			{ C_Figures, 3,  TEXT("Isaac the Healer"),         TEXT("Isaac mends bones, fevers and broken hearts, in that order of difficulty.|He told me once that after the Hollow Night he treated a patient with no wounds, no breath and a perfectly good appetite.") },
			{ C_Figures, 1,  TEXT("Tokugawa Tash"),            TEXT("The katana on my rack is copied from the blade of Tokugawa Tash, the ancient warrior from beyond the sea.|Tash came here chasing something older than any of us, and found a village instead. He stayed. He never said what he was chasing.") },
			{ C_Figures, 6,  TEXT("Grant the Giant"),          TEXT("Grant is taller than the mine door and has to be talked down the shafts like a nervous horse.|The collectors wrote him in their ledgers as nine men. He worked every shift that way and was paid for one.") },
			{ C_Figures, 13, TEXT("Brasskey the Locksmith"),   TEXT("The vault's lock was made by Brasskey, the ancient locksmith, who swore no key could ever be cut for it.|Brasskey is long gone. The keyhole, I notice, has been getting a little bigger.") },
			{ C_Figures, 5,  TEXT("Jeebs the Leader"),         TEXT("Jeebs leads us. He is always kind, always calm, and he alone among the great houses never wore a mask.|Some say that is because he has nothing to hide. I say a man with nothing to hide does not need so many doors.") },
			{ C_Rumours, 10, TEXT("What Jeebs Promised"),      TEXT("Jeebs gives a blessing at every harvest, and the leaves hold on a little longer each time.|Brother Ansel asks, quietly, whether the autumn is holding on for us, or for him.") },
		};
		return E;
	}
	// Indices of the fragments a speaker tells, in order
	inline TArray<int32> BySpeaker(int32 Speaker)
	{
		TArray<int32> Out;
		const TArray<FEntry>& E = Entries();
		for (int32 i = 0; i < E.Num(); ++i) if (E[i].Speaker == Speaker) Out.Add(i);
		return Out;
	}
}

# Vanilla target classification audit

This audit supports the living-humanoid eligibility rule in version 0.3.0. It
examined the 99 `RACE` records in the exact Skyrim 1.7.104 data file used for
development:

- File: `Skyrim.esm`
- Size: 249,752,131 bytes
- SHA-256: `E198C3B85E5E48E0C92A6580D8F66E644256B68D812EE32B61735CF9B753DF73`
- `ActorTypeNPC`: `Skyrim.esm:00013794`

The read-only scanner recursively walked `GRUP` records, decompressed records
with flag `0x00040000`, indexed `KYWD` editor IDs, and decoded every `RACE`
record's `KWDA` array. Thirty-two races carry `ActorTypeNPC`:

- All ten ordinary adult playable races, the four child races, `ElderRace`,
  `DA13AfflictedRace`, and `NordRaceAstrid` are living-humanoid candidates.
- Twelve vampire variants also carry `ActorTypeUndead` and are rejected.
- `DremoraRace` also carries `ActorTypeDaedra` and is rejected.
- `ManakinRace` (`0010760A`) and `InvisibleRace` (`00071E6A`) carry only
  `ActorTypeNPC` among the actor-type keywords. They are nonliving utility
  races and therefore have explicit FormID exclusions.

The positive gate intentionally uses CommonLib's `Actor::IsHumanoid()`, which
resolves Skyrim's default `ActorTypeNPC` object across the actor base and
current race. Negative gates use default-object keywords for animal, Daedra,
Dwarven/robot, and undead classifications, plus the default dragon test. Fixed
Skyrim FormIDs supply the supplemental `ActorTypeCreature` (`00013795`) and
`ActorTypeGhost` (`000D205E`) checks. Engine ghost and reanimated states provide
additional protection.

`IsBeastRace` is not an exclusion: vanilla Argonian and Khajiit races use it
and are living humanoids. Mod-added humanoids remain eligible when their race
or actor base follows the standard `ActorTypeNPC` convention. A missing race or
missing positive classification preserves vanilla behavior.

The parser framing is reproducible from `audit/esp.py` in the associated
`skyrim-mod-assistant` repository: skip the leading `TES4` record by declared
size, recursively traverse `GRUP` children, zlib-decompress flagged records,
and decode `EDID`/`KWDA` subrecords.

## API references

- [CommonLibSSE NG `TESObjectREFR` implementation](https://github.com/CharmedBaryon/CommonLibSSE-NG/blob/main/src/RE/T/TESObjectREFR.cpp)
- [CommonLibSSE NG actor-state declarations](https://github.com/CharmedBaryon/CommonLibSSE-NG/blob/main/include/RE/A/ActorState.h)
- [CommonLibSSE NG default-object declarations](https://github.com/CharmedBaryon/CommonLibSSE-NG/blob/main/include/RE/B/BGSDefaultObjectManager.h)

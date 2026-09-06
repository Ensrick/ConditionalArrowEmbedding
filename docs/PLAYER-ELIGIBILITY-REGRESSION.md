# Full-health player regression: 0.3.1 to 0.3.2

## Observed failure

The September 5, 2026 user test had a full-health Breton player in godmode
receiving arrows. Every observed arrow remained embedded. The installed 0.3.1
DLL SHA-256 was
`2DA8F3EF1F08B73FFB3E47DB3BF870289004F4973E1079585DC331208F320ACA`.
The actual log reported:

```text
18:47:18.554 first normal arrow reached the post-damage physical-hit hook
18:47:18.556 target=00000014 livingHumanoid=false region=body
             postHitHealthRatio=1.0000 killedByHit=false
             missileResult=4 impactResult=0 action=preserve-vanilla
```

This proves that the hook was reached and that eligibility rejected the player;
it is not evidence of an absent DLL, low player health, or a godmode exemption.
The 0.3.1 log did not record individual classification gates, so it cannot alone
prove which gate rejected this particular hit.

## Proven code defect and repair

`CollectTargetTypeTraits` called the inherited `Actor::IsReanimated()` directly.
Disassembling the exact 0.3.1 `Hooks.cpp.obj` showed this sequence:

```text
mov eax, dword ptr [rsi+0A8h] ; rsi is the Actor pointer
and eax, 1E00000h
cmp eax, 800000h
sete al
```

That is not the life-state word in Skyrim 1.7.104. CommonLib's runtime-aware
`Actor::AsActorState()` selects Actor+0xC0; the life-state word is eight bytes
into that subobject, at Actor+0xC8. The multi-runtime C++ inheritance layout
omits runtime-sized data and is not the engine layout. The old code interprets
unrelated memory as reanimation state, which can falsely exclude living actors.

0.3.2 uses `AsActorState()->IsReanimated()` through a shared tested helper. It
does not special-case the player, godmode, or any living/undead race. New bounded
player telemetry records the actual race and every eligibility gate so the
next game test can distinguish this repair from any additional runtime issue.

## Other eligibility and health calls inspected

| Call | Implementation and pointer safety |
|---|---|
| `GetRace()` | Reads the versioned `GetActorRuntimeData()` and falls back to the actor base's race; no direct `ActorState` cast. |
| `IsHumanoid()` / `IsDragon()` | `TESObjectREFR` helpers resolve default keywords, then call `HasKeyword`; `TESObjectREFR` is Actor's primary base at zero offset. |
| `HasKeywordWithType()` | Uses the default-object manager's version-aware object lookup, then primary-base keyword dispatch. |
| `HasKeyword()` | Calls the engine virtual `HasKeywordHelper` on Actor/TESObjectREFR, not a cast to the NPC keyword-form base. |
| `IsGhost()` | Calls relocated engine function AE Address Library ID 37275 with the original Actor pointer. |
| `IsDead()` | Engine virtual on Actor's primary vtable; not the inherited `ActorState` convenience helper. |
| `GetActorValue()` | Production code explicitly uses `AsActorValueOwner()`, which is runtime-aware. |
| `GetActorValueMax()` | CommonLib implementation uses `AsActorValueOwner()->GetPermanentActorValue()` plus relocated temporary-modifier lookup. |
| `IsPlayerRef()` | Primary form identity helper; no state-bearing secondary-base cast. |

No other directly inherited state-bearing secondary-base method is used by this
hook. This inspection addresses the pointer-layout defect; it does not assert
that all external runtime metadata can never be wrong.

## Verification boundaries

- A native test models 1.7.104 with `REL::Module::mock`, varies unrelated legacy
  word values over all 16 bit patterns and true life states over 0 through 8,
  then checks the production accessor and full-health player policy (144 cases).
- A source contract verifies the live hook is wired to that tested helper.
- The policy matrix covers player/NPC, head/body/unknown region, both sides of
  the unchanged strict-below-50% boundary, and lethal/nonlethal outcomes.
- Existing executable/control-flow and binary audits remain required.
- These are static/native host tests. Fresh in-game visual and damage tests are
  still required before declaring the runtime issue resolved. No game was
  launched for this investigation.

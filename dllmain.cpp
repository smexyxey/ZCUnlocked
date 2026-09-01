// ZCUnlocked - Star Wars: Zero Company (Steam build 24874058)
//
// Lets any humanoid character pick the shipped hero kits (Padawan / Mandalorian) and
// the Mandalorian armour in the creator - AND freely switch back off them again.
//
// Two problems this solves:
//   1. Hero classes / specs / weapons are gated to a specific hero's identity, so the
//      creator never offers them to an ordinary character. We authorise them by handing
//      the game's OWN requirement check the character's tags plus the authored identity
//      tags temporarily supplied (rig / species / body / slot / facts stay enforced).
//   2. Once you equip a hero class or its weapon you were LOCKED to it - because the
//      only thing that would un-set it (the default humanoid class) is never shown as a
//      selectable tile. So we also inject the DEFAULT parts (Class_Hero_Humanoid, plain
//      weapon specs) as tiles: pick the default class and the game strips the now-invalid
//      hero spec/weapon, putting the character back to normal.
//
// How a part is shown: the game builds the creator list in FilterAssetDataByTags; after
// it runs we append any part in kOfferParts the game's own check (with identity lent)
// accepts. How a part is equipped: DoesPartMeetRequirements, same identity-lend trick.
//
// Self-contained: talks to UE4SS through the handful of functions it already exports,
// with a tiny built-in hook. No SDK or extra libraries to build.
//
// EDIT HERE: kOfferParts (tiles to show) and kLendTags (identity tags we lend).

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <intrin.h>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <atomic>
#include <string>
#include <vector>
#include <memory>

#define STR(x) L##x

namespace
{
    // ---- minimal Unreal ABI (only what we touch) ----
    struct FName { std::uint32_t comparison_index; std::uint32_t number; };
    struct FPrimaryAssetId { FName type; FName name; };
    struct FGameplayTag { FName tag_name; };
    template <class T> struct TArray { T* data; std::int32_t num; std::int32_t max; };
    struct FGameplayTagContainer { TArray<FGameplayTag> tags; TArray<FGameplayTag> parents; };
    static_assert(sizeof(FName) == 8 && sizeof(FPrimaryAssetId) == 16 && sizeof(FGameplayTagContainer) == 32);

    enum { FindName = 0, AddName = 1 };

    // ---- parts we make selectable as tiles in the creator ----
    // Includes the DEFAULT humanoid class + plain weapon specs so you can always switch
    // BACK to a normal loadout, not just into a hero one. Gear kits are NOT tiles - they
    // come in with their weapon spec and are authorised by the requirement hook.
    constexpr const wchar_t* kOfferParts[] = {
        // class - Hero_Humanoid is the "back to normal" tile (Class.Default, rig-only)
        STR("CPD_Char_Class_Hero_Humanoid"),
        STR("CPD_Char_Class_Hero_Padawan"),
        STR("CPD_Char_Class_Hero_Mando"),
        // tactical specs (Padawan Jedi tree + generic Warrior)
        STR("CPD_TacticalSpec_Padawan"),
        STR("CPD_TacticalSpec_PadawanExtended"),
        STR("CPD_TacticalSpec_Warrior"),
        // talents
        STR("CPD_TalentSpec_TheLostPadawan"),
        STR("CPD_TalentSpec_TheMandalorian"),
        // weapon specs - lightsabers + Cly's blasters
        STR("CPD_WeaponSpec_Melee_2H_TelRea"),
        STR("CPD_WeaponSpec_Melee_1H_Anakin"),
        STR("CPD_WeaponSpec_Blaster_Pistol"),
        STR("CPD_WeaponSpec_Blaster_Rifle"),
        // 1.4: the Coil Striker's VIBROSWORD (1H). Enemy-class spec: base BP grants the proven
        // 1H lane (AS_Human_1HMelee + PxTable_Wep_1HMelee + CPD_GK_MeleeWeapon_CoilStriker) and
        // difficulty-scaled enemy damage (T1=12 vs saber 10). AuthoredOnly gate already in
        // kLendTags. Its owned tag is Weapon.ENEMY.1HMelee (not Weapon.Melee) so the saber
        // machinery correctly ignores it; the 003_ZCVibrosword_P pak fixes BP_Vibroblade's
        // authored SASArch_2HRifle quirk so the game's own equip flow holds/swings it 1H-melee.
        // v2.14.0's offer produced NO tile -> its Class.Battledroid.B1 requirement blocked
        // accepts(); v2.14.1 lends B1 scoped to enemy specs only (g_is_vibro / g_b1).
        STR("CPD_WeaponSpec_Enemy_Melee_Coil_Striker"),
        // Mandalorian wardrobe - colourable armour (needs Species.Human, so human-only)
        STR("CPD_H_Outfit_Man001A_TORS"), STR("CPD_H_Outfit_Man001A_LEGS"),
        STR("CPD_H_Outfit_Man001A_ARMS"), STR("CPD_H_Outfit_Man001A_BOOT"),
        STR("CPD_H_Outfit_Man001A_HELM"), STR("CPD_H_Outfit_Man002A_TORS"),
        STR("CPD_H_Outfit_Man002A_LEGS"), STR("CPD_H_Outfit_Man002A_ARMS"),
        STR("CPD_H_Outfit_Man002A_BOOT"), STR("CPD_H_Outfit_Man002A_HELM"),
        STR("CPD_H_Outfit_Cly_TORS"), STR("CPD_H_Outfit_Cly_LEGS"),
        STR("CPD_H_Outfit_Cly_ARMS"), STR("CPD_H_Outfit_Cly_BOOT"),
        STR("CPD_H_Outfit_Cly_HELM"), STR("CPD_H_Outfit_ClyB_HELM"),
        // Jedi / Padawan robes (1.3) - same all-species lend as the Mando set
        // Tel-Rea (Jedi Master) wardrobe
        STR("CPD_H_Outfit_TelRea_TORS"),   // full robe: occupies torso+arms+legs+boots+helmet
        STR("CPD_H_Outfit_TelReaA_TORS_MSA_TA"), STR("CPD_H_Outfit_TelReaA_ARMS"),
        STR("CPD_H_Outfit_TelReaA_LEGS"), STR("CPD_H_Outfit_TelReaA_BOOT"),
        STR("CPD_H_Outfit_TelReaA_HELM"),
        STR("CPD_H_Outfit_TelReaB_TORS_MSA_TA"), STR("CPD_H_Outfit_TelReaB_ARMS"),
        STR("CPD_H_Outfit_TelReaB_LEGS"), STR("CPD_H_Outfit_TelReaB_BOOT"),
        STR("CPD_H_Outfit_TelReaB_HELM"),
        // Anakin (Jedi) robes
        STR("CPD_H_Outfit_Anakin_TORS"), STR("CPD_H_Outfit_Anakin_ARMS"),
        STR("CPD_H_Outfit_Anakin_LEGS"), STR("CPD_H_Outfit_Anakin_BOOT"),
        // generic Jedi robe set
        STR("CPD_H_Outfit_Jed001A_TORS_MSA_TA"), STR("CPD_H_Outfit_Jed001A_LEGS_MSA_LB"),
        // Hawks (Lost Padawan) wardrobe
        STR("CPD_H_Outfit_HawksA_TORS_MSA_TA"), STR("CPD_H_Outfit_HawksA_TORS_MSA_TALB"),
        STR("CPD_H_Outfit_HawksA_LEGS_MSA_LB"), STR("CPD_H_Outfit_HawksA_BOOT"),
        STR("CPD_H_Outfit_HawksB_TORS_MSA_TAL"), STR("CPD_H_Outfit_HawksB_TORS_MSA_TALB"),
        STR("CPD_H_Outfit_HawksB_LEGS_MSA_LB"), STR("CPD_H_Outfit_HawksC_LEGS"),
        STR("CPD_H_Outfit_HawksD_TORS_L_MSA_TA"), STR("CPD_H_Outfit_HawksE_TORS"),
        STR("CPD_H_Outfit_HawksF_TORS_L"),
    };
    constexpr std::size_t kOfferCount = sizeof(kOfferParts) / sizeof(kOfferParts[0]);

    // ---- identity tags we lend to the game's own check (never rig/species/body) ----
    constexpr const wchar_t* kLendTags[] = {
        STR("br.Customization.Accepts.AuthoredOnly"),
        STR("br.Customization.Accepts.Outfit.Mdo"),
        STR("br.Customization.Part.Character.Info.Name.ClyKullervo"),
        STR("br.Customization.Part.Character.Info.Name.Tel-ReaVokoss"),
        STR("br.Customization.Part.Character.Info.Name.KabbUppercut"),
        STR("br.Customization.Part.Character.Info.Name.Hawks"),
        STR("br.Customization.Part.Character.Info.Name.JaeMordant"),
        STR("br.Customization.Part.Character.Info.Name.NeeshRenark"),
        STR("br.Customization.Part.Character.Info.Name.RunaBlask"),
        STR("br.Customization.Part.Character.Info.Name.Trick"),
        STR("br.Customization.Part.Character.Info.Name.M-EVO"),
        STR("br.Customization.Part.Character.Info.Name.Anakin"),   // gates CPD_WeaponSpec_Melee_1H_Anakin
    };
    constexpr std::size_t kLendMax = sizeof(kLendTags) / sizeof(kLendTags[0]);

    // ---- USER SETTING: let the Mandalorian armour be worn by ANY species, not just humans ----
    // The Man/Cly outfit parts are gated to human/humanoid recipients by the game's own
    // requirement check. When this is on we lend the human species + humanoid body/rig tags
    // ONLY to the outfit parts below (kOutfitLendTags), so any species passes the game's check
    // for exactly those parts and nothing else in the human wardrobe is un-gated. NOTE: this
    // makes the armour OFFERED and EQUIPPABLE on every species; correct APPEARANCE is only
    // guaranteed on humanoid-rigged characters (the meshes are skinned to the humanoid rig).
    constexpr bool kMandoAllSpecies = true;

    // Structural identity tags lent ONLY to the CPD_H_Outfit_* armour parts (see kMandoAllSpecies).
    constexpr const wchar_t* kOutfitLendTags[] = {
        STR("br.Customization.Part.Character.Species.Human"),
        STR("br.Customization.Part.Character.Rig.Humanoid"),
        STR("br.Customization.Part.Character.Appearance.Humanoid.Body.Type.Feminine"),
        STR("br.Customization.Part.Character.Appearance.Humanoid.Body.Type.Masculine"),
        STR("br.Customization.Part.Character.Appearance.Body.Height.Average"),
        // body-frame variants the Jedi robes are authored for (1.3; tag strings pak-verified -
        // Weight.* really does sit directly under Character, unlike the Height family)
        STR("br.Customization.Part.Character.Appearance.Body.Height.Short"),   // Tel-Rea robes
        STR("br.Customization.Part.Character.Appearance.Body.Height.Tall"),    // Anakin robes
        STR("br.Customization.Part.Character.Weight.Thin"),                    // Tel-Rea robes
        STR("br.Customization.Part.Character.Weight.Average"),                 // robes generally
    };
    constexpr std::size_t kOutfitLendMax = sizeof(kOutfitLendTags) / sizeof(kOutfitLendTags[0]);

    // Hero-identity tags that gameplay-tag LOCK queries test for. A spec/talent/weapon whose part
    // carries one of these is "hero-bound" -> the game locks it. We neutralise any Matches() query
    // that tests for one of these so hero kits stay changeable (spec used the parent; the weapon
    // path tests a specific name, so we cover both).
    constexpr const wchar_t* kNameTags[] = {
        STR("br.Customization.Part.Character.Info.Name"),
        STR("br.Customization.Part.Character.Info.Name.Tel-ReaVokoss"),
        STR("br.Customization.Part.Character.Info.Name.ClyKullervo"),
        STR("br.Customization.Part.Character.Info.Name.KabbUppercut"),
        STR("br.Customization.Part.Character.Info.Name.JaeMordant"),
        STR("br.Customization.Part.Character.Info.Name.LucoBronc"),
        STR("br.Customization.Part.Character.Info.Name.Trick"),
        STR("br.Customization.Part.Character.Info.Name.Hawks"),
        STR("br.Customization.Part.Character.Info.Name.NeeshRenark"),
        STR("br.Customization.Part.Character.Info.Name.RunaBlask"),
        STR("br.Customization.Part.Character.Info.Name.M-EVO"),
    };
    constexpr std::size_t kNameMax = sizeof(kNameTags) / sizeof(kNameTags[0]);

    // The weapon-change gate (WBP_Menu_Armory_WeaponLanding): CanChange/Customize/ModifyWeaponQuery =
    // ALL( ALL(BitReactor.Item.UIType) NONE(BitReactor.Item.UIType.Lightsaber) ) -> you can change a
    // weapon UNLESS it's a lightsaber. We force those queries to return TRUE (identified by the dict
    // holding BOTH the UIType parent AND UIType.Lightsaber, which separates them from combat
    // "is-lightsaber" checks that hold only the Lightsaber tag) so the lightsaber changes like any weapon.
    const wchar_t* const kUiTypeTag = STR("BitReactor.Item.UIType");
    const wchar_t* const kLightsaberTag = STR("BitReactor.Item.UIType.Lightsaber");

    // Force Meditations (exotic utilities like Fast Healing) require the character to own the
    // bespoke class tag br.CharacterClass.Bespoke.Jedi.TelRea, checked by
    // UBrunoUtilityInventoryItem::CanCharacterEquipItemType via a native HasAll on the item's
    // required tags at item+0x450. A recruit with the Padawan kit doesn't own that tag, so the
    // meditations are hidden. We hook that check and return TRUE only for a utility whose required
    // tags include that bespoke tag -> Tel-Rea's meditations unlock, nothing else is touched.
    constexpr std::uintptr_t kCanEquipRva = 0x726DF90;   // UBrunoUtilityInventoryItem::CanCharacterEquipItemType
    const std::uint8_t kCanEquipBytes[16] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x83};
    const wchar_t* const kBespokeTag = STR("br.CharacterClass.Bespoke.Jedi.TelRea");

    // The bespoke tag br.CharacterClass.Bespoke.Jedi.TelRea is granted only by the class OBJECT
    // Class_Padawan_TelRea; the recruit's customization class part never adds it to owned tags, so
    // a Padawan recruit doesn't have it and the meditations are never granted/available. The game
    // tests it with a native HasAll (FGameplayTagContainer::HasAll @0x58DFAB0, (required, character)).
    // We hook it: when a query for the bespoke tag runs against a character who owns Class.Exotic.Padawan
    // (i.e. is a Padawan via our kit), answer yes -> the recruit registers as a real Padawan for the
    // meditation grant AND its equip eligibility. Any non-bespoke query falls straight through.
    constexpr std::uintptr_t kHasAllRva = 0x58DFAB0;
    const std::uint8_t kHasAllBytes[16] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x63, 0x41, 0x08, 0x48, 0x89, 0x74, 0x24, 0x50, 0x48};
    const wchar_t* const kExoticTag = STR("br.Customization.Part.Character.Class.Exotic.Padawan");

    // 1.4: BLASTER DEFLECTION FOR CUSTOM JEDI (2026-08-30 recon). The deflect MECHANIC is fine for
    // any Padawan-kit carrier: FBrunoCustomizationCommandLeveledAbilities::Execute (0x719FAC0)
    // defaults a character with no focus allocations to level 1 (disasm: "mov ebx,1" before
    // CharacterHasFocusWithAbility, and GetFocusAllocations' not-found path also returns 1), so
    // GA_PadawanTraining_T1 (= GE_PadawanTraining_T1, +0.30 BitReactorCombatSet.DeflectionChance,
    // pak-verified float @0x324) is granted like the free T1 actives, and the accuracy calc
    // (BitReactorAccuracyCalculation) rolls that captured chance with NO identity check.
    // What never fires is the PRESENTATION: BP_BaseProjectile (HandleDeflection) and SM_UnitReaction
    // (StartDeflection) both gate on GetOwnedGameplayTags(target).HasAnyMatchingGameplayTags(
    // DeflectionCharacterNameTags), and BP_CharacterNameTagGroups hard-whitelists exactly three
    // character identity tags: Info.Name.Anakin / Info.Name.Tel-ReaVokoss / Info.Name.Trilla.
    // A custom Jedi's name tag is never in that set -> deflected shots present as plain misses.
    // Fix: lend a whitelist name as an OWNED tag (loose ASC count, set_owned_tag) to Weapon.Melee
    // wielders in the settled window, removed on the settled revert (sabers off).
    // v2.13.1 LANE MATCHING (Alex's v2.13.0 live test): the name tag doesn't just open the gate,
    // it also SELECTS the deflect anim family - with Info.Name.Anakin a DUAL-saber wielder played
    // Anakin's single-saber deflect (the unprefixed AM_1HMelee_Deflect_* set; Trilla has her own
    // AM_Trilla_* set in the same folder, Tel his AM_TelRea_* set). So the lend now matches the
    // weapon lane: Melee.2H (Tel's dual sabers) -> Info.Name.Tel-ReaVokoss, any other melee ->
    // Info.Name.Anakin, and the settled pass removes the mismatched one first (self-heals lane
    // swaps and v2.13.0 leftovers). Trilla stays unused (BP_ProjectileHomingTarget has a
    // Trilla-specific "DeflectorIsTrilla" homing branch). Real Tel/Anakin never enter the pending
    // registry (their pawns are melee-native, never PWT-flipped), so their own tags are never touched.
    // Open item from the same test: one deflect resolved mechanically (shooter took the reflect
    // damage) but the SHOOTER's firing presentation never played - watch whether lane matching
    // fixes that too (suspected presentation bind failure on the name/stance mismatch).
    constexpr bool kEnableDeflectLend = true;
    const wchar_t* const kDeflectNameTag    = STR("br.Customization.Part.Character.Info.Name.Anakin");
    const wchar_t* const kDeflectNameTagTel = STR("br.Customization.Part.Character.Info.Name.Tel-ReaVokoss");

    // 1.4 v2.18.0: FORCE PUSH ASSIST FOR CUSTOM JEDI (2026-08-30 recon). The teamwork/backup
    // assist ability is granted per CHARACTER CLASS, not per kit or weapon: every hero class CPD
    // (CPD_Char_Class_Hero_Humanoid/Mando/Rex/...) carries AbilitySet_TeamworkPackage_Standard
    // = GA_Backup_TeamworkAssistShot (a ranged shot, presentation SM_TypicalShot_ReactionFire),
    // while CPD_Char_Class_Hero_Padawan carries AbilitySet_TeamworkPackage_Padawan =
    // GA_Backup_TeamworkAssistShot_Push_TelRea and Anakin's class the _Anakin push variant
    // (both present via SM_ForcePush_Assist; raw-byte pak scan - the sets are invisible to
    // name-map scans because each stores its single GA as a soft class path string). A custom
    // Jedi keeps its recruit class, so its assist stays the blaster shot - fired with a saber
    // equipped there is no blaster to present = Alex's "invisible laser pistol".
    // Fix (settled window, mission-only, saber wielders only = Weapon.Melee minus the
    // Enemy.1HMelee vibro lane, so vibro clones keep their shot; v2.18.1 - the original
    // g_exotic gate never matched Hawks-style customs): remove the standard shot from the ASC
    // (UBitReactorAbilityScriptingFunctions::RemoveAbilityByClass) and grant the lane-matched
    // push package (UBitReactorGameStatics::GrantAbilitySet, handles stored for the revert via
    // RemoveAbilitySetWithHandles + standard re-grant). Param layouts reflection-proven
    // (PropPointers @0xbd313f0/0xbd2ebf0/0xbd17df0/0xbd15700): Grant={GiveToASC@0,Set@8,
    // Handles@0x10 (3 TArrays=0x30)}, RemoveSet={ASC@0,Handles@8}, Find/RemoveByClass=
    // {ASC@0,Class@8(,int32 ret@0x10)}; exec disasm 0x6c8f790 shows the set's runtime ability
    // entries are HARD UClass* (IsChildOf-checked), so loading the SET also loads the GA.
    // Presentation rides infrastructure we already lend: DT_AG_ForcePush selects the push
    // choreography by Info.Name.Tel-ReaVokoss / Info.Name.Anakin - the deflect-lend name tags,
    // same lanes; SAS_TelRea_Force (aim stepouts) is Tel-pawn-only but degrades gracefully.
    constexpr bool kEnableAssistPush = true;
    const wchar_t* const kAssistStdGaPath   = STR("/Game/Game/GameData/Abilities/GA_Backup_TeamworkAssistShot.GA_Backup_TeamworkAssistShot_C");
    // v2.18.3: the OTHER assist shot - granted by every RANGED WEAPON SPEC CPD (Pistol/Rifle/
    // Repeater/Longarm/DC-17M), and the recruit keeps a holdout-PISTOL spec alongside the
    // saber (1HPistol stance set present on saber wielders) -> its assist shot is the actual
    // "invisible laser pistol" that outruns the push. Strip it too (exact-class removal;
    // it re-grants naturally with the weapon specs at the next hub equip/spawn, and weapons
    // cannot change mid-mission, so no mission-side restore is needed).
    const wchar_t* const kAssistCommonGaPath = STR("/Game/Game/GameData/Abilities/Common/GA_TeamworkAssistShot.GA_TeamworkAssistShot_C");
    // v2.18.3: the THIRD shooter - GA_BondAssistShot, granted to EVERY character via
    // AbilitySet_BasicCharacterDefaults/_Striker (raw-byte scan), trigger
    // BitReactor.AbilityTrigger.BondAssist (the Call-for-Backup bond flow's "Bonus Assist"),
    // AbilityID Base.Shoot, guaranteed-hit SM_TypicalShot presentation - a plain ranged shot
    // that fires weaponless with a saber. Stripped from custom Jedi too: their bond-bonus
    // assist becomes a no-op instead of an invisible pistol (real Jedi pawns never enter the
    // registry, so Tel/Anakin keep theirs).
    const wchar_t* const kAssistBondGaPath = STR("/Game/Game/GameData/Abilities/GA_BondAssistShot.GA_BondAssistShot_C");
    // v2.18.4: live ability-list diagnostics + combat-join re-strip. The 2.18.3 diag proved
    // NONE of the three shot GAs is granted at settle time (std=0 common=0 bond=0) yet a
    // shot still fires on assist -> something grants the shooter LATER (combat join is the
    // prime suspect: the basic sets are full of GA_OnJoinedCombat/TacticalEncounter*
    // machinery, and GetBasicShotAbility resolves the ally's shot BY TAG at call time -
    // FindAllAbilitiesWithTags on a native tag, exec disasm 0x6c594f0). So: every 128 ticks
    // in missions, for each tracked char, (a) if the granted-ability set changed, log every
    // spec's ability class name (FName::ToString by RVA), (b) re-strip any known shot that
    // reappeared. The engine-allocated name buffers leak per dump line - bounded, accepted.
    constexpr bool kAssistDiagStrip = true;           // v2.18.6: 2.18.4 behavior (strip re-granted shots)
    // v2.18.6: grant the ANAKIN push package to BOTH lanes. Alex's live matrix (08-30
    // ~23:2x, v2.18.4): Anakin-lane assist = Force Push WORKS on a custom; TelRea-lane =
    // no-op. The TelRea variant's K2_ShouldAbilityRespondToEvent carries one extra gate
    // the Anakin variant lacks (CallFunc_ActorHasMatchingGameplayTag - name-map-proven) -
    // custom dual wielders evidently fail it. Presentation is unaffected by the package
    // choice: DT_AG_ForcePush picks the choreography by the char's NAME TAG (dual wielders
    // carry the lent Info.Name.Tel-ReaVokoss -> Tel's push sequence, leader swap handles
    // the rig). Trade-off: the TelRea variant's meditation-buff damage bonus is skipped.
    constexpr bool kAssistAnakinBothLanes = true;
    constexpr std::uintptr_t kFNameToStringRva = 0x1852AD0;   // FName::ToString(FString&) - (this, out)
    const std::uint8_t kFNameToStringBytes[16] = {0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xEC,0x30,0x48};
    constexpr std::size_t kAscAbilitiesOff = 0x530;   // ASC ActivatableAbilities.Items data (num @+0x538, stride 0xF8) - exec disasm 0x6c62a70
    constexpr std::size_t kSpecStride = 0xF8;
    constexpr std::size_t kSpecAbilityOff = 0x10;     // FGameplayAbilitySpec::Ability (UGameplayAbility*)
    const wchar_t* const kAssistPushGaTel   = STR("/Game/Game/GameData/Abilities/GA_Backup_TeamworkAssistShot_Push_TelRea.GA_Backup_TeamworkAssistShot_Push_TelRea_C");
    const wchar_t* const kAssistPushGaAna   = STR("/Game/Game/GameData/Abilities/GA_Backup_TeamworkAssistShot_Push_Anakin.GA_Backup_TeamworkAssistShot_Push_Anakin_C");
    const wchar_t* const kAssistSetStdPath  = STR("/Game/Game/GameData/CharacterClasses/AbilitySet_TeamworkPackage_Standard.AbilitySet_TeamworkPackage_Standard");
    const wchar_t* const kAssistSetTelPath  = STR("/Game/Game/GameData/CharacterClasses/AbilitySet_TeamworkPackage_Padawan.AbilitySet_TeamworkPackage_Padawan");
    const wchar_t* const kAssistSetAnaPath  = STR("/Game/Game/GameData/CharacterClasses/AbilitySet_TeamworkPackage_Anakin.AbilitySet_TeamworkPackage_Anakin");
    const wchar_t* const kGameStaticsCdo    = STR("/Script/BitReactorGame.Default__BitReactorGameStatics");
    const wchar_t* const kAbilityFnsCdo     = STR("/Script/BitReactorGame.Default__BitReactorAbilityScriptingFunctions");
    constexpr std::uintptr_t kZConstructAbilitySetRva = 0x6A625E0;   // Z_Construct_UClass_UBitReactorAbilitySet
    const std::uint8_t kZConstructAbilitySetBytes[16] = {0x48,0x83,0xEC,0x28,0x48,0x8B,0x05,0x1D,0xF0,0xA0,0x06,0x48,0x85,0xC0,0x75,0x1A};

    // v2.18.2: PHASE-BASED hub/mission discriminator. The old hub_now() probe ("HQ inventory
    // actor findable") answers HUB during missions too — the actor/world persists across the
    // hub→mission transition — so from v2.14.1 on, EVERY mission-only branch (leader swap,
    // deflect lend, assist push) was silently withheld in missions: the log has zero
    // deflect-granted/leader-swap lines in any session after the v2.14.1 ready line (they
    // appear in every mission session up through v2.13.1/2.14.0). The game's own authority:
    // UBitReactorGameGamePhaseStatics::IsTacticalPhase / IsHeadquartersPhase — GameInstance
    // phase subsystem's current tag matched vs SubPhase.Tactical / SubPhase.Headquarters
    // (disasm: GetWorldFromContextObject → GameInstance @world+0x228 → subsystem tag @+0x68).
    using PhaseFn = bool(__fastcall*)(void* world_ctx);
    constexpr std::uintptr_t kIsTacticalPhaseRva = 0x6EC5DE0;   // UBitReactorGameGamePhaseStatics::IsTacticalPhase
    constexpr std::uintptr_t kIsHqPhaseRva       = 0x6EC5D60;   // UBitReactorGameGamePhaseStatics::IsHeadquartersPhase
    const std::uint8_t kIsTacticalPhaseBytes[16] = {0x48,0x83,0xEC,0x28,0x41,0xB8,0x01,0x00,0x00,0x00,0x48,0x8B,0xD1,0xE8,0x7E,0x64};
    const std::uint8_t kIsHqPhaseBytes[16]       = {0x48,0x83,0xEC,0x28,0x41,0xB8,0x01,0x00,0x00,0x00,0x48,0x8B,0xD1,0xE8,0xFE,0x64};

    // ---- 1.4: display names for IMPORTED enemy parts (v2.15.0) ----
    // Enemy-spec armory tiles (the vibrosword etc.) render with no name: their
    // BitReactorUIDataFragment FTexts were never authored for player UI. Fix at runtime:
    // in settled hub windows, find the loaded CPD (StaticFindObject; it loads once the
    // armory enumerates it), walk its Fragments array (UBitReactorAssetDefinitionBase),
    // find the UIData fragment by class, and write DisplayName/MarkedUpDisplayName/
    // ShortDescription/LongDescription. FTexts are built with the game's own
    // UKismetTextLibrary::Conv_StringToText via ProcessEvent on the library CDO, then the
    // 0x18-byte FText is move-copied into the property (the stale empty FText's payload
    // leaks once per write - accepted). Add one kImportNames row per future enemy import.
    constexpr bool kEnableImportNames = true;
    // desc == nullptr -> name-only rename (keep the part's own localized descriptions);
    // icon != nullptr -> also point the UIData fragment's Small/LargeImage at that image-bank
    // tag (FBitReactorImageReference: byte Mode @+0 [0 = bank tag], FGameplayTag @+4 -
    // disasm-verified via ResolveImage 0x642A8E0).
    struct FImportName { const wchar_t* path; const wchar_t* name; const wchar_t* desc; const wchar_t* icon; };
    constexpr FImportName kImportNames[] = {
        { STR("/Game/Game/Customizations/Characters/Common/Specialization/Weapon/Enemy/CPD_WeaponSpec_Enemy_Melee_Coil_Striker.CPD_WeaponSpec_Enemy_Melee_Coil_Striker"),
          STR("Vibrosword"),
          STR("A Coil raider's vibro-blade. One-handed melee; enemy-grade damage that scales with the mission's threat level."),
          STR("ImageBank.Icon.VibroswordStrike") },
        // v2.16.1 renames (Alex): the sabers keep their own localized descriptions/icons.
        { STR("/Game/Game/Customizations/Characters/Common/Specialization/Weapon/CPD_WeaponSpec_Melee_1H_Anakin.CPD_WeaponSpec_Melee_1H_Anakin"),
          STR("Lightsaber"), nullptr, nullptr },
        { STR("/Game/Game/Customizations/Characters/Common/Specialization/Weapon/CPD_WeaponSpec_Melee_2H_TelRea.CPD_WeaponSpec_Melee_2H_TelRea"),
          STR("Dual Lightsabers"), nullptr, nullptr },
        // v2.16.4: the change-weapon LIST rows appear to render the GEAR-KIT part's name
        // (every saber GK fragment is natively "Lightsaber", which is why both saber rows
        // matched; the vibro row fell back to the spec name because its enemy GK has none).
        // Rename Tel's GK part so the dual row reads distinctly; short form per Alex.
        { STR("/Game/Game/Customizations/Characters/Common/CharacterClass/GearKit/CPD_GK_Lightsaber_TelRea.CPD_GK_Lightsaber_TelRea"),
          STR("Dual Sabers"), nullptr, nullptr },
        { STR("/Game/Game/Customizations/Characters/Common/CharacterClass/GearKit/CPD_GK_MeleeWeapon_CoilStriker.CPD_GK_MeleeWeapon_CoilStriker"),
          STR("Vibrosword"), nullptr, nullptr },
    };
    constexpr std::size_t kImportNameCount = sizeof(kImportNames) / sizeof(kImportNames[0]);

    // v2.16.1: the vibrosword's missing HOLOGRAM PREVIEW + category icons. The saber gear
    // kits' items carry BitReactor.Item.UIType.Lightsaber (+ Item.Lightsaber); the striker
    // kit's item has neither - the UIType is what the armory keys the 3D weapon hologram and
    // category iconography on (UGearItem::GameplayTags container; UGearItem::HologramScale
    // shows the preview is a per-gear-item feature). Lend UIType.Lightsaber to the striker
    // kit's items at runtime (the game's own AddTag, idempotent, parents auto-filled).
    constexpr bool kEnableVibroUiType = true;
    const wchar_t* const kVibroGkPath = STR("/Game/Game/GameData/GearKits/GK_MeleeWeap_Enemy_Coil_Striker.GK_MeleeWeap_Enemy_Coil_Striker");

    // v2.17.0: WRITE-AT-READ-TIME for the import names/icons. Ground truth from the pak data:
    // both saber specs natively carry inline "Lightsaber" FTexts and the armory's part
    // viewmodels come from a LIFETIME-CACHED provider whose InitializeViewModel runs exactly
    // once, at the first menu open - which is also when the CPDs first load, so any
    // find-then-write pass ALWAYS loses the race and the cache makes it permanent (why none
    // of the v2.15-2.16 text writes ever rendered; the VIBROSWORD row was a native tag-data
    // fallback, not our text). Fix: hook the exact consumption point -
    // UBitReactorAssetDefinitionBase::FindFragmentByClass<UBitReactorUIDataFragment>
    // (0x63DE750, called by AssetDefinitionViewModel::InitializeViewModel right before it
    // reads the texts) and install our precomputed FTexts on the returned fragment when its
    // OWNER is one of the kImportNames CPDs. FTexts are precomputed ONCE in the settled pass
    // (Conv_StringToText via ProcessEvent, stored {ITextData*, flags}); each hook install
    // AddRefs the shared text data (ITextData vtbl+8, per the 0x6317540 getter disasm) so
    // every holder owns a real reference. Icon writes ride the same hook (POD).
    constexpr std::uintptr_t kFindFragRva = 0x63DE750;
    const std::uint8_t kFindFragBytes[16] = {0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x0F,0xB6,0xDA,0x48,0x8B,0xF9};

    // ---- 1.4 v2.16.0: PAK-FREE RUNTIME TWEAKS (retire 002_ZCSaberBalance_P / 003_ZCVibrosword_P) ----
    // Both pak edits are plain data writes the DLL can make on the LOADED objects instead:
    //  A. Anakin saber balance: copy every UBitReactorMeleeWeaponSet attribute (FGameplayAttributeData
    //     floats @+8 Base / @+0xC Current - disasm-verified via SetBaseValue/SetCurrentValue) from
    //     Attributes_Lightsaber_Standard (Tel, Damage 10) onto Attributes_Lightsaber_Anakin (28).
    //     Values are SNAPSHOTTED into save state at EQUIP time (proven: the 002 pak served but the
    //     old 36 damage persisted until Alex re-equipped), and equips happen in the hub where the
    //     periodic pass has long since landed - so the runtime write is fully pak-equivalent.
    //  B. Vibrosword stance: write BP_Vibroblade's CDO property StanceAnimationArchetypeAsset
    //     (FSoftObjectPtr) from SASArch_2HRifle -> SASArch_1HMelee. The FName pair is LOCATED BY
    //     SCAN inside the property bytes (TPersistentObjectPtr layout puts a cached FWeakObjectPtr
    //     before the path; scanning for the rifle pair sidesteps the layout guess), and the 8 bytes
    //     before the pair are reset to an invalid weak ptr. Mission loads can read the CDO before
    //     our first pass (the CDO only loads with the spawn itself), so hook_cache_stance also
    //     queues Weapon.Enemy.1HMelee wielders and the settled window re-runs the game's own
    //     CacheStanceAnimationSetFromWeapon(SASArch_1HMelee, bAdd=true) on them (the v1.8.1-proven
    //     ProcessEvent recipe) - self-healing the one cold-boot-into-mission edge.
    // Coexistence with the paks is harmless (already-patched data means both passes no-op), so
    // keep the paks installed until this is confirmed live, then delete the two triplets.
    constexpr bool kEnableRuntimeBalance = true;
    constexpr bool kEnableVibroArchFix  = true;
    const wchar_t* const kAttrStandardPath = STR("/Game/Game/GameData/WeaponInfo/WeaponAttributes/Heroes/Attributes_Lightsaber_Standard.Attributes_Lightsaber_Standard");
    const wchar_t* const kAttrAnakinPath   = STR("/Game/Game/GameData/WeaponInfo/WeaponAttributes/Heroes/Attributes_Lightsaber_Anakin.Attributes_Lightsaber_Anakin");
    const wchar_t* const kVibroCdoPath     = STR("/Game/Game/GameData/WeaponInfo/Gear/BP_Vibroblade.Default__BP_Vibroblade_C");
    const wchar_t* const kRifleArchPkg     = STR("/Game/Game/GameData/AnimationSets/SASArch_2HRifle");
    const wchar_t* const kRifleArchName    = STR("SASArch_2HRifle");
    const wchar_t* const kMeleeArchPkg     = STR("/Game/Game/GameData/AnimationSets/SASArch_1HMelee");
    const wchar_t* const kMeleeArchName    = STR("SASArch_1HMelee");
    const wchar_t* const kMeleeArchAsset   = STR("/Game/Game/GameData/AnimationSets/SASArch_1HMelee.SASArch_1HMelee");
    const wchar_t* const kMeleeAttrProps[] = {
        STR("Damage"), STR("CriticalHitChance"), STR("CriticalHitDamage"),
        STR("LOSTraceRadius"), STR("MaxSkillBonus"), STR("Range") };
    constexpr std::size_t kMeleeAttrPropCount = sizeof(kMeleeAttrProps) / sizeof(kMeleeAttrProps[0]);

    // ================= EXPERIMENTAL: Fast Healing meditation grant (NOT in shipped 1.1.1) =================
    // The meditation is a story-granted inventory gearkit. To give it to a recruit we must call the game's
    // own grant path. Increment 1 (this build) only PROBES resolution - it calls FindInventoryItem for a set
    // of candidate names and logs which one returns a non-null item. No inventory is mutated. We trigger the
    // probe once, from inside the armory's gearkit enumeration (a reliable in-armory call site).
    constexpr std::uintptr_t kFindItemRva  = 0x71827E0;   // UBrunoCheatManager::FindInventoryItem(FString) [static] -> UBrunoInventoryItem*
    constexpr std::uintptr_t kGetGearKitsRva = 0x6ECE140; // UGearKitComponent::GetGearKitsWithTags (armory trigger)
    const std::uint8_t kGetGearKitsBytes[16] = {0x48, 0x83, 0xEC, 0x28, 0x48, 0x63, 0x81, 0x58, 0x01, 0x00, 0x00, 0x48, 0x89, 0x5C, 0x24, 0x30};
    // Run the game's OWN console dispatch from the mod's per-frame update (a safe between-frames moment),
    // instead of calling item functions raw mid-UI. First test: a harmless Slomo cheat to prove cheats are live.
    constexpr std::uintptr_t kExecConsoleRva = 0x49E3E10; // UKismetSystemLibrary::ExecuteConsoleCommand(WorldContext, FString, PlayerController)

    // ---- game build 24874058: PE identity + the five functions we rely on ----
    constexpr std::uint32_t kPeTimestamp = 0xE10ABE56;
    constexpr std::uint32_t kImageSize = 0x0E354000;
    constexpr std::uintptr_t kDoesPartRva = 0x63C6400;   // DoesPartMeetRequirements
    constexpr std::uintptr_t kFilterRva = 0x63D1C20;      // FilterAssetDataByTags
    constexpr std::uintptr_t kTagCopyRva = 0x40F7B20;     // FGameplayTagContainer copy ctor
    constexpr std::uintptr_t kTagDtorRva = 0x16C2E60;     // FGameplayTagContainer dtor
    constexpr std::uintptr_t kTagAddRva = 0x41017A0;      // FGameplayTagContainer::AddTag

    const std::uint8_t kDoesPartBytes[16] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x48, 0x89, 0x7C, 0x24, 0x18, 0x4C};
    const std::uint8_t kFilterBytes[16] = {0x48, 0x89, 0x5C, 0x24, 0x20, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57};
    const std::uint8_t kTagCopyBytes[16] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x33, 0xC0, 0x48, 0x8B, 0xD9, 0x48, 0x89, 0x01, 0x48, 0x89};
    const std::uint8_t kTagDtorBytes[16] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x8B, 0x49, 0x10, 0x48, 0x85, 0xC9};
    const std::uint8_t kTagAddBytes[16] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x02, 0x48, 0x8B, 0xF9};

    // ---- the slot-lock system that produces the in-game "Change Specialization" padlock ----
    // Equipping an authored/hero part calls AddExternallyLockedSlot to lock its slot; the UI
    // then shows a padlock and refuses to change it. We neutralise both the writer (so no slot
    // ever gets externally locked -> the read side always sees "unlocked") and the fragment
    // GetSlotIsLocked getter (belt and suspenders). Patched to return immediately.
    constexpr std::uintptr_t kAddLockRva = 0x63C3E70;       // UCustomizationInstance::AddExternallyLockedSlot (void)
    constexpr std::uintptr_t kGetSlotLockedRva = 0x63D3130; // UCustomizationFragmentInstanceSlot::GetSlotIsLocked (bool)
    const std::uint8_t kAddLockBytes[16] = {0x48, 0x89, 0x5C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x4C};
    const std::uint8_t kGetSlotLockedBytes[16] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0xE8, 0x62, 0xAB, 0xFE, 0xFF, 0x80, 0xBB};

    // The actual padlock the UI shows: UBitReactorCustomizationSlotViewModel::OnCustomizationRefreshed
    // copies slotInstance[+0xa2] straight into the view-model's locked field (bypassing GetSlotIsLocked).
    // Neutralise the read (movzx eax,[rsi+0xa2] -> xor eax,eax + nops) so the VM always sees "unlocked".
    constexpr std::uintptr_t kUiLockReadRva = 0x6FBAAA8;
    const std::uint8_t kUiLockReadBytes[16] = {0x0F, 0xB6, 0x86, 0xA2, 0x00, 0x00, 0x00, 0x4C, 0x8B, 0xF7, 0xF2, 0x0F, 0x10, 0x05, 0x66, 0x49};

    // THE real "Change Specialization" padlock (found by live PDB-symbol tracing of the
    // crash-confirmed path ResetToDefault -> RefreshCustomization -> BroadcastRefreshed ->
    // SlotViewModel::OnCustomizationRefreshed). That function looks the slot up in the
    // customization instance's ExternallyLockedSlots map; FOUND -> it sets SlotLockingThisSlotVM
    // (the padlock + "cannot be changed for this operator"), NOT-FOUND -> it clears it to null.
    // Patch the lookup's result test (cmp eax,-1) to (xor eax,eax) so it is ALWAYS "not found"
    // -> SlotLockingThisSlotVM stays null -> every slot is changeable. Also clears a stale/
    // serialized lock on reload (unlike no-oping only the writer).
    constexpr std::uintptr_t kSlotLockLookupRva = 0x6FBAD28;
    const std::uint8_t kSlotLockLookupBytes[16] = {0x83, 0xF8, 0xFF, 0x0F, 0x84, 0xCD, 0x01, 0x00, 0x00, 0x4C, 0x8D, 0x83, 0xB0, 0x00, 0x00, 0x00};

    // THE ACTUAL "cannot be changed for this operator" gate (found by decompiling the widget
    // BP_SpecializationSelectionVM): ShouldSpecializaitonBeLocked runs a gameplay-tag query
    // SpecLockedQuery/TalentLockedQuery = ANY( br.Customization.Part.Character.Info.Name ) via
    // FGameplayTagQuery::Matches. A spec/talent whose part carries any hero Info.Name tag matches
    // -> locked. We hook Matches and return false whenever the query's TagDictionary contains the
    // Info.Name parent tag -> that one query never matches -> nothing is ever locked (heroes too).
    constexpr std::uintptr_t kMatchesRva = 0x41042F0;   // FGameplayTagQuery::Matches(const FGameplayTagContainer&)
    const std::uint8_t kMatchesBytes[16] = {0x48, 0x83, 0xEC, 0x48, 0x44, 0x8B, 0x49, 0x20, 0x4C, 0x8B, 0xD2, 0x4C, 0x8B, 0xC1, 0x45, 0x85};
    // Matches is generic; MANY systems call it. We must override it ONLY for the spec/talent/weapon
    // lock UIs, which are Blueprint nodes routed through UBlueprintGameplayTagLibrary::DoesContainerMatchTagQuery.
    // That thunk calls Matches from exactly one site, whose return address is fixed. The native roster
    // filter (BrunoRosterViewModel::FilterCharacterList) also calls Matches - with an ANY(Info.Name) query
    // to list active operators - and if we override THAT, every operator is rejected and missions report
    // "not enough operators". So we gate the override on this exact return address only.
    constexpr std::uintptr_t kBpQueryRetRva = 0x40F887F;   // ret addr of Matches call inside execDoesContainerMatchTagQuery

    // ================= HELMET VOICE MODULATOR ==========================================================
    // Ported from Sternab's ZeroCompanyMandoWardrobe v0.4.x (MIT, (c) 2026 Sternab). When a Mandalorian /
    // Cly helmet is equipped and the game's OWN voiceover-preset solver returns "no effect" (-1), we inject
    // the stock helmet-radio filter so the character speaks with the modulated helmet voice. Fallback-only:
    // it never overrides an existing effect. The preset VALUE is not hard-coded - it is read at runtime from
    // the shipped Clone helmet CPD_H_Outfit_Clo008_HELM_TintB's CustomizationFragmentHelmetVO fragment via
    // UE4SS reflection (GetValuePtrByPropertyNameInChain), reusing the game's genuine authored value.
    constexpr bool kEnableHelmetVoice = true;
    constexpr std::uintptr_t kSolveVoiceRva = 0x639E1E0;       // BitReactor voiceover-preset solver (hooked)
    constexpr std::uintptr_t kGetSlotInstanceRva = 0x63C3860;  // (customization, slotTag) -> slot instance
    constexpr std::uintptr_t kGetPartDefFromIdRva = 0x63C5660; // (FPrimaryAssetId) -> CustomizationPartDefinition

    // ================= SABER STANCE FIX (weapon-driven anim identity) ==================================
    // Root cause (Ghidra + live tag dump): Hawks (and any bespoke rifle pawn) owns the tag
    // Animation.PrimaryWeaponType.Rifle. ABitReactorGameCharacter::CacheStanceAnimationSetFromWeapon /
    // GetStanceAnimationSetFromArchetype pick the stance archetype (SASArch_2HRifle vs SASArch_1HMelee)
    // and the upper-body anim layer by matching that owned tag, so a lightsaber still animates like a
    // rifle - the melee weapon spec only adds Weapon.Melee tags, it never changes PrimaryWeaponType.
    // Fix: hook the stance-setup exec (it receives the character as Context), and for a melee/saber
    // wielder still flagged Rifle, flip the owned tag Rifle->Other with the game's own
    // FGameplayTagCountContainer::UpdateTagMap_Internal so the saber archetype/layer is chosen.
    constexpr bool kEnableSaberStance = true;
    // v2.22.0 OPTION B (root fix for the den/SignificanceManager cold-load crash): defer the
    // PrimaryWeaponType flip out of the LOAD window. Disasm-confirmed root cause: swap_pwt's
    // update_tag (FGameplayTagCountContainer::UpdateTagMap_Internal 0x58DE790) fires the edge-gated
    // FOnGameplayEffectTagCountChanged broadcast; when that lands mid-actor-spawn with meditation GE
    // listeners equipped, a listener corrupts world state -> GetActorOfClass AV OR
    // USignificanceManager::Update AV. (The explicit vtable+0x968 notify is a ret-0 no-op in
    // shipping, so gating the notify is useless - the flip itself must not run during load.) When on,
    // hook_cache_stance skips flip_pwt while grant_world()==nullptr (world not begun-play = loading)
    // and enqueues the char so the settled relink loop re-flips + re-links from tag ownership once
    // kQuietTicks of stance silence proves the world settled. Set false to restore the pre-B behavior
    // (immediate flip during load) without a rebuild.
    constexpr bool kEnableOptionB = true;
    constexpr std::uintptr_t kCacheStanceRva  = 0x6C66820;  // ABitReactorGameCharacter::execCacheStanceAnimationSetFromWeapon(Context, FFrame&, Result)
    constexpr std::uintptr_t kUpdateTagMapRva = 0x58DE790;  // FGameplayTagCountContainer::UpdateTagMap_Internal(container, &tag, count) -> bool
    constexpr std::size_t kCharAscOff     = 0x7E8;  // char+0x7E8: embedded ASC provider (vtable+0x10 -> ASC)
    constexpr std::size_t kCharTagIfOff   = 0x638;  // char+0x638: owned-tag interface (vtable+0x18 -> HasTag(FGameplayTag byval))
    constexpr std::size_t kAscTagCountOff = 0xF50;  // ASC+0xF50: FGameplayTagCountContainer
    const std::uint8_t kCacheStanceBytes[16] = {0x48,0x89,0x5C,0x24,0x18,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57};

    using CacheStanceFn  = void(__fastcall*)(void*, void*, void*);
    using UpdateTagMapFn = bool(__fastcall*)(void* container, const FGameplayTag* tag, int count);
    constexpr std::size_t kAscBroadcastVt = 0x968;  // ASC vtable byte offset: tag-change notify (asc, &tag, present) - the game fires this after every UpdateTagMap
    using HasTagIfaceFn  = bool(__fastcall*)(void* iface, FName tag);   // char+0x638 vtable+0x18 (tag passed by value)
    using GetAscFn       = void*(__fastcall*)(void* provider);          // char+0x7E8 vtable+0x10
    using TagNotifyFn    = void(__fastcall*)(void* asc, const FGameplayTag* tag, int present);  // ASC vtable+0x968
    // Deferred anim RE-LINK: the upper-body layer is linked once at equip and cached; after we flip
    // the tag we must re-init the anim (SetAnimInstanceClass nil->class via ProcessEvent) so it re-links
    // the saber layer. The game re-grants Rifle many times while a mission loads, so the re-link is
    // QUIET-GATED: every stance-setup call for the char resets its timer, and the re-link fires only
    // after the stance system has been silent for kQuietTicks - the same "after load settled" moment
    // where the manual probe re-link is proven to stick. Self-healing: a later Rifle re-grant re-arms
    // the cycle via the stance hook, so it always converges on the proven end state.
    constexpr std::size_t kPendingMax = 8;
    constexpr unsigned kQuietTicks = 300;         // ~5s @60fps of stance silence for the char = settled
    constexpr unsigned kMinRelinkGapTicks = 300;  // floor between re-links of one char (anti flash-spam)
    constexpr unsigned kMaxRelinkFails = 8;       // drop a char after this many failed re-link tries
    using ProcessEventFn = void(__fastcall*)(void* obj, void* func, void* params);   // UObject::ProcessEvent
    using GetFuncFn      = void*(__fastcall*)(void* obj, const wchar_t* name);        // UObject::GetFunctionByNameInChain
    constexpr std::size_t kSlotPrimaryAssetIdOffset = 0xF0;    // FPrimaryAssetId inside a slot instance
    const std::uint8_t kSolveVoiceBytes[16] = {0x4C,0x8B,0xDC,0x55,0x41,0x54,0x48,0x81,0xEC,0x98,0x00,0x00,0x00,0xC7,0x01,0xFF};
    const std::uint8_t kGetSlotInstanceBytes[16] = {0x4C,0x8B,0xDC,0x48,0x83,0xEC,0x48,0x49,0x8D,0x43,0x08,0x49,0x89,0x53,0xD8,0x49};
    const std::uint8_t kGetPartDefFromIdBytes[16] = {0x4C,0x8B,0xDC,0x53,0x55,0x48,0x81,0xEC,0xB8,0x00,0x00,0x00,0x48,0x8B,0x01,0x33};

    // ---- UE4SS exports resolved at runtime ----
    using FNameCtorFn = FName*(__fastcall*)(FName*, const wchar_t*, int, void*);
    using ResizeAllocFn = void(__fastcall*)(void*, int, int, std::uint64_t);
    using GetValuePtrFn = void*(__fastcall*)(void*, const wchar_t*);   // UObject::GetValuePtrByPropertyNameInChain
    using IsRealFn = bool(__fastcall*)(const void*);                   // UObject::IsReal (static)
    FNameCtorFn g_fname_ctor{};
    ResizeAllocFn g_resize_alloc{};
    GetValuePtrFn g_get_value_ptr{};
    IsRealFn g_is_real{};

    // ---- voice game functions ----
    using SolveVoiceFn = void(__fastcall*)(int*, void*, const TArray<FGameplayTag>*);
    using GetSlotInstFn = void*(__fastcall*)(void*, const FGameplayTag*);
    using GetPartDefFn = void*(__fastcall*)(const FPrimaryAssetId*);
    SolveVoiceFn g_solve_voice_orig{};
    FPrimaryAssetId g_voice_src_id{};       // CPD_H_Outfit_Clo008_HELM_TintB (voice value donor)
    bool g_voice_helmet[kOfferCount]{};     // the four helmets that get the voice effect
    int g_voice_preset = -1;                // cached HelmetFxRtpcValue (>=0 once resolved)

    // ---- game functions ----
    using DoesPartFn = bool(__fastcall*)(const FPrimaryAssetId*, const FGameplayTagContainer*);
    using FilterFn = void(__fastcall*)(void*, const FGameplayTagContainer*, TArray<FPrimaryAssetId>*);
    using TagCopyFn = FGameplayTagContainer*(__fastcall*)(FGameplayTagContainer*, const FGameplayTagContainer*);
    using TagDtorFn = void(__fastcall*)(FGameplayTagContainer*);
    using TagAddFn = void(__fastcall*)(FGameplayTagContainer*, const FGameplayTag*);

    using MatchesFn = bool(__fastcall*)(void*, const void*);
    using CanEquipFn = bool(__fastcall*)(void*, void*);
    using HasAllFn = bool(__fastcall*)(const void*, const void*);   // FGameplayTagContainer::HasAll(required, character)

    // EXPERIMENTAL meditation-grant plumbing
    using FindItemFn = void*(__fastcall*)(const void*);                  // FindInventoryItem(FString*) -> UBrunoInventoryItem*
    using GetGearKitsFn = void(__fastcall*)(void*, const void*, void*);  // GetGearKitsWithTags(comp, tags, out)
    using ExecConsoleFn = void(__fastcall*)(void*, const void*, void*);  // ExecuteConsoleCommand(WorldContext, FString*, PlayerController*)
    struct FStringLite { const wchar_t* data; std::int32_t num; std::int32_t max; };

    std::uintptr_t g_base{};
    FPrimaryAssetId g_ids[kOfferCount]{};
    FGameplayTag g_lend[kLendMax]{};
    std::size_t g_lend_count{};
    FGameplayTag g_outfit_lend[kOutfitLendMax]{};   // human/structural tags lent only to armour parts
    std::size_t g_outfit_lend_count{};
    bool g_is_outfit[kOfferCount]{};                // true for the CPD_H_Outfit_* armour tiles
    bool g_is_vibro[kOfferCount]{};                 // true for CPD_WeaponSpec_Enemy_* (vibrosword lane)
    FGameplayTag g_b1{};                            // Class.Battledroid.B1 - lent ONLY to enemy-spec accepts()
    DoesPartFn g_does_part_orig{};
    FilterFn g_filter_orig{};
    MatchesFn g_matches_orig{};
    CanEquipFn g_can_equip_orig{};
    HasAllFn g_hasall_orig{};
    unsigned g_hasall_diag{};   // rate-limited meditation-lend miss diagnostics
    FGameplayTag g_names[kNameMax]{};   // Info.Name family that lock queries test for
    std::size_t g_names_count{};
    FGameplayTag g_uitype{}, g_lightsaber{};   // weapon-change gate tags (force that query true)
    FGameplayTag g_bespoke{};   // Padawan bespoke tag that gates the Force meditations
    FGameplayTag g_exotic{};    // Class.Exotic.Padawan - marks a character as a Padawan via our kit
    FGameplayTag g_deflect_name{};       // deflection whitelist lend, 1H/other lane (Info.Name.Anakin)
    FGameplayTag g_deflect_name_tel{};   // deflection whitelist lend, dual-saber lane (Info.Name.Tel-ReaVokoss)
    bool g_import_named[kImportNameCount]{};   // import display names already written this session
    void* g_import_cpd[kImportNameCount]{};    // CPD ptr the write landed on (reload -> rewrite)
    unsigned g_import_name_tries{};            // settled-hub attempts (capped)
    FName g_uifrag_class{};                    // "BitReactorUIDataFragment" for the fragment walk
    using FindFragFn = void*(__fastcall*)(void*, std::uint8_t);
    FindFragFn g_findfrag_orig{};              // v2.17.0 write-at-read-time hook
    bool g_texts_ready{};                      // precomputed FTexts available for the hook
    void* g_imp_text[kImportNameCount][2]{};   // [i][0]=name ITextData*, [i][1]=desc (null = none)
    std::uint32_t g_imp_flags[kImportNameCount][2]{};
    std::uint64_t g_imp_target[kImportNameCount]{};   // CPD object NamePrivate qwords (path leaf)
    std::uint64_t g_imp_icon[kImportNameCount]{};     // image-bank tag FName qword (0 = none)
    void* g_imp_frag_done[kImportNameCount]{};        // fragment ptr already dressed (per entry)
    bool g_balance_done{};                     // runtime balance landed at least once this session
    bool g_vibro_cdo_done{};                   // vibro CDO archetype write landed this session
    bool g_vibro_uitype_logged{};              // one-time log for the UIType lend
    FGameplayTag g_enemy_1h{};                 // Weapon.Enemy.1HMelee - vibro-wielder detect
    void* g_vibro_heal[8]{};                   // chars whose spawn cached the stance pre-CDO-fix
    std::uint64_t g_vibro_heal_name[8]{};
    unsigned g_vibro_heal_quiet[8]{};

    CacheStanceFn g_cache_stance_orig{};                        // saber stance fix
    FGameplayTag g_pwt_rifle{}, g_pwt_other{}, g_melee_spec{};  // PrimaryWeaponType Rifle/Other + melee-wielder detect
    FGameplayTag g_melee_2h{};                                  // Tel's DUAL-saber spec (the only broken lane)
    FGameplayTag g_melee_1h{};                                  // Anakin's saber spec (meditation lend must match it explicitly)
    // THE PRESENTATION SELECTOR (found via the F11 C_BindingAlias dump): the pawn component's
    // "Alias Tags" container carries Binding.AnimationCategory.Humanoid.2HRifle - it selects both
    // the equip-flow hold layer and the ability strike presentation. Everything else on the pawn is
    // already Tel-Rea (AS_TelRea / 2HSaber proxy / SAS_TelRea stances); this is the last lever.
    FGameplayTag g_cat_telrea{}, g_cat_hum2hrifle{};
    ProcessEventFn g_process_event{};                           // for the deferred SetAnimInstanceClass re-link
    GetFuncFn g_get_func{};
    void* g_pending_relink[kPendingMax]{};                      // characters awaiting a quiet-gated anim re-link
    std::uint64_t g_pending_name[kPendingMax]{};                // their NamePrivate (ptr-reuse guard, like g_flipped)
    unsigned g_pending_quiet[kPendingMax]{};                    // tick of the last stance activity seen for the char
    unsigned g_pending_last[kPendingMax]{};                     // tick of our last re-link of the char (0 = never)
    std::uint8_t g_pending_fails[kPendingMax]{};
    bool g_pending_revert[kPendingMax]{};                       // true = restoring the rifle grip after saber unequip
    void* g_flipped[kPendingMax]{};                             // chars whose PrimaryWeaponType we currently hold at Other
    std::uint64_t g_flipped_name[kPendingMax]{};                // their NamePrivate (ptr-reuse guard)
    unsigned g_flip_seq{};                                      // bumped on every hook-side flip (re-link race detect)
    // StanceAnimationSet asset names, for logging/verifying what the game's stance rebuild produced.
    constexpr std::size_t kSasCount = 7;
    constexpr const wchar_t* kSasNameStrs[kSasCount] = {
        STR("SAS_Human_2HRifle"), STR("SAS_Human_1HMelee"), STR("SAS_Human_1HThrowR"),
        STR("SAS_Human_1HSaber"), STR("SAS_Human_1HPistol"), STR("SAS_TelRea"), STR("SAS_TelRea_Force"),
    };
    constexpr const char* kSasLabels[kSasCount] = { "2HRifle", "1HMelee", "1HThrowR", "1HSaber", "1HPistol", "TelRea", "TelRea_Force" };
    FName g_sas_names[kSasCount]{};
    // Stance RETARGET (the missing third ingredient, ported from the proven Lua F8 probe): Hawks'
    // stance archetype is baked to rifle UPSTREAM of the owned tags - every load-time rebuild passes
    // the rifle archetype no matter what he owns (log-proven: pre-flipped rebuilds still produce
    // [2HRifle,1HThrowR]). Fix: call the game's own BlueprintCallable CacheStanceAnimationSetFromWeapon
    // with the MELEE archetype ourselves, auto-detect the bAdd polarity by reading the array back,
    // then remove the rifle set the same way. Self-verifying via check_stance_sets.
    using StaticFindFn = void*(__fastcall*)(void* cls, void* outer, const wchar_t* name, bool exact);
    StaticFindFn g_static_find{};    // UObjectGlobals::StaticFindObject_InternalSlow (UE4SS export)
    int g_badd_add = 1;              // bAdd=true = insert at FRONT/active (decompile-confirmed)
    bool g_in_stance_fix{};          // reentrancy guard: our own Cache calls must not re-enter hook logic
    // Tel-Rea's saber gear BPs (BP_LightSaber_Tel_Padawan/Master) are the ONLY weapons in the game
    // with an EMPTY StanceAnimationArchetypeAsset (Tel's pawn supplies his bespoke stance, so the
    // field was never authored) - so nothing loads SASArch_1HMelee in a saber-only mission and the
    // game's Cache call is a no-op for the saber. We must LOAD the archetype ourselves, using the
    // GAME's own ::StaticLoadObject (PDB-named) - NOT a UE4SS export: UE4SS wrappers throw C++
    // exceptions across the DLL boundary when an engine binding is missing, which is instant
    // std::terminate in this /MT DLL (the v1.8.0 crash). Class arg from the game's own idempotent
    // Z_Construct_UClass_UStanceAnimationSetArchetype (returns the cached UClass after startup).
    constexpr std::uintptr_t kStaticLoadObjectRva = 0x1AC2DA0;
    constexpr std::uintptr_t kZConstructArchClassRva = 0x6C2AFB0;
    constexpr std::uintptr_t kZConstructSasClassRva = 0x6C2ABE0;   // Z_Construct_UClass_UStanceAnimationSet
    const std::uint8_t kStaticLoadObjectBytes[16] = {0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x57,0x48,0x8D,0xAC,0x24,0xC0};
    const std::uint8_t kZConstructArchClassBytes[16] = {0x48,0x83,0xEC,0x28,0x48,0x8B,0x05,0x45,0x24,0x85,0x06,0x48,0x85,0xC0,0x75,0x1A};
    const std::uint8_t kZConstructSasClassBytes[16] = {0x48,0x83,0xEC,0x28,0x48,0x8B,0x05,0x55,0x28,0x85,0x06,0x48,0x85,0xC0,0x75,0x1A};
    using StaticLoadObjFn = void*(__fastcall*)(void* cls, void* outer, const wchar_t* name, const wchar_t* filename,
                                               unsigned int flags, void* sandbox, bool reconcile, const void* instancing);
    using ZConstructFn = void*(__fastcall*)();
    bool g_load_arch_ok{};   // both RVAs byte-verified at install

    // ================= 1.3: FORCE-MEDITATION GRANT (public 1.3 item 2) =================
    // The 12 Force meditations are exotic utility items (ItemType.Force, Rarity.Unique, not
    // Buyable) natively produced only by Tel-Rea's Personnel meditate action and equippable
    // only by the real Tel-Rea: each item's FGameplayTagRequirements (item+0x450) REQUIRES the
    // owned bespoke tag br.CharacterClass.Bespoke.Jedi.TelRea, and every armory eligibility
    // path funnels into FGameplayTagRequirements::RequirementsMet 0x58DFAB0 via the virtual
    // UBrunoUtilityInventoryItem::CanCharacterEquipItemType (ABrunoHQInventory::
    // CanCharacterEquipItemID also demands the item be OWNED first - HasItemID). Two parts:
    //  (1) GRANT: load each UtilityItem_*_T1 data asset (game's own StaticLoadObject, class
    //      from Z_Construct_UClass_UBrunoUtilityInventoryItem) and hand it to the game's own
    //      UBrunoGameStatics::GrantItem(ctx, item, 1) -> ABrunoHQInventory::AddItem, which
    //      itself clamps at the item's MaxAmount; we additionally skip items the company
    //      already owns (GetTotalAmountOwned > 0) so the pass is idempotent. Runs from
    //      on_update once the armory has been opened (g_world_ctx captured by hook_filter)
    //      and the HQ inventory actor exists (hub only).
    //  (2) EQUIP LEND: install hook_hasall on RequirementsMet (site + bytes at kHasAllRva) -
    //      see the hook for the lend condition (Padawan kit OR any Weapon.Melee spec).
    constexpr bool kGrantMeditations = true;
    constexpr std::uintptr_t kGrantItemRva = 0x7187EA0;        // UBrunoGameStatics::GrantItem(ctx, item, amount)
    constexpr std::uintptr_t kGetHqInventoryRva = 0x721B950;   // ABrunoHQInventory::GetInventory(ctx) [static]
    constexpr std::uintptr_t kGetTotalOwnedRva = 0x7220A90;    // ABrunoHQInventory::GetTotalAmountOwned(this, item)
    constexpr std::uintptr_t kZConstructUtilityItemRva = 0x7137180;  // Z_Construct_UClass_UBrunoUtilityInventoryItem
    const std::uint8_t kGrantItemBytes[16] = {0x48,0x89,0x6C,0x24,0x20,0x48,0x89,0x54,0x24,0x10,0x57,0x48,0x83,0xEC,0x30,0x41};
    const std::uint8_t kGetHqInventoryBytes[16] = {0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0xE8,0x32,0x57,0xEB,0xFF,0x80,0x3D};
    const std::uint8_t kGetTotalOwnedBytes[16] = {0x4C,0x8B,0xDC,0x55,0x41,0x56,0x49,0x8D,0x6B,0xA1,0x48,0x81,0xEC,0xB8,0x00,0x00};
    const std::uint8_t kZConstructUtilityItemBytes[16] = {0x48,0x83,0xEC,0x28,0x48,0x8B,0x05,0x1D,0xEC,0x34,0x06,0x48,0x85,0xC0,0x75,0x1A};
    using GrantItemFn = void(__fastcall*)(void* ctx, void* item, int amount);
    using GetHqInvFn = void*(__fastcall*)(void* ctx);
    using GetTotalOwnedFn = int(__fastcall*)(void* inv, void* item);
    constexpr std::size_t kMeditationCount = 12;
    const wchar_t* const kMeditationPaths[kMeditationCount] = {
        STR("/Game/Game/GameData/ItemData/UtilityItems/UtilityItem_FastHealing_T1.UtilityItem_FastHealing_T1"),
        STR("/Game/Game/GameData/ItemData/UtilityItems/UtilityItem_Tranquility_T1.UtilityItem_Tranquility_T1"),
        STR("/Game/Game/GameData/ItemData/UtilityItems/UtilityItem_Serenity_T1.UtilityItem_Serenity_T1"),
        STR("/Game/Game/GameData/ItemData/UtilityItems/UtilityItem_BondedToTheBlade_T1.UtilityItem_BondedToTheBlade_T1"),
        STR("/Game/Game/GameData/ItemData/UtilityItems/UtilityItem_SurgingHarmony_T1.UtilityItem_SurgingHarmony_T1"),
        STR("/Game/Game/GameData/ItemData/UtilityItems/UtilityItem_FortifiedWill_T1.UtilityItem_FortifiedWill_T1"),
        STR("/Game/Game/GameData/ItemData/UtilityItems/UtilityItem_VisionsOfTheFuture_T1.UtilityItem_VisionsOfTheFuture_T1"),
        STR("/Game/Game/GameData/ItemData/UtilityItems/UtilityItem_VisionsOfThePast_T1.UtilityItem_VisionsOfThePast_T1"),
        STR("/Game/Game/GameData/ItemData/UtilityItems/UtilityItem_ControlPain_T1.UtilityItem_ControlPain_T1"),
        STR("/Game/Game/GameData/ItemData/UtilityItems/UtilityItem_ListenThroughChaos_T1.UtilityItem_ListenThroughChaos_T1"),
        STR("/Game/Game/GameData/ItemData/UtilityItems/UtilityItem_AttunedToAllThings_T1.UtilityItem_AttunedToAllThings_T1"),
        STR("/Game/Game/GameData/ItemData/UtilityItems/UtilityItem_PresenceInTheForce_T1.UtilityItem_PresenceInTheForce_T1"),
    };
    bool g_med_grant_ok{};        // all grant-path RVAs byte-verified at install
    bool g_meds_granted{};        // every meditation accounted for this session (granted or owned)
    unsigned g_med_attempts{};    // grant passes run in the hub (capped)
    bool g_med_log_noctx{};       // one-time "waiting for a world context" diagnostic
    bool g_med_log_noinv{};       // one-time "waiting for the hub" diagnostic
    unsigned g_med_waits{};       // grant wait counter (drives the periodic still-waiting heartbeat)
    unsigned g_last_stance_tick{};   // tick of the last CacheStance call = "the world is loading/churning" signal
    constexpr unsigned kWorldQuietTicks = 300;   // no stance activity for ~5s = load settled (the proven-safe window)

    // ================= 1.3: FORCE JUMP (Jedi traversal nav filter) =================
    // "Force Jump" is not a kit passive: traversal is baked into the PAWN. Tel-Rea's pawn is the
    // only one whose BaseNavigationQueryFilter (a ClassProperty, live on every humanoid pawn) is
    // NavFilter_CharacterMovement_Jedi - that filter is what makes the Jedi jump links pathable
    // (the Mandalorian jetpack lane is the same pattern with its own filter). Lend it: write the
    // property to the Jedi filter class on saber wielders (fresh resolve per write - GC-safe, the
    // pawn roots the class the moment it is assigned), remember the original class per character,
    // restore it when the sabers come off. Class loads via the game's own StaticLoadObject with
    // class = UClass (loading a UClass object needs UClass::StaticClass as the validation class).
    constexpr bool kEnableForceJump = true;
    constexpr std::uintptr_t kUClassStaticClassRva = 0x18EA810;   // UClass::GetPrivateStaticClass()
    const std::uint8_t kUClassStaticClassBytes[16] = {0x4C,0x8B,0xDC,0x48,0x81,0xEC,0x88,0x00,0x00,0x00,0x48,0x8B,0x05,0x97,0x0D,0xAC};
    const wchar_t* const kJediNavFilterPath =
        STR("/Game/Game/Core/AI/EnvironmentalQueries/NavFilter_CharacterMovement_Jedi.NavFilter_CharacterMovement_Jedi_C");
    bool g_nav_ok{};              // load-path RVAs byte-verified at install
    bool g_nav_log_noprop{};      // one-time "property not found" diagnostic

    // ================= 1.4: GLOBAL SABER STOW SOCKETS (dual-saber choreography on ANY outfit) ====
    // Tel-Rea's LightsaberStrike sequences re-parent the sabers to lightsaber_store_l/_r mid-
    // choreography. Those attach points exist only as BONES on Tel's own outfit meshes (pak diff:
    // they are the ONLY animation-relevant skeleton delta vs other outfits; parent bone = pelvis,
    // which every humanoid body mesh has; the tel_hand names the diff also flagged turned out to
    // be MATERIAL SLOT names, not bones). No package in the game ships socket OBJECTS, and every
    // outfit mesh references its own per-kit USkeleton (SKEL_HAA_Hawks/_Jed001/_Man001/...), so a
    // one-shot skeleton injection cannot cover the wardrobe. Instead: hook the asset-level socket
    // lookups (USkeletalMesh::FindSocketAndIndex + FindSocketInfo - the funnel every component
    // GetSocketByName/DoesSocketExist/GetSocketTransform path virtual-dispatches into, verified
    // by disasm) and, ONLY when the game's own lookup missed, serve one of two pre-built
    // USkeletalMeshSocket objects carrying Tel's ref-pose pelvis offsets. Serve rules:
    //   - mesh has the requested name as a real BONE -> stand down (Tel's own meshes keep the
    //     native ANIMATED bone path; sockets cannot animate),
    //   - mesh has no pelvis bone -> stand down (hair/head/prop meshes must keep missing so the
    //     game's holding-socket search walks on to a body mesh),
    //   - otherwise serve (fixed pelvis-relative stow point; the stow moment reads as intended).
    // The two sockets are constructed once per session in a stance-quiet window (SEH-guarded,
    // retry-capped, memory-only - no asset loads, no world walks) via the game's own
    // StaticConstructObject_Internal + USkeletalMeshSocket native class, GC-rooted with
    // RF_MarkAsRootSet, outer = the engine package owning the class. Inert for non-saber content;
    // no engine arrays or caches are ever written. Hook bodies are read-only + thread-safe.
    constexpr bool kEnableStowSockets = false;   // 1.4 experiments PARKED 2026-08-30 (Alex: revert) - see SABER-BONES-GLOBAL-HANDOFF.md
    constexpr std::uintptr_t kFindSocketAndIndexRva = 0x4E6A920;   // USkeletalMesh::FindSocketAndIndex
    constexpr std::uintptr_t kFindSocketInfoRva = 0x4E6AA00;       // USkeletalMesh::FindSocketInfo
    constexpr std::uintptr_t kStaticConstructRva = 0x1ACA0A0;      // StaticConstructObject_Internal(Params&)
    constexpr std::uintptr_t kZConstructSocketRva = 0x42CB0E0;     // Z_Construct_UClass_USkeletalMeshSocket
    const std::uint8_t kFindSocketAndIndexBytes[16] = {0x48,0x89,0x5C,0x24,0x08,0x33,0xDB,0x41,0xC7,0x00,0xFF,0xFF,0xFF,0xFF,0x48,0x89};
    const std::uint8_t kFindSocketInfoBytes[16] = {0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48};
    const std::uint8_t kStaticConstructBytes[16] = {0x4C,0x8B,0xDC,0x55,0x53,0x41,0x56,0x49,0x8D,0xAB,0x28,0xFE,0xFF,0xFF,0x48,0x81};
    const std::uint8_t kZConstructSocketBytes[16] = {0x48,0x83,0xEC,0x28,0x48,0x8B,0x05,0x8D,0x7D,0x15,0x09,0x48,0x85,0xC0,0x75,0x1A};
    constexpr unsigned kStowFailCap = 8;   // construction attempts before the feature stands down

    // ================= 1.4: TRANSLATION-RETARGET LEND (the REAL dual-saber mechanism) ==========
    // v2.7.x's stow sockets were aimed at the wrong mechanism: live testing showed ZERO socket
    // lookups for lightsaber_store_* (nothing in the game references those bones - gear kits
    // attach the sabers at weapon_pistol_l/r, hand-child bones present on every mesh). The real
    // delta (pak recon 2026-08-29 night, Release\ZCSaberBones): per-bone TRANSLATION RETARGETING
    // in the skeleton assets' BoneTree. SKEL_HSTF_TelRea marks 289/298 bones AnimationRelative(3)
    // (only root/Weapon/ik_* stay Skeleton(1)); EVERY other kit skeleton (SKEL_HAA_Hawks/_Jed001/
    // _B00/...) marks ALL bones Skeleton(1) = animated bone TRANSLATIONS are STRIPPED and replaced
    // with the target skeleton's ref pose. Tel's choreography flies the off-hand saber by
    // ANIMATING weapon_pistol_l/r translations (and drives arm support bones the same way) - on
    // any foreign skeleton those translations are discarded: saber glued to the hand ("teleports
    // along"), arms glitch. Wearing Tel's robes binds SKEL_HSTF_TelRea as the leader mesh's
    // skeleton - which is why the robes fixed everything (the bones were a red herring; the
    // skeleton REFERENCE is the active ingredient).
    // The lend: for dual-saber wielders, set the leader mesh skeleton's BoneTree modes 1 -> 3 for
    // every bone EXCEPT Tel's own mode-1 set (root/Weapon/ik_foot_root/l/r/ik_hand_root/weapon/
    // l/r). Tel's skeleton is living proof AnimationRelative-everywhere is safe with this game's
    // full anim set (Tel plays all generic humanoid anims through it). One-way per session
    // (skeleton assets are shared across characters; a revert could break a concurrent wielder),
    // idempotent, re-run each settled window so outfit swaps / reloaded skeletons re-patch.
    // Layout facts (this build): USkeleton::BoneTree @ +0x38 (data ptr +0x38, num +0x40) with
    // FBoneNode compiled down to ONE BYTE per bone (deprecated fields stripped) - authoritative:
    // FAnimationRuntime::GetBoneTranslationRetargetingMode (RVA 0x4380470) reads
    // `movzx eax, byte [skel+0x38's data + index]` with bounds vs [skel+0x40].
    // USkeleton::ReferenceSkeleton @ +0x48 (raw info ptr @+0x48, raw num @+0x50, FINAL
    // FMeshBoneInfo* @+0x68, final num @+0x70 - disasm of UpdateReferencePoseFromMesh);
    // component asset @ +0x5D8 (proven). BoneTree found via reflection AND cross-checked against
    // the +0x38 raw member the engine reads (pointers must agree) before any write.
    constexpr bool kEnableRetargetFix = false;   // 1.4 experiments PARKED 2026-08-30 (falsified live) - see SABER-BONES-GLOBAL-HANDOFF.md
    constexpr std::size_t kSkelBoneTreeOff = 0x38;
    constexpr std::size_t kSkelRefSkelOff = 0x48;
    constexpr std::uint8_t kModeSkeleton = 1, kModeAnimRelative = 3;

    // ================= 1.4 v2.10.0: HAND-IK RETARGET LEND (the PROVEN mechanism) ==========
    // Probe v6 mid-strike captures (2026-08-30, probe_strike.txt; analysis in
    // SABER-BONES-GLOBAL-HANDOFF.md): the saber float choreography (weapon_pistol_l
    // translations, ~1m out and back) plays IDENTICALLY on robed and non-robed rigs -
    // translations DO flow cross-skeleton; only the ARMS diverge, grossly and segment-wise
    // (hand separation anti-correlated: 7.6cm vs 105cm at aligned montage positions).
    // Rig topology and IK ref poses are IDENTICAL between SK_HSTF_TelRea and the HAA leader
    // (chain_diff.py / ik_refpose.py in Release\ZCSaberBones) -> the divergence is a RUNTIME
    // system present on one rig only: the foreign leader mesh runs PPABP_Proportions as its
    // post-process ABP (the robed leader runs ABP_HSTF_TelRea instead), and its name map
    // carries `UseWeaponHandIK` + `ik_hand_weapon` + per-limb offsets + 8 ModifyBone nodes:
    // it re-plants the hands via weapon-hand IK after proportion scaling.
    // SKEL_HSTF_TelRea marks its ik bones translation-retarget mode Skeleton(1), and
    // DecompressPose consults the SOURCE (anim's) skeleton -> for TEL-AUTHORED anims the
    // animated ik_hand_* translations are DISCARDED on every rig (replaced by the static
    // chest-center ref-pose local); HAA-authored anims flow theirs (HAA skeletons have no
    // mode-1 bones, v2.9.1 live read). So through Tel's strikes the proportions solver chases
    // a phantom chest-level weapon while the FK tracks fly the real choreography = arms wonky
    // mid-strike, hands clamped or behind the player - every recorded symptom. Rifle/Anakin
    // lanes clean (their ik flows), robed/Tel lanes clean (no proportions post-process).
    // THE LEND: flip the 4 hand-IK bones on SKEL_HSTF_TelRea from Skeleton(1) to Animation(0)
    // so Tel's authored ik translations flow everywhere. On Tel's own mesh the flowing values
    // equal the authoring intent (his rig) - no regression expected. One-way, idempotent,
    // re-checked each settled window (covers skeleton asset reloads), find-only lookup (never
    // sync-loads: the skeleton is resident whenever Tel sabers/robes/anims are), and the full
    // v2.8.1 BoneTree layout validation runs before any write.
    constexpr bool kEnableIkLend = false;   // RETIRED 2026-08-30: live read proved the ik bones were ALREADY Animation(0) - pure no-op (the offline BoneTree decode was wrong for BOTH skeleton families)
    constexpr const wchar_t* kTelSkelPath = L"/Game/Game/Characters/Humanoid/TelRea/Model/SKEL_HSTF_TelRea.SKEL_HSTF_TelRea";
    constexpr const wchar_t* kIkLendBones[] = {
        STR("ik_hand_root"), STR("ik_hand_weapon"), STR("ik_hand_l"), STR("ik_hand_r"),
    };
    constexpr std::size_t kIkLendBoneCount = sizeof(kIkLendBones) / sizeof(kIkLendBones[0]);
    constexpr std::uint8_t kModeAnimation = 0;

    // ================= 1.4: TEL-RIG LEADER MESH LEND â€” THE DUAL-SABER FIX =================
    // âœ… CONFIRMED WORKING END-TO-END 2026-08-30 (v2.12.0 live test): clean Tel-Rea dual-saber
    // strike choreography on ANY outfit â€” combat pose, off-hand blade ignition, collision
    // sparks and attack sounds all intact. Closes the 50-session saga (full history:
    // SABER-BONES-GLOBAL-HANDOFF.md).
    //
    // MECHANISM (each link falsified/verified live):
    // 1. Tel's strike anims are authored on SKEL_HSTF_TelRea. On any other outfit the leader
    //    mesh binds a SKEL_HAA_* skeleton -> the engine's cross-skeleton handling runs at
    //    decompression, and the rigs' ref-pose ROTATION conventions differ exactly where it
    //    glitches (weapon_pistol_r 180deg, fingers ~10deg, hands/upperarms ~5deg). Wearing
    //    Tel's robes works because the game swaps the LEADER MESH to a Tel-rig asset =
    //    same-skeleton playback. (Falsified along the way: stow bones, skeleton sockets,
    //    translation-retarget modes, hand-IK modes, PPABP_Proportions, UE linked layers.)
    // 2. THE LEND: for Melee.2H wielders, swap the leader mesh to SK_HSTF_TelReaA_NullMesh -
    //    the game's own INVISIBLE Tel-rig null leader (per-rig-family null leaders are the
    //    game's design; visible body = follower part meshes, which re-map by bone name since
    //    the HAA bone set is a strict subset of Tel's rig). Normal outfit visuals, Tel-rig
    //    animation. Settled-window ProcessEvent SetSkeletalMeshAsset; registry + revert on
    //    unequip; robed leaders skipped (theirs IS the visible robe body).
    // 3. SetSkeletalMeshAsset re-creates the anim instance, losing two pieces of equip-time
    //    instance state, both restored in the same call:
    //    a. "Proxy Table"/"Last Frame Proxy Table" (BitReactor's weapon-specialization anim
    //       selector; null = no combat-hold pose) - re-seeded from the pawn's CachedProxyTable.
    //    b. The multicast delegate subscriptions (OnLightSaberActivate/EnableCollision/...)
    //       through which the strike anims' notify states drive blade ignition, collision
    //       (sparks) and sounds - the saber actors bound them to the OLD instance at equip.
    //       Migrated by stealing each non-empty FMulticastScriptDelegate list (POD entries
    //       referencing the surviving saber actors) onto the new instance; restored to the
    //       old instance if the swap fails; same migration on the revert path.
    // Self-healing: re-applied each quiet window (asset reloads, outfit changes, late loads
    // all recover); first-attempt StaticLoadObject misses are normal and retried.
    constexpr bool kEnableLeaderSwap = true;
    constexpr const wchar_t* kTelNullLeaderPath = L"/Game/Game/Characters/Humanoid/TelRea/Model/SK_HSTF_TelReaA_NullMesh.SK_HSTF_TelReaA_NullMesh";
    constexpr std::uintptr_t kZConstructSkelMeshRva = 0x4E66970;   // Z_Construct_UClass_USkeletalMesh
    const std::uint8_t kZConstructSkelMeshBytes[16] = {0x48,0x83,0xEC,0x28,0x48,0x8B,0x05,0x95,0x8D,0x5D,0x08,0x48,0x85,0xC0,0x75,0x1A};

    // v2.12.0: THE DELEGATE MIGRATION. Probe v6.5 proved the notify STATES fire fine on the
    // swapped char (SaberCollision.Begin/End identical on swapped vs robed strikes) - the
    // extras die because the notify bodies BROADCAST on the ANIM INSTANCE's multicast
    // delegates (OnLightSaberActivate/EnableCollision/... - F6-dump-proven properties of
    // ABP_BR_Humanoid_Base_C), and the saber actors SUBSCRIBED to the OLD instance at equip.
    // The swap replaces the instance; broadcasts go into the void -> no ignition, no
    // collision-sparks, no sounds. Fix: steal each non-empty delegate invocation list
    // (FMulticastScriptDelegate = TArray<FScriptDelegate{FWeakObjectPtr,FName}> - POD
    // entries pointing at the SABER actors, which survive the swap) off the old instance
    // (zeroing its header so teardown cannot free the buffer) and install it on the new
    // instance's same-named property. Applied on the revert path too (the rifle's own
    // equip-time wiring would die the same way when the original leader is restored).
    constexpr const wchar_t* kMigrateDelegates[] = {
        L"OnLightSaberActivate", L"OnLightSaberDeactivate",
        L"OnLightSaberEnableCollision", L"OnLightSaberDisableCollision",
        L"OnLightsaberSpinActivate", L"OnLightsaberSpinDeactivate",
        L"OnPlayMontageNotifyBegin", L"OnPlayMontageNotifyEnd",
    };
    constexpr std::size_t kMigrateCount = sizeof(kMigrateDelegates) / sizeof(kMigrateDelegates[0]);

    // v2.11.4 diagnostic - RETIRED in v2.12.0: two full sessions produced ZERO lines including
    // robed full-FX strikes; either the family is unused in gameplay or the hook never
    // installed. Toggled off to reduce surface.
    constexpr bool kEnableGkDiag = false;
    constexpr std::uintptr_t kGkFindRva = 0x6ECD830;   // UGearKitComponent::FindSingleGearItemWithTags
    const std::uint8_t kGkFindBytes[16] = {0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57};
    constexpr unsigned kGkDiagLineCap = 80;
    constexpr const wchar_t* kRetargetExcl[] = {   // Tel's own mode-1 (Skeleton) set - never flip
        STR("root"), STR("Weapon"),
        STR("ik_foot_root"), STR("ik_foot_l"), STR("ik_foot_r"),
        STR("ik_hand_root"), STR("ik_hand_weapon"), STR("ik_hand_l"), STR("ik_hand_r"),
    };
    constexpr std::size_t kRetargetExclCount = sizeof(kRetargetExcl) / sizeof(kRetargetExcl[0]);

    // ================= DEN COLD-LOAD CRASH GUARD (game-bug workaround) =================
    // GAME BUG, not ours (reproduced with the mod fully disabled): loading a Den/hub save as
    // the FIRST world after boot leaves the first-registered handler in UWorld's OnActorSpawned
    // multicast list (world+0x460) pointing at FREED memory - its owner dies without unbinding.
    // Any later actor-iterator use (GetActorOfClass etc., from arbitrary BPs) compacts the list,
    // virtual-calls the dangling instance and ACCESS-VIOLATIONs; mission-first loads are clean,
    // which is why "load a mission save, then the Den save" works (Alex's discovery). Five crash
    // dumps analyzed - identical signature (see dump_analyze.py in Release\ZCMeditations).
    // The guard: every frame, resolve the current world (walk the cached context's Outer chain
    // to the object whose primary vtable is UWorld's), SEH-probe each invocation-list element's
    // instance pointer, and null BOTH element fields when the instance is unreadable or its
    // vtable lies outside the exe image. The engine's own compaction then drops null entries
    // through its inst==nullptr path - no virtual call on dead memory can ever happen.
    // Elements are 16 bytes: {IDelegateInstance* inst, u64 handle}.
    // v2.6.0: DISABLED - the "game bug" this guarded against was OUR OWN v2.4.0 defect (the
    // enabled.txt-invalidated tests hid it): mid-load GetActorOfClass calls from the meditation
    // grant (armed by v2.4.0's character ctx capture) leaked iterator delegate handles when a
    // swallowed SEH fault skipped their cleanup. Cause removed in v2.6.0 (armory-only ctx),
    // so the symptom guard goes too. Code kept for reference only.
    // v2.20.0 (2026-08-31): RE-ENABLED. The v2.6.0 "cause removed" conclusion was incomplete -
    // the OnActorSpawned corruption recurred on 2.18.6 (Alex's cold-boot-into-Den crashes, stack
    // GetActorOfClass -> RemoveDelegateInstance -> RemoveAtSwapImpl AV). Root cause this time =
    // ASC tag broadcasts fired during load churn (flip_pwt/swap_pwt in hook_cache_stance) landing
    // mid-actor-spawn with equipped meditation gameplay-effect listeners (the exact mechanism the
    // hook_cache_stance comment at ~3653 flagged for the bespoke grant, which was moved to settled
    // but the PWT-flip broadcast was not). This guard is the robust symptom fix: it scrubs the
    // dangling OnActorSpawned entry at the two compaction entry points (hook_remove_deleg =
    // RemoveDelegateInstance, exactly the crash frame) BEFORE the engine walks the list, so the AV
    // cannot happen regardless of which broadcast poisons it. Install byte-verifies both RVAs;
    // ready line reports denfix (7 = frame scan + both sync hooks armed).
    constexpr bool kEnableDenLoadFix = true;
    constexpr std::uintptr_t kUWorldVtableRva = 0xB4AB4A0;   // primary UWorld vftable (dump-confirmed)
    constexpr std::uintptr_t kWorldSpawnListOff = 0x460;     // OnActorSpawned TMulticastDelegateBase
    // v2.5.1: the per-frame scan alone lost a race - a freed-but-not-yet-recycled instance still
    // reads as valid (old vtable intact), then gets heap-recycled between our scan and the
    // engine's compaction walk. Watertight version: ALSO scrub synchronously at the two
    // compaction entry points, immediately before the engine walks the very same list -
    //   (1) TMulticastDelegateBase<FDefaultDelegateUserPolicy>::RemoveDelegateInstance (its
    //       inline compaction was the first three crash dumps),
    //   (2) FActorIteratorState's constructor (registers the OnActorSpawned handler and
    //       compacts on add - the Niagara-caller crash dumps).
    // Same thread, same call, nothing can recycle the memory in between. The frame scan stays
    // as an early-detection layer and now reads the world straight from the GWorld global, so
    // it is armed from the first tick instead of waiting for a character context.
    constexpr std::uintptr_t kGWorldRva = 0xD2BD948;          // UWorld* GWorld (PDB)
    constexpr std::uintptr_t kRemoveDelegRva = 0x1690EE0;     // TMulticastDelegateBase<FDefault...>::RemoveDelegateInstance
    const std::uint8_t kRemoveDelegBytes[16] = {0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57};
    constexpr std::uintptr_t kActorItCtorRva = 0x39737A0;     // FActorIteratorState::FActorIteratorState
    const std::uint8_t kActorItCtorBytes[16] = {0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x55,0x57,0x41,0x54,0x41,0x56};
    using RemoveDelegFn = void(__fastcall*)(void*, void*);
    using ActorItCtorFn = void*(__fastcall*)(void*, void*, void*, void*);
    RemoveDelegFn g_remove_deleg_orig{};
    ActorItCtorFn g_actorit_orig{};
    unsigned g_denfix_count{};                               // total neutralized (log counter)
    void* g_nav_ch[kPendingMax]{};            // chars whose nav filter we swapped to Jedi
    std::uint64_t g_nav_name[kPendingMax]{};  // their NamePrivate (ptr-reuse guard)
    void* g_nav_orig[kPendingMax]{};          // the original filter class to restore

    // v2.19.0 BUILD 1 (2026-08-31): CLASS-LOCK FIX registry. The deflect/assist lend adds a HERO
    // Info.Name owned tag (Info.Name.Anakin / Tel-ReaVokoss) to mission saber wielders; the game's
    // spec/talent lock query ANY(br...Info.Name) then MATCHES the recruit and shows the
    // "cannot be changed for this operator" padlock. hook_matches only cancels that at ONE BP call
    // site (kBpQueryRetRva), so any other evaluation still locks. The hub strip in the pending loop
    // (see on_update ~4251) only runs for chars still queued in g_pending_relink, so an operator
    // that carried the lent tag into the hub without a fresh stance-cache event is NEVER stripped ->
    // random padlock (both user workarounds confirm the mechanism: unequip strips the tag;
    // save+reload drops it because it's a loose runtime tag, never serialized). This registry records
    // EVERY char we lend a name tag to; strip_lent_names_in_hub() clears both tags from every live
    // registered char whenever we are in the hub, independent of relink-queue membership. It only
    // ever holds chars WE lent to (real Tel/Anakin never enter the pending registry), so it can never
    // strip a genuine hero's own identity tag.
    // v2.20.0: GATED OFF for the den-crash build so the ONLY active change vs stable 2.18.6 is the
    // den guard (clean one-change-per-build attribution). The code is preserved; flip this true for
    // its own isolated test once the den crash is confirmed fixed. Was BUILD 1 / ver 2.19.0.
    // v2.21.0: RE-ENABLED on top of the den guard - but v2.21.0 crashed (SignificanceManager), which
    // turned out to be the broadcast-during-load root cause (NOT this fix; it never ran). v2.22.0:
    // GATED OFF AGAIN to isolate OPTION B (kEnableOptionB) vs the confirmed-good v2.20.0. Re-enable
    // for its own test once Option B confirms the cold-load crash is gone at the source.
    constexpr bool kEnableClassLockFix = false;
    void* g_lentname_ch[kPendingMax]{};
    std::uint64_t g_lentname_id[kPendingMax]{};   // NamePrivate (ptr-reuse guard)

    GetGearKitsFn g_getgk_orig{};   // EXPERIMENTAL trigger
    bool g_med_probe_done{};
    void* g_world_ctx{};            // a valid UObject cached from a hook, used as console WorldContext
    unsigned g_update_ticks{};      // frames since load
    bool g_console_test_sent{};

    struct Trampoline { std::uintptr_t target{}; std::uint8_t saved[24]{}; std::size_t n{}; void* code{}; bool active{}; };
    Trampoline g_does_part_tr{}, g_filter_tr{}, g_matches_tr{}, g_can_equip_tr{}, g_hasall_tr{}, g_getgk_tr{};
    Trampoline g_findfrag_tr{};   // v2.17.0 UIData fragment write-at-read-time hook
    Trampoline g_solve_voice_tr{};
    Trampoline g_update_ref_tr{}, g_update_prev_tr{};
    Trampoline g_cache_stance_tr{};
    Trampoline g_remove_deleg_tr{}, g_actorit_tr{};
    Trampoline g_findsock_tr{}, g_findsockinfo_tr{};

    wchar_t g_log_path[MAX_PATH]{};
    HMODULE g_self{};

    auto logf(const char* msg) -> void
    {
        if (g_log_path[0] == 0) return;
        FILE* f = _wfopen(g_log_path, L"a");
        if (!f) return;
        std::fputs("[ZCUnlocked] ", f);
        std::fputs(msg, f);
        std::fputc('\n', f);
        std::fclose(f);
    }

    // ---- tiny absolute-jmp trampoline hook (prologues are position-independent) ----
    auto install_hook(std::uintptr_t target, void* detour, std::size_t n, Trampoline& tr) -> void*
    {
        auto* code = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, n + 14, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (!code) return nullptr;
        std::memcpy(tr.saved, reinterpret_cast<void*>(target), n);
        std::memcpy(code, reinterpret_cast<void*>(target), n);       // copied prologue
        code[n] = 0xFF; code[n + 1] = 0x25; *reinterpret_cast<std::uint32_t*>(code + n + 2) = 0;
        *reinterpret_cast<std::uint64_t*>(code + n + 6) = target + n; // jmp back to target+n

        DWORD old{};
        if (!VirtualProtect(reinterpret_cast<void*>(target), n, PAGE_EXECUTE_READWRITE, &old))
        {
            VirtualFree(code, 0, MEM_RELEASE);
            return nullptr;
        }
        auto* t = reinterpret_cast<std::uint8_t*>(target);
        t[0] = 0xFF; t[1] = 0x25; *reinterpret_cast<std::uint32_t*>(t + 2) = 0;
        *reinterpret_cast<std::uint64_t*>(t + 6) = reinterpret_cast<std::uint64_t>(detour);
        for (std::size_t i = 14; i < n; ++i) t[i] = 0x90;
        VirtualProtect(reinterpret_cast<void*>(target), n, old, &old);
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(target), n);

        tr.target = target; tr.n = n; tr.code = code; tr.active = true;
        return code;
    }

    auto remove_hook(Trampoline& tr) -> void
    {
        if (!tr.active) return;
        DWORD old{};
        if (VirtualProtect(reinterpret_cast<void*>(tr.target), tr.n, PAGE_EXECUTE_READWRITE, &old))
        {
            std::memcpy(reinterpret_cast<void*>(tr.target), tr.saved, tr.n);
            VirtualProtect(reinterpret_cast<void*>(tr.target), tr.n, old, &old);
            FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(tr.target), tr.n);
        }
        if (tr.code) VirtualFree(tr.code, 0, MEM_RELEASE);
        tr.active = false;
    }

    auto make_fname(const wchar_t* s, int mode) -> FName
    {
        FName f{};
        g_fname_ctor(&f, s, mode, nullptr);
        return f;
    }

    auto same_id(const FPrimaryAssetId& a, const FPrimaryAssetId& b) -> bool { return std::memcmp(&a, &b, sizeof(FPrimaryAssetId)) == 0; }

    auto listed(const TArray<FPrimaryAssetId>& list, const FPrimaryAssetId& id) -> bool
    {
        for (std::int32_t i = 0; i < list.num; ++i) if (same_id(list.data[i], id)) return true;
        return false;
    }

    auto array_add(TArray<FPrimaryAssetId>* arr, const FPrimaryAssetId& v) -> void
    {
        if (arr->num >= arr->max)
        {
            const int new_max = arr->max > 0 ? arr->max * 2 : 4;
            g_resize_alloc(&arr->data, arr->num, new_max, sizeof(FPrimaryAssetId));
            arr->max = new_max;
        }
        arr->data[arr->num] = v;
        arr->num += 1;
    }

    auto has_prefix(const wchar_t* s, const wchar_t* p) -> bool
    {
        for (; *p != 0; ++s, ++p) if (*s != *p) return false;
        return true;
    }

    // Is this id one of the Mandalorian armour (CPD_H_Outfit_*) tiles we make all-species?
    auto id_is_outfit(const FPrimaryAssetId& id) -> bool
    {
        for (std::size_t i = 0; i < kOfferCount; ++i)
            if (g_is_outfit[i] && same_id(id, g_ids[i])) return true;
        return false;
    }

    // Is this id one of the enemy weapon specs we offer (the vibrosword lane)?
    auto id_is_vibro(const FPrimaryAssetId& id) -> bool
    {
        for (std::size_t i = 0; i < kOfferCount; ++i)
            if (g_is_vibro[i] && same_id(id, g_ids[i])) return true;
        return false;
    }

    // Does the game's own check accept this part once we lend the character the authored
    // identity tags? Structural requirements (rig / species / body / slot / facts) still
    // come from the real owned tags, so this stays honest about those - EXCEPT for the
    // Mandalorian armour parts when kMandoAllSpecies is on, where we also lend the human /
    // humanoid structural tags so any species can wear them (see kOutfitLendTags).
    auto accepts(const FPrimaryAssetId& id, const FGameplayTagContainer& owned) -> bool
    {
        if (g_does_part_orig == nullptr) return false;
        if (g_does_part_orig(&id, &owned)) return true;
        alignas(FGameplayTagContainer) std::uint8_t storage[sizeof(FGameplayTagContainer)]{};
        auto* lent = reinterpret_cast<FGameplayTagContainer*>(storage);
        const auto copy = reinterpret_cast<TagCopyFn>(g_base + kTagCopyRva);
        const auto destroy = reinterpret_cast<TagDtorFn>(g_base + kTagDtorRva);
        const auto add = reinterpret_cast<TagAddFn>(g_base + kTagAddRva);
        copy(lent, &owned);
        for (std::size_t i = 0; i < g_lend_count; ++i) add(lent, &g_lend[i]);
        if (kMandoAllSpecies && id_is_outfit(id))
            for (std::size_t i = 0; i < g_outfit_lend_count; ++i) add(lent, &g_outfit_lend[i]);
        // v2.14.1: the Coil Striker vibrosword spec requires Class.Battledroid.B1 (the enemy
        // spec's authored gate; found when v2.14.0's offer produced no armory tile). Lend it
        // ONLY for our offered enemy specs - a global B1 lend would open droid-only parts.
        if (g_b1.tag_name.comparison_index != 0 && id_is_vibro(id))
            add(lent, &g_b1);
        const bool ok = g_does_part_orig(&id, lent);
        destroy(lent);
        return ok;
    }

    auto __fastcall hook_does_part(const FPrimaryAssetId* id, const FGameplayTagContainer* owned) -> bool
    {
        if (g_does_part_orig == nullptr || id == nullptr || owned == nullptr) return false;
        return accepts(*id, *owned);
    }

    auto __fastcall hook_filter(void* subsystem, const FGameplayTagContainer* owned, TArray<FPrimaryAssetId>* out) -> void
    {
        if (g_filter_orig == nullptr) return;
        g_filter_orig(subsystem, owned, out);
        if (g_world_ctx == nullptr && subsystem != nullptr) g_world_ctx = subsystem;   // EXPERIMENTAL: cache a live UObject for console WorldContext
        if (owned == nullptr || out == nullptr) return;
        for (std::size_t i = 0; i < kOfferCount; ++i)
        {
            if (listed(*out, g_ids[i])) continue;
            if (accepts(g_ids[i], *owned)) array_add(out, g_ids[i]);
        }
    }

    // FGameplayTagQuery::Matches hook: TagDictionary.Data at query+0x08, .Num at query+0x10,
    // each entry an FGameplayTag (an FName, 8 bytes). If the query tests the Info.Name tag it is
    // the spec/talent lock query -> report "no match" so the operator's spec/talent stays changeable.
    bool g_logged_name[kNameMax]{};

    auto __fastcall hook_matches(void* query, const void* container) -> bool
    {
        if (g_matches_orig == nullptr) return false;
        // Only the Blueprint DoesContainerMatchTagQuery node (spec/talent/weapon lock UIs) may be
        // overridden. Any other caller - above all the native roster filter that lists active operators
        // - must get the real result. Gate strictly on the BP thunk's Matches-call return address.
        if (reinterpret_cast<std::uintptr_t>(_ReturnAddress()) != g_base + kBpQueryRetRva)
            return g_matches_orig(query, container);
        if (query != nullptr && g_names_count != 0)
        {
            auto* q = reinterpret_cast<std::uint8_t*>(query);
            auto* dict = *reinterpret_cast<FGameplayTag**>(q + 0x08);
            const auto num = *reinterpret_cast<std::int32_t*>(q + 0x10);
            if (dict != nullptr)
            {
                bool has_name = false, has_uitype = false, has_saber = false;
                auto eq = [](const FName& a, const FName& b) { return a.comparison_index == b.comparison_index && a.number == b.number; };
                for (std::int32_t i = 0; i < num; ++i)
                {
                    const FName& t = dict[i].tag_name;
                    if (g_uitype.tag_name.comparison_index != 0 && eq(t, g_uitype.tag_name)) has_uitype = true;
                    if (g_lightsaber.tag_name.comparison_index != 0 && eq(t, g_lightsaber.tag_name)) has_saber = true;
                    for (std::size_t j = 0; j < g_names_count; ++j)
                        if (eq(t, g_names[j].tag_name)) { has_name = true; break; }
                }
                // weapon-change gate ("can change unless lightsaber") -> force TRUE so a lightsaber is changeable
                if (has_uitype && has_saber) return true;
                // spec/talent hero-identity lock -> force FALSE so hero kits stay changeable
                if (has_name) return false;
            }
        }
        return g_matches_orig(query, container);
    }

    // UBrunoUtilityInventoryItem::CanCharacterEquipItemType(item, character). The item's required
    // tags live at item+0x450 (TArray<FGameplayTag>: data at +0x450, count at +0x458). Return true
    // when they include the Padawan bespoke tag so the Force meditations become equippable.
    auto __fastcall hook_can_equip(void* item, void* character) -> bool
    {
        if (g_can_equip_orig == nullptr) return false;
        if (item != nullptr && g_bespoke.tag_name.comparison_index != 0)
        {
            auto* req = reinterpret_cast<std::uint8_t*>(item) + 0x450;
            auto* tags = *reinterpret_cast<FGameplayTag**>(req);
            const auto num = *reinterpret_cast<std::int32_t*>(req + 0x08);
            if (tags != nullptr)
                for (std::int32_t i = 0; i < num; ++i)
                    if (tags[i].tag_name.comparison_index == g_bespoke.tag_name.comparison_index &&
                        tags[i].tag_name.number == g_bespoke.tag_name.number) return true;
        }
        return g_can_equip_orig(item, character);
    }

    // FGameplayTagRequirements::RequirementsMet(requirements, ownedTags) - the site the v1.1.1
    // comments called "HasAll". Every utility-item eligibility check funnels here: the virtual
    // UBrunoUtilityInventoryItem::CanCharacterEquipItemType collects the character actor's owned
    // gameplay tags (which include the customization spec-part tags) and evaluates the item's
    // requirements struct (RequireTags TArray at +0). The meditations REQUIRE the bespoke tag
    // br.CharacterClass.Bespoke.Jedi.TelRea, which only the class OBJECT Class_Padawan_TelRea
    // carries - so only the real Tel-Rea ever passes. The lend: when a query REQUIRES the bespoke
    // tag AND the queried container carries the Padawan class kit OR any Weapon.Melee spec (the
    // 1.3 "meditations on saber equip" rule; the container's ParentTags are checked too so any
    // Melee child - Tel's 2H, Anakin's 1H - matches), answer true. Any query not requiring the
    // bespoke tag falls straight through to the original - one cheap scan of RequireTags.
    // Deliberately NOT unconditional: enemy logic also queries this tag (GA_PrepareToKill_SC).
    auto __fastcall hook_hasall(const void* required, const void* character) -> bool
    {
        if (g_hasall_orig == nullptr) return false;
        if (required != nullptr && character != nullptr && g_bespoke.tag_name.comparison_index != 0)
        {
            auto* rt = *reinterpret_cast<FGameplayTag* const*>(required);
            const auto rnum = *reinterpret_cast<const std::int32_t*>(reinterpret_cast<const std::uint8_t*>(required) + 0x08);
            bool wants_bespoke = false;
            if (rt != nullptr && rnum > 0 && rnum <= 64)
                for (std::int32_t i = 0; i < rnum; ++i)
                    if (rt[i].tag_name.comparison_index == g_bespoke.tag_name.comparison_index &&
                        rt[i].tag_name.number == g_bespoke.tag_name.number) { wants_bespoke = true; break; }
            if (wants_bespoke)
            {
                const auto* cont = reinterpret_cast<const FGameplayTagContainer*>(character);
                auto scan = [](const TArray<FGameplayTag>& arr, const FGameplayTag& want) -> bool
                {
                    if (want.tag_name.comparison_index == 0 || arr.data == nullptr || arr.num <= 0 || arr.num > 512) return false;
                    for (std::int32_t i = 0; i < arr.num; ++i)
                        if (arr.data[i].tag_name.comparison_index == want.tag_name.comparison_index &&
                            arr.data[i].tag_name.number == want.tag_name.number) return true;
                    return false;
                };
                // explicit children matter: containers built without FillParentTags carry only
                // the leaf spec tag (Melee.2H = Tel's sabers, Melee.1H = Anakin's - the v2.4.0
                // "only my equipped meditations show" bug was the missing 1H here)
                if (scan(cont->tags, g_exotic) || scan(cont->tags, g_melee_spec) ||
                    scan(cont->tags, g_melee_2h) || scan(cont->tags, g_melee_1h) ||
                    scan(cont->parents, g_melee_spec))
                    return true;
                // lend MISSED for a bespoke-requiring query: log what the container held (rate-
                // limited) so "meditations not listed" reports are diagnosable from the log
                if (g_hasall_diag < 8)
                {
                    ++g_hasall_diag;
                    char msg[160];
                    std::snprintf(msg, sizeof(msg), "meditation-lend MISS #%u: tags=%d parents=%d (2H=%d 1H=%d meleeParent=%d padawan=%d)",
                                  g_hasall_diag, cont->tags.num, cont->parents.num,
                                  scan(cont->tags, g_melee_2h) ? 1 : 0, scan(cont->tags, g_melee_1h) ? 1 : 0,
                                  scan(cont->tags, g_melee_spec) ? 1 : 0, scan(cont->tags, g_exotic) ? 1 : 0);
                    logf(msg);
                }
            }
        }
        return g_hasall_orig(required, character);
    }

    // EXPERIMENTAL: SEH-guarded call into FindInventoryItem (POD-only body so SEH is legal here).
    std::uintptr_t g_fault_rva{}, g_fault_code{};
    auto med_seh_filter(unsigned long code, _EXCEPTION_POINTERS* ep) -> int
    {
        g_fault_code = code;
        g_fault_rva = ep && ep->ExceptionRecord ? reinterpret_cast<std::uintptr_t>(ep->ExceptionRecord->ExceptionAddress) - g_base : 0;
        return EXCEPTION_EXECUTE_HANDLER;
    }
    // EXPERIMENTAL: SEH-guarded call into ExecuteConsoleCommand (POD-only body so SEH is legal here).
    auto try_exec_console(ExecConsoleFn fn, void* ctx, const void* cmd) -> void
    {
        g_fault_rva = 0; g_fault_code = 0;
        __try { fn(ctx, cmd, nullptr); }
        __except (med_seh_filter(GetExceptionCode(), GetExceptionInformation())) {}
    }

    // EXPERIMENTAL: run one console command through the game's OWN dispatch, from a safe between-frames
    // moment (on_update). The command string is copied to a writable buffer (the dispatch may edit it).
    auto run_console(const wchar_t* cmd) -> void
    {
        if (g_world_ctx == nullptr) { logf("console: no world context yet"); return; }
        static wchar_t buf[256];
        std::size_t len = wcslen(cmd); if (len > 254) len = 254;
        std::memcpy(buf, cmd, (len + 1) * sizeof(wchar_t)); buf[len] = 0;
        const std::int32_t n = static_cast<std::int32_t>(len) + 1;
        FStringLite s{buf, n, static_cast<std::int32_t>(sizeof(buf) / sizeof(buf[0]))};
        auto exec = reinterpret_cast<ExecConsoleFn>(g_base + kExecConsoleRva);
        try_exec_console(exec, g_world_ctx, &s);
        char line[256];
        std::snprintf(line, sizeof(line), "console cmd=%ls sent (fault_code=0x%llx rva=0x%llx)", cmd, (unsigned long long)g_fault_code, (unsigned long long)g_fault_rva);
        logf(line);
    }

    auto bytes_match(std::uintptr_t rva, const std::uint8_t* want) -> bool
    {
        return rva + 16 <= kImageSize && std::memcmp(reinterpret_cast<const void*>(g_base + rva), want, 16) == 0;
    }

    // Overwrite a verified function's entry so it returns immediately (never runs its body).
    auto apply_ret_patch(std::uintptr_t rva, const std::uint8_t* sig, const std::uint8_t* patch, std::size_t n) -> bool
    {
        if (!bytes_match(rva, sig)) return false;
        auto* t = reinterpret_cast<std::uint8_t*>(g_base + rva);
        DWORD old{};
        if (!VirtualProtect(t, n, PAGE_EXECUTE_READWRITE, &old)) return false;
        std::memcpy(t, patch, n);
        VirtualProtect(t, n, old, &old);
        FlushInstructionCache(GetCurrentProcess(), t, n);
        return true;
    }

    auto has_suffix(const wchar_t* s, const wchar_t* suf) -> bool
    {
        std::size_t ls = 0; while (s[ls]) ++ls;
        std::size_t lf = 0; while (suf[lf]) ++lf;
        if (lf > ls) return false;
        for (std::size_t i = 0; i < lf; ++i) if (s[ls - lf + i] != suf[i]) return false;
        return true;
    }

    // Read the stock Clone helmet's HelmetFxRtpcValue via UE4SS reflection (cached after first success).
    auto resolve_voice_preset() -> int
    {
        if (g_voice_preset >= 0) return g_voice_preset;
        if (g_get_value_ptr == nullptr) return -1;
        void* src = reinterpret_cast<GetPartDefFn>(g_base + kGetPartDefFromIdRva)(&g_voice_src_id);
        if (src == nullptr || (g_is_real && !g_is_real(src))) return -1;
        auto* frags = reinterpret_cast<TArray<void*>*>(g_get_value_ptr(src, L"Fragments"));
        if (frags == nullptr || frags->data == nullptr) return -1;
        for (std::int32_t i = 0; i < frags->num; ++i)
        {
            void* frag = frags->data[i];
            if (frag == nullptr || (g_is_real && !g_is_real(frag))) continue;
            auto* v = reinterpret_cast<int*>(g_get_value_ptr(frag, L"HelmetFxRtpcValue"));
            if (v != nullptr && *v >= 0) { g_voice_preset = *v; return g_voice_preset; }
        }
        return -1;
    }

    auto id_is_voice_helmet(const FPrimaryAssetId& id) -> bool
    {
        for (std::size_t i = 0; i < kOfferCount; ++i)
            if (g_voice_helmet[i] && same_id(id, g_ids[i])) return true;
        return false;
    }

    // Hook of the voiceover-preset solver. Call the original; if it produced no preset (-1) and the
    // character has one of our Mando/Cly helmets equipped, inject the stock helmet-radio value.
    auto __fastcall hook_solve_voice(int* output, void* customization, const TArray<FGameplayTag>* slot_tags) -> void
    {
        if (g_solve_voice_orig == nullptr) return;
        g_solve_voice_orig(output, customization, slot_tags);
        if (output == nullptr || *output != -1 || customization == nullptr || slot_tags == nullptr) return;
        const auto get_slot = reinterpret_cast<GetSlotInstFn>(g_base + kGetSlotInstanceRva);
        for (std::int32_t i = 0; i < slot_tags->num; ++i)
        {
            const FGameplayTag& st = slot_tags->data[i];
            if (st.tag_name.comparison_index == 0) continue;   // None
            void* slot = get_slot(customization, &st);
            if (slot == nullptr || (g_is_real && !g_is_real(slot))) continue;
            FPrimaryAssetId sel{};
            std::memcpy(&sel, reinterpret_cast<std::uint8_t*>(slot) + kSlotPrimaryAssetIdOffset, sizeof(sel));
            if (!id_is_voice_helmet(sel)) continue;
            const int preset = resolve_voice_preset();
            if (preset >= 0) *output = preset;
            return;
        }
    }

    // ================= HELMET CLIPPING FIT (render-palette) ============================================
    // Ported from Sternab's ZeroCompanyMandoWardrobe v0.4.1 (MIT, (c) 2026 Sternab). After the game builds
    // a skeletal mesh's current/previous skinning palette we post-multiply every bone matrix by a small
    // scale around the animated head-bone pivot, pulling the Man001A/Man002A helmets off the face. Only the
    // exact helmet meshes are touched; all guards are Sternab's. To stay SDK-free (no cross-CRT std::vector /
    // std::wstring from FindAllOf/GetFullName), the target mesh + its "head" bone are found from the asset
    // itself: identify by the asset's FName (UObjectBase::NamePrivate @+0x18), and read the head bone from
    // its FReferenceSkeleton (asset->vtable[0x320]() -> refskel; count @+0x28; FMeshBoneInfo[] @+0x20,
    // 12-byte stride, FName @+0). Offsets extracted from GetBoneName/GetNumBones on this exact build.
    constexpr bool kEnableHelmetFit = true;
    constexpr std::uintptr_t kUpdateRefRva = 0x4EA2060;      // UpdateRefToLocalMatrices (current palette)
    constexpr std::uintptr_t kUpdatePrevRefRva = 0x4EA2270;  // UpdatePreviousRefToLocalMatrices
    constexpr std::size_t kGetRefSkelVtOff = 0x320;          // USkinnedAsset::GetRefSkeleton vtable slot
    constexpr std::size_t kObjNameOff = 0x18;                // UObjectBase::NamePrivate
    constexpr std::size_t kRefSkelArrOff = 0x20;             // FReferenceSkeleton -> FMeshBoneInfo*
    constexpr std::size_t kRefSkelNumOff = 0x28;             // FReferenceSkeleton -> bone count
    constexpr std::size_t kBoneInfoStride = 12;              // FMeshBoneInfo { FName; int32 }
    constexpr std::int32_t kMaxBoneCount = 768;
    constexpr double kFitScaleX = 1.06, kFitScaleY = 1.06, kFitScaleZ = 1.00;
    const std::uint8_t kUpdateRefBytes[16] = {0x48,0x8B,0xC4,0x48,0x89,0x58,0x10,0x48,0x89,0x70,0x18,0x57,0x41,0x54,0x41,0x55};
    // exact SkeletalMesh asset names that get the fit (female + male, Man001A + Man002A helmets)
    constexpr const wchar_t* kFitMeshNames[] = {
        STR("SK_HAAF_Man001A_HELM"), STR("SK_HAAM_Man001A_HELM"),
        STR("SK_HAAF_Man002A_HELM"), STR("SK_HAAM_Man002A_HELM"),
    };
    constexpr std::size_t kFitMeshCount = sizeof(kFitMeshNames) / sizeof(kFitMeshNames[0]);

    struct Vector3d { double x{}, y{}, z{}; };
    struct Quaternion4d { double x{}, y{}, z{}, w{1.0}; };
    struct alignas(16) Transform3d { Quaternion4d rotation{}; alignas(16) Vector3d translation{}; alignas(16) Vector3d scale{1.0, 1.0, 1.0}; };
    struct alignas(16) Matrix44f { float m[4][4]{}; };
    template <typename T> struct ArrayHeader { T* data{}; std::int32_t num{}; std::int32_t max{}; };
    struct ArrayViewRaw { const void* data{}; std::int32_t num{}; std::int32_t padding{}; };
    struct SharedPointerMirror { const void* object{}; const void* controller{}; };
    // FSkinnedMeshSceneProxyDynamicData mirror (build 24874058); only named fields are read.
    struct alignas(16) DynamicDataMirror {
        FName debug_name{};                        // 0x000
        const void* cloth_simulation_data{};       // 0x008
        const void* mesh_deformer_instances{};     // 0x010
        SharedPointerMirror ref_pose_override{};   // 0x018
        ArrayViewRaw external_morph_weights{};     // 0x028
        ArrayViewRaw component_space_transforms{}; // 0x038
        ArrayViewRaw previous_transforms{};        // 0x048
        ArrayViewRaw bone_visibility_states{};     // 0x058
        ArrayViewRaw previous_visibility_states{}; // 0x068
        ArrayViewRaw leader_bone_map{};            // 0x078
        ArrayViewRaw skin_cache_usage{};           // 0x088
        std::uint8_t padding_098[8]{};
        Transform3d component_world_transform{};   // 0x0A0
        std::uint32_t current_revision{};          // 0x100
        std::uint32_t previous_revision{};         // 0x104
        std::uint32_t bone_transform_revision{};   // 0x108
        std::uint16_t number_of_lods{};            // 0x10C
        std::uint8_t flags{};                      // 0x10E
        std::uint8_t padding_10f{};
    };
    static_assert(sizeof(DynamicDataMirror) == 0x110, "DynamicDataMirror layout changed");
    static_assert(offsetof(DynamicDataMirror, component_space_transforms) == 0x38 &&
                  offsetof(DynamicDataMirror, previous_transforms) == 0x48 &&
                  offsetof(DynamicDataMirror, leader_bone_map) == 0x78 &&
                  offsetof(DynamicDataMirror, flags) == 0x10E, "DynamicDataMirror offsets changed");
    constexpr std::uint8_t kHasLeaderPoseFlag = 1U << 0U;
    constexpr std::uint8_t kHasMeshDeformerFlag = 1U << 1U;
    enum class PaletteKind : std::uint8_t { Current, Previous };

    using UpdateRefFn = void(__fastcall*)(ArrayHeader<Matrix44f>*, const DynamicDataMirror*, const void*, const void*, std::int32_t, const ArrayHeader<std::uint16_t>*);
    using GetRefSkelFn = void*(__fastcall*)(const void*);
    UpdateRefFn g_update_ref_orig{};
    UpdateRefFn g_update_prev_orig{};
    FName g_fit_names[kFitMeshCount]{};
    FName g_head_name{};

    // Read the mesh's FReferenceSkeleton and find its unique "head" bone. Returns false if not resolvable.
    auto resolve_head_bone(const void* mesh, std::int32_t& head, std::int32_t& bones) -> bool
    {
        head = -1; bones = 0;
        if (mesh == nullptr) return false;
        auto vtbl = *reinterpret_cast<const std::uintptr_t*>(mesh);
        if (vtbl == 0) return false;
        auto get_ref = *reinterpret_cast<GetRefSkelFn*>(vtbl + kGetRefSkelVtOff);
        if (get_ref == nullptr) return false;
        void* refskel = get_ref(mesh);
        if (refskel == nullptr) return false;
        const std::int32_t count = *reinterpret_cast<std::int32_t*>(reinterpret_cast<std::uint8_t*>(refskel) + kRefSkelNumOff);
        auto* data = *reinterpret_cast<std::uint8_t**>(reinterpret_cast<std::uint8_t*>(refskel) + kRefSkelArrOff);
        if (count <= 0 || count > kMaxBoneCount || data == nullptr) return false;
        std::int32_t matches = 0, idx = -1;
        for (std::int32_t i = 0; i < count; ++i)
        {
            const FName bn = *reinterpret_cast<const FName*>(data + static_cast<std::size_t>(i) * kBoneInfoStride);
            if (bn.comparison_index == g_head_name.comparison_index) { ++matches; idx = i; }
        }
        if (matches != 1 || idx < 0) return false;
        head = idx; bones = count; return true;
    }

    struct FitTarget { std::int32_t head{-1}; std::int32_t bones{0}; bool found{false}; };

    // Is this asset one of the fit meshes? If so, resolve its head bone + bone count (immutable per mesh;
    // resolved inline each call - cheap, and avoids any shared cache / render-thread race).
    auto lookup_fit(const void* asset) -> FitTarget
    {
        if (asset == nullptr || g_head_name.comparison_index == 0) return {};
        // cheap FName gate first (fires per-frame for every mesh)
        const FName an = *reinterpret_cast<const FName*>(reinterpret_cast<const std::uint8_t*>(asset) + kObjNameOff);
        bool is_target = false;
        for (std::size_t i = 0; i < kFitMeshCount; ++i)
            if (g_fit_names[i].comparison_index != 0 && an.comparison_index == g_fit_names[i].comparison_index) { is_target = true; break; }
        if (!is_target) return {};
        std::int32_t head, bones;
        if (!resolve_head_bone(asset, head, bones)) return {};
        return FitTarget{head, bones, true};
    }

    auto finite_transform_for_pivot(const Transform3d& t, Quaternion4d& out) -> bool
    {
        const auto& q = t.rotation; const auto& p = t.translation;
        if (!std::isfinite(q.x) || !std::isfinite(q.y) || !std::isfinite(q.z) || !std::isfinite(q.w) ||
            !std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) ||
            std::abs(p.x) > 1000000.0 || std::abs(p.y) > 1000000.0 || std::abs(p.z) > 1000000.0) return false;
        const double n2 = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
        if (!std::isfinite(n2) || n2 < 0.90 || n2 > 1.10) return false;
        const double inv = 1.0 / std::sqrt(n2);
        out = Quaternion4d{q.x * inv, q.y * inv, q.z * inv, q.w * inv};
        return true;
    }

    auto make_head_pivot_scale(const Transform3d& head_transform, Matrix44f& adj) -> bool
    {
        Quaternion4d q{};
        if (!finite_transform_for_pivot(head_transform, q)) return false;
        const double x2 = q.x + q.x, y2 = q.y + q.y, z2 = q.z + q.z;
        const double xx2 = q.x * x2, yy2 = q.y * y2, zz2 = q.z * z2;
        const double yz2 = q.y * z2, wx2 = q.w * x2, xy2 = q.x * y2, wz2 = q.w * z2, xz2 = q.x * z2, wy2 = q.w * y2;
        const double rot[3][3]{
            {1.0 - (yy2 + zz2), xy2 + wz2, xz2 - wy2},
            {xy2 - wz2, 1.0 - (xx2 + zz2), yz2 + wx2},
            {xz2 + wy2, yz2 - wx2, 1.0 - (xx2 + yy2)},
        };
        const double scale[3]{kFitScaleX, kFitScaleY, kFitScaleZ};
        double lin[3][3]{};
        for (std::size_t r = 0; r < 3; ++r)
        {
            for (std::size_t c = 0; c < 3; ++c)
            {
                for (std::size_t a = 0; a < 3; ++a) lin[r][c] += rot[a][r] * scale[a] * rot[a][c];
                if (!std::isfinite(lin[r][c])) return false;
                adj.m[r][c] = static_cast<float>(lin[r][c]);
            }
            adj.m[r][3] = 0.0F;
        }
        const double pivot[3]{head_transform.translation.x, head_transform.translation.y, head_transform.translation.z};
        for (std::size_t c = 0; c < 3; ++c)
        {
            double tf = 0.0;
            for (std::size_t r = 0; r < 3; ++r) tf += pivot[r] * lin[r][c];
            const double tr = pivot[c] - tf;
            if (!std::isfinite(tr) || std::abs(tr) > 1000000.0) return false;
            adj.m[3][c] = static_cast<float>(tr);
        }
        adj.m[3][3] = 1.0F;
        return true;
    }

    auto post_multiply(Matrix44f& mat, const Matrix44f& adj) -> void
    {
        Matrix44f r{};
        for (std::size_t row = 0; row < 4; ++row)
            for (std::size_t col = 0; col < 4; ++col)
            {
                float v = 0.0F;
                for (std::size_t k = 0; k < 4; ++k) v += mat.m[row][k] * adj.m[k][col];
                r.m[row][col] = v;
            }
        mat = r;
    }

    auto apply_render_fit(ArrayHeader<Matrix44f>* output, const DynamicDataMirror* dd, const void* asset, PaletteKind kind) -> void
    {
        const FitTarget target = lookup_fit(asset);
        if (!target.found) return;
        if (output == nullptr || output->data == nullptr || output->num != target.bones ||
            output->max < output->num || output->num <= 0 || output->num > kMaxBoneCount || dd == nullptr) return;
        if ((dd->flags & kHasMeshDeformerFlag) != 0) return;
        const ArrayViewRaw& view = kind == PaletteKind::Current ? dd->component_space_transforms : dd->previous_transforms;
        if (view.data == nullptr || view.num <= 0 || view.num > kMaxBoneCount) return;
        std::int32_t ti = target.head;
        if ((dd->flags & kHasLeaderPoseFlag) != 0)
        {
            const auto& map = dd->leader_bone_map;
            if (map.data == nullptr || map.num != target.bones || target.head < 0 || target.head >= map.num) return;
            ti = static_cast<const std::int32_t*>(map.data)[target.head];
        }
        if (ti < 0 || ti >= view.num) return;
        const auto* transforms = static_cast<const Transform3d*>(view.data);
        Matrix44f adj{};
        if (!make_head_pivot_scale(transforms[ti], adj)) return;
        for (std::int32_t i = 0; i < output->num; ++i) post_multiply(output->data[i], adj);
    }

    auto __fastcall hook_update_ref(ArrayHeader<Matrix44f>* output, const DynamicDataMirror* dd, const void* asset,
                                    const void* render_data, std::int32_t lod, const ArrayHeader<std::uint16_t>* extra) -> void
    {
        if (g_update_ref_orig == nullptr) return;
        g_update_ref_orig(output, dd, asset, render_data, lod, extra);
        if (kEnableHelmetFit) apply_render_fit(output, dd, asset, PaletteKind::Current);
    }

    auto __fastcall hook_update_prev(ArrayHeader<Matrix44f>* output, const DynamicDataMirror* dd, const void* asset,
                                     const void* render_data, std::int32_t lod, const ArrayHeader<std::uint16_t>* extra) -> void
    {
        if (g_update_prev_orig == nullptr) return;
        g_update_prev_orig(output, dd, asset, render_data, lod, extra);
        if (kEnableHelmetFit) apply_render_fit(output, dd, asset, PaletteKind::Previous);
    }

    // Test one owned gameplay tag on a character via its owned-tag interface (char+0x638).
    auto char_owns(void* ch, const FGameplayTag& tag) -> bool
    {
        if (ch == nullptr || tag.tag_name.comparison_index == 0) return false;
        void* tag_if = reinterpret_cast<std::uint8_t*>(ch) + kCharTagIfOff;
        auto** if_vt = *reinterpret_cast<void***>(tag_if);
        if (if_vt == nullptr) return false;
        auto has_tag = reinterpret_cast<HasTagIfaceFn>(if_vt[3]);
        return has_tag != nullptr && has_tag(tag_if, tag.tag_name);
    }

    // NamePrivate (UObjectBase+0x18, comparison index + instance number) - identity check against
    // pointer reuse: a recycled address only matches if the object is the very same named instance.
    auto obj_name_ci(void* obj) -> std::uint64_t
    {
        return obj != nullptr ? *reinterpret_cast<std::uint64_t*>(reinterpret_cast<std::uint8_t*>(obj) + 0x18) : 0u;
    }

    // Swap one owned PrimaryWeaponType tag for another on the char's ASC tag-count container, using
    // the game's own updater + change notify. Drains every stacked count of `out` (the game may have
    // granted it more than once), grants `in` only if absent. Returns true if `out` was present.
    auto swap_pwt(void* ch, const FGameplayTag& out, const FGameplayTag& in) -> bool
    {
        auto* c = reinterpret_cast<std::uint8_t*>(ch);
        void* tag_if = c + kCharTagIfOff;
        auto** if_vt = *reinterpret_cast<void***>(tag_if);
        if (if_vt == nullptr) return false;
        auto has_tag = reinterpret_cast<HasTagIfaceFn>(if_vt[3]);
        if (has_tag == nullptr || !has_tag(tag_if, out.tag_name)) return false;
        void* provider = c + kCharAscOff;
        auto** pv_vt = *reinterpret_cast<void***>(provider);
        if (pv_vt == nullptr) return false;
        auto get_asc = reinterpret_cast<GetAscFn>(pv_vt[2]);
        if (get_asc == nullptr) return false;
        void* asc = get_asc(provider);
        if (asc == nullptr) return false;
        void* container = reinterpret_cast<std::uint8_t*>(asc) + kAscTagCountOff;
        const auto update_tag = reinterpret_cast<UpdateTagMapFn>(g_base + kUpdateTagMapRva);
        for (int i = 0; i < 4 && has_tag(tag_if, out.tag_name); ++i)
            update_tag(container, &out, -1);
        if (!has_tag(tag_if, in.tag_name))
            update_tag(container, &in, 1);
        auto** asc_vt = *reinterpret_cast<void***>(asc);
        if (asc_vt != nullptr)
        {
            auto notify = reinterpret_cast<TagNotifyFn>(*reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(asc_vt) + kAscBroadcastVt));
            if (notify != nullptr) { notify(asc, &out, 0); notify(asc, &in, 1); }
        }
        return true;
    }

    // Swap the pawn's presentation-binding category (C_BindingAlias "Alias Tags" container) using
    // the game's own container copy/add/destroy. Returns true only when it actually changed.
    auto set_binding_category(void* ch, const FGameplayTag& cat) -> bool
    {
        if (g_get_value_ptr == nullptr || cat.tag_name.comparison_index == 0) return false;
        void* pcomp = g_get_value_ptr(ch, L"C_BindingAlias");
        void* comp = pcomp ? *reinterpret_cast<void**>(pcomp) : nullptr;
        if (comp == nullptr) return false;
        auto* cont = reinterpret_cast<FGameplayTagContainer*>(g_get_value_ptr(comp, L"Alias Tags"));
        if (cont == nullptr || cont->tags.num < 0 || cont->tags.num > 16) return false;
        for (std::int32_t i = 0; i < cont->tags.num; ++i)
            if (cont->tags.data != nullptr &&
                cont->tags.data[i].tag_name.comparison_index == cat.tag_name.comparison_index)
                return false;   // already carrying the wanted category
        const auto add = reinterpret_cast<TagAddFn>(g_base + kTagAddRva);
        const auto copy = reinterpret_cast<TagCopyFn>(g_base + kTagCopyRva);
        const auto destroy = reinterpret_cast<TagDtorFn>(g_base + kTagDtorRva);
        FGameplayTagContainer fresh{};   // zeroed = empty container
        add(&fresh, &cat);               // AddTag also fills the parent-tag list
        destroy(cont);                   // free the old arrays...
        copy(cont, &fresh);              // ...and copy-construct the replacement in place
        destroy(&fresh);
        return true;
    }

    // Grant or remove a single owned tag on the char's ASC (loose count, game's updater + notify).
    // Used to lend Tel-Rea's bespoke class identity to dual-saber wielders: Tel-Rea himself owns
    // CharacterClass.Bespoke.Jedi.TelRea and his swings are clean; Hawks lacked it and his TelRea
    // montages glitched mid-swing (partial presentation match).
    auto set_owned_tag(void* ch, const FGameplayTag& tag, bool present) -> bool
    {
        if (ch == nullptr || tag.tag_name.comparison_index == 0) return false;
        auto* c = reinterpret_cast<std::uint8_t*>(ch);
        void* tag_if = c + kCharTagIfOff;
        auto** if_vt = *reinterpret_cast<void***>(tag_if);
        if (if_vt == nullptr) return false;
        auto has_tag = reinterpret_cast<HasTagIfaceFn>(if_vt[3]);
        if (has_tag == nullptr) return false;
        if (has_tag(tag_if, tag.tag_name) == present) return false;
        void* provider = c + kCharAscOff;
        auto** pv_vt = *reinterpret_cast<void***>(provider);
        if (pv_vt == nullptr) return false;
        auto get_asc = reinterpret_cast<GetAscFn>(pv_vt[2]);
        if (get_asc == nullptr) return false;
        void* asc = get_asc(provider);
        if (asc == nullptr) return false;
        void* container = reinterpret_cast<std::uint8_t*>(asc) + kAscTagCountOff;
        const auto update_tag = reinterpret_cast<UpdateTagMapFn>(g_base + kUpdateTagMapRva);
        if (present) update_tag(container, &tag, 1);
        else for (int i = 0; i < 4 && has_tag(tag_if, tag.tag_name); ++i) update_tag(container, &tag, -1);
        auto** asc_vt = *reinterpret_cast<void***>(asc);
        if (asc_vt != nullptr)
        {
            auto notify = reinterpret_cast<TagNotifyFn>(*reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(asc_vt) + kAscBroadcastVt));
            if (notify != nullptr) notify(asc, &tag, present ? 1 : 0);
        }
        return true;
    }

    // v2.19.0 BUILD 1: track a char we lent a deflect/assist Info.Name tag to (see g_lentname_ch
    // decl). Idempotent; ptr-reuse-guarded by NamePrivate.
    auto lentname_register(void* ch) -> void
    {
        if (ch == nullptr) return;
        const std::uint64_t nm = obj_name_ci(ch);
        std::size_t free_i = kPendingMax;
        for (std::size_t i = 0; i < kPendingMax; ++i)
        {
            if (g_lentname_ch[i] == ch && g_lentname_id[i] == nm) return;   // already tracked
            if (g_lentname_ch[i] == nullptr && free_i == kPendingMax) free_i = i;
        }
        if (free_i != kPendingMax) { g_lentname_ch[free_i] = ch; g_lentname_id[free_i] = nm; }
    }

    // v2.19.0 BUILD 1: THE CLASS-LOCK FIX. Called every ~0.5s while hub_now() is true. Strips both
    // lent hero Info.Name tags off every live registered char regardless of relink-queue membership,
    // closing the leak that let a lent tag ride into the armory and trip the ANY(Info.Name) spec
    // lock. Removes only tags the mod itself added (registry only holds chars we lent to), in the
    // hub, where the mission-only lend is meant to be off anyway - so it cannot regress deflect /
    // force-push (both are mission-side) or the roster filter.
    auto strip_lent_names_in_hub() -> void
    {
        for (std::size_t i = 0; i < kPendingMax; ++i)
        {
            void* ch = g_lentname_ch[i];
            if (ch == nullptr) continue;
            if ((g_is_real != nullptr && !g_is_real(ch)) || obj_name_ci(ch) != g_lentname_id[i])
            { g_lentname_ch[i] = nullptr; g_lentname_id[i] = 0; continue; }
            const bool a = set_owned_tag(ch, g_deflect_name, false);
            const bool t = set_owned_tag(ch, g_deflect_name_tel, false);
            if (a || t) logf("class-lock fix: stripped lent Info.Name tag in hub (was leaking to the spec lock)");
            // both gone now -> drop it; a fresh mission lend re-registers via lentname_register.
            if (!char_owns(ch, g_deflect_name) && !char_owns(ch, g_deflect_name_tel))
            { g_lentname_ch[i] = nullptr; g_lentname_id[i] = 0; }
        }
    }

    // Ensure a melee/saber character owns Animation.PrimaryWeaponType.Other (not Rifle).
    // Returns true if it actually flipped Rifle->Other now.
    auto flip_pwt(void* ch) -> bool
    {
        if (ch == nullptr || !char_owns(ch, g_melee_spec)) return false;   // not a melee/saber wielder
        return swap_pwt(ch, g_pwt_rifle, g_pwt_other);
    }

    // Restore the baked rifle identity after the saber comes off (only ever called for chars WE
    // flipped): drain our Other, re-grant Rifle, so blasters animate correctly again.
    auto flip_pwt_back(void* ch) -> bool
    {
        if (ch == nullptr || char_owns(ch, g_melee_spec)) return false;    // still wielding melee -> keep Other
        return swap_pwt(ch, g_pwt_other, g_pwt_rifle);
    }

    // Read the char's StanceAnimationSets array (reflection) and log its contents; report whether it
    // still holds the rifle set and whether any saber/melee set is present. The array is what the
    // game's own CacheStanceAnimationSetFromWeapon rebuilt from the owned tags - it tells us which
    // identity the re-link is about to re-install.
    auto check_stance_sets(void* ch, bool& has_rifle, bool& has_melee, bool& telrea_front, bool log_line = true) -> bool
    {
        has_rifle = false; has_melee = false; telrea_front = false;
        if (g_get_value_ptr == nullptr) return false;
        auto* arr = reinterpret_cast<TArray<void*>*>(g_get_value_ptr(ch, L"StanceAnimationSets"));
        if (arr == nullptr || arr->num < 0 || arr->num > 16 || (arr->num > 0 && arr->data == nullptr)) return false;
        char line[256];
        int off = std::snprintf(line, sizeof(line), "saber-stance: stance sets of %p (n=%d):", ch, arr->num);
        for (std::int32_t i = 0; i < arr->num; ++i)
        {
            void* set = arr->data[i];
            const auto ci = set != nullptr ? static_cast<std::uint32_t>(obj_name_ci(set)) : 0u;
            const char* label = nullptr;
            for (std::size_t k = 0; k < kSasCount; ++k)
                if (ci != 0 && ci == g_sas_names[k].comparison_index)
                {
                    label = kSasLabels[k];
                    if (k == 0) has_rifle = true;
                    if (k == 1 || k == 3 || k == 5 || k == 6) has_melee = true;
                    if (i == 0 && (k == 5 || k == 6)) telrea_front = true;   // SAS_TelRea(_Force) active
                    break;
                }
            if (off > 0 && off < static_cast<int>(sizeof(line)))
                off += label != nullptr ? std::snprintf(line + off, sizeof(line) - off, " %s", label)
                                        : std::snprintf(line + off, sizeof(line) - off, " ci=%08X", ci);
        }
        if (log_line) logf(line);
        return true;
    }

    // SEH-guarded archetype lookup / load / Cache invocation (POD-only bodies, med_seh pattern).
    // Any fault or thrown exception becomes a logged failure + graceful defer instead of abort.
    auto find_arch_guarded(const wchar_t* path) -> void*
    {
        if (g_static_find == nullptr) return nullptr;
        void* r = nullptr;
        g_fault_code = 0; g_fault_rva = 0;
        __try { r = g_static_find(nullptr, nullptr, path, false); }
        __except (med_seh_filter(GetExceptionCode(), GetExceptionInformation())) { r = nullptr; }
        return r;
    }
    auto load_arch_guarded(const wchar_t* path) -> void*
    {
        if (!g_load_arch_ok) return nullptr;
        void* r = nullptr;
        g_fault_code = 0; g_fault_rva = 0;
        __try
        {
            void* cls = reinterpret_cast<ZConstructFn>(g_base + kZConstructArchClassRva)();
            if (cls != nullptr)
                r = reinterpret_cast<StaticLoadObjFn>(g_base + kStaticLoadObjectRva)(
                        cls, nullptr, path, nullptr, 0u, nullptr, true, nullptr);
        }
        __except (med_seh_filter(GetExceptionCode(), GetExceptionInformation())) { r = nullptr; }
        return r;
    }
    auto load_sas_guarded(const wchar_t* path) -> void*   // same, but class = UStanceAnimationSet
    {
        if (!g_load_arch_ok) return nullptr;
        void* r = nullptr;
        g_fault_code = 0; g_fault_rva = 0;
        __try
        {
            void* cls = reinterpret_cast<ZConstructFn>(g_base + kZConstructSasClassRva)();
            if (cls != nullptr)
                r = reinterpret_cast<StaticLoadObjFn>(g_base + kStaticLoadObjectRva)(
                        cls, nullptr, path, nullptr, 0u, nullptr, true, nullptr);
        }
        __except (med_seh_filter(GetExceptionCode(), GetExceptionInformation())) { r = nullptr; }
        return r;
    }
    auto process_event_guarded(void* obj, void* fn, void* params) -> bool
    {
        g_fault_code = 0; g_fault_rva = 0;
        __try { g_process_event(obj, fn, params); }
        __except (med_seh_filter(GetExceptionCode(), GetExceptionInformation())) { return false; }
        return true;
    }

    // ---- 1.3 meditation grant: SEH-guarded wrappers around the game's own item/grant calls ----
    auto load_item_guarded(const wchar_t* path) -> void*   // class = UBrunoUtilityInventoryItem
    {
        if (!g_med_grant_ok) return nullptr;
        void* r = nullptr;
        g_fault_code = 0; g_fault_rva = 0;
        __try
        {
            void* cls = reinterpret_cast<ZConstructFn>(g_base + kZConstructUtilityItemRva)();
            if (cls != nullptr)
                r = reinterpret_cast<StaticLoadObjFn>(g_base + kStaticLoadObjectRva)(
                        cls, nullptr, path, nullptr, 0u, nullptr, true, nullptr);
        }
        __except (med_seh_filter(GetExceptionCode(), GetExceptionInformation())) { r = nullptr; }
        return r;
    }
    // v2.6.2: the world-context for the grant is the engine's GWorld global - always the
    // current world, never stale, no armory-capture needed (the captured subsystem stopped
    // resolving a world; GWorld cannot fail that way). Verified by its UWorld vtable per read.
    auto grant_world() -> void*
    {
        void* world = *reinterpret_cast<void* const*>(g_base + kGWorldRva);
        if (world == nullptr ||
            *reinterpret_cast<const std::uint64_t*>(world) != static_cast<std::uint64_t>(g_base + kUWorldVtableRva))
            return nullptr;
        // 1.3.1 hardening (public crash report 2026-08-30, mission-load AV with the grant armed):
        // never hand the grant's actor-walk (GetInventory = GetActorOfClass) a world that is
        // still loading - that walk over a half-built world is the proven v2.4.0 poison pattern.
        // Gate on the engine's own "world is live" test, replicated from UWorld::HasBegunPlay
        // (disasm 0x50886A0): bBegunPlay bit @ world+0x18D, PersistentLevel @ +0x30 non-null,
        // its Actors.Num @ level+0xA8 > 0. True while browsing the hub/armory (no starvation),
        // false during every load window.
        const auto* w = reinterpret_cast<const std::uint8_t*>(world);
        if ((w[0x18D] & 1u) == 0) return nullptr;
        const auto* level = *reinterpret_cast<const std::uint8_t* const*>(w + 0x30);
        if (level == nullptr || *reinterpret_cast<const std::int32_t*>(level + 0xA8) <= 0) return nullptr;
        return world;
    }

    auto hq_inventory_guarded(void* ctx) -> void*   // the HQ inventory actor; null anywhere but the hub
    {
        if (!g_med_grant_ok || ctx == nullptr) return nullptr;
        void* r = nullptr;
        g_fault_code = 0; g_fault_rva = 0;
        __try { r = reinterpret_cast<GetHqInvFn>(g_base + kGetHqInventoryRva)(ctx); }
        __except (med_seh_filter(GetExceptionCode(), GetExceptionInformation())) { r = nullptr; }
        if (g_fault_code != 0)
        {
            // A fault swallowed INSIDE GetActorOfClass leaks its actor-iterator delegate handle
            // in the world's OnActorSpawned list -> the NEXT GetActorOfClass (any BP) walks the
            // dangling entry and hard-crashes (the 2026-08-29 crash-on-load stacks). Never poke
            // a faulting path twice: kill the grant feature for the rest of the session.
            g_med_grant_ok = false;
            char msg[128];
            std::snprintf(msg, sizeof(msg), "meditations: GetInventory faulted (code=0x%llx rva=0x%llx) - grant DISABLED this session",
                          (unsigned long long)g_fault_code, (unsigned long long)g_fault_rva);
            logf(msg);
        }
        return r;
    }
    auto total_owned_guarded(void* inv, void* item) -> int   // -1 on fault
    {
        int r = -1;
        g_fault_code = 0; g_fault_rva = 0;
        __try { r = reinterpret_cast<GetTotalOwnedFn>(g_base + kGetTotalOwnedRva)(inv, item); }
        __except (med_seh_filter(GetExceptionCode(), GetExceptionInformation())) { r = -1; }
        return r;
    }
    auto grant_item_guarded(void* ctx, void* item) -> bool
    {
        g_fault_code = 0; g_fault_rva = 0;
        __try { reinterpret_cast<GrantItemFn>(g_base + kGrantItemRva)(ctx, item, 1); }
        __except (med_seh_filter(GetExceptionCode(), GetExceptionInformation())) { return false; }
        return true;
    }

    // Is the current world the hub/Den? v2.18.2: ask the game's PHASE subsystem (see the
    // kIsTacticalPhaseRva block - the old HQ-inventory probe said "hub" during missions too
    // and silently killed every mission-only feature from v2.14.1 on). Tactical phase =
    // mission -> false; Headquarters phase -> true; any other phase (shell/menu/loading)
    // -> true = conservative withhold, same behavior the old probe gave outside missions.
    bool g_phase_ok{};   // both phase natives byte-verified at init
    auto hub_now() -> bool
    {
        void* w = grant_world();
        if (w == nullptr) return true;   // menus/loading: withhold (old probe also said not-mission)
        if (g_phase_ok)
        {
            bool tactical = false;
            bool faulted = false;
            g_fault_code = 0; g_fault_rva = 0;
            __try { tactical = reinterpret_cast<PhaseFn>(g_base + kIsTacticalPhaseRva)(w); }
            __except (med_seh_filter(GetExceptionCode(), GetExceptionInformation())) { faulted = true; }
            if (faulted)
            {
                g_phase_ok = false;   // never poke a faulting path twice
                logf("phase: IsTacticalPhase faulted - falling back to the HQ-inventory probe");
            }
            else
                return !tactical;
        }
        return hq_inventory_guarded(w) != nullptr;   // legacy fallback (phase natives unverified)
    }

    // Build an FText from a literal with the game's own Conv_StringToText and move it into an
    // FText UPROPERTY on `obj` (see the kImportNames block). ProcessEvent copies the return
    // FText into our POD param block; the memcpy transfers ownership to the property and the
    // block is never destructed, so the intrusive refcount stays correct.
    // âš  FText is EXACTLY 0x10 bytes: {ITextData* TextData @+0 (TRefCountPtr, intrusive),
    // uint32 Flags @+8, pad} - disasm-proven via the fragment's own FText getter (0x6317540:
    // copies one qword + one dword, AddRef vtbl+8 / Release vtbl+0x10). v2.16.1 copied 0x18
    // and smashed the first 8 bytes of the NEXT property - for the rename-only entries that
    // left a NULL TextData in an adjacent FText = Alex's change-weapon menu crash
    // (SetFieldNotifyValue AV at 0x0 under CustomizationPartViewModel::InitializeViewModel).
    auto write_text_prop(void* lib, void* conv, void* obj, const wchar_t* prop, const wchar_t* text) -> bool
    {
        void* dst = g_get_value_ptr != nullptr ? g_get_value_ptr(obj, prop) : nullptr;
        if (dst == nullptr) return false;
        struct ConvParams
        {
            const wchar_t* data; std::int32_t num; std::int32_t max;   // FString InString @0x00
            void* ret_text;                                            // FText.TextData    @0x10
            std::uint32_t ret_flags;                                   // FText.Flags       @0x18
            std::uint32_t pad;
        } p{};
        static_assert(sizeof(ConvParams) == 0x20, "Conv_StringToText param block layout");
        p.data = text;
        p.num = static_cast<std::int32_t>(::wcslen(text)) + 1;
        p.max = p.num;
        if (!process_event_guarded(lib, conv, &p)) return false;
        if (p.ret_text == nullptr) { logf("import-names: DIAG conversion returned a null FText - write skipped"); return false; }
        std::memcpy(dst, &p.ret_text, 0x10);   // exactly sizeof(FText) - NEVER more
        return true;
    }

    // One settled-hub pass: give every imported enemy part its armory display name.
    auto apply_import_names() -> void
    {
        if (g_process_event == nullptr || g_get_func == nullptr || g_get_value_ptr == nullptr) return;
        void* lib = find_arch_guarded(STR("/Script/Engine.Default__KismetTextLibrary"));
        if (lib == nullptr) return;
        void* conv = nullptr;
        {
            g_fault_code = 0; g_fault_rva = 0;
            __try { conv = g_get_func(lib, STR("Conv_StringToText")); }
            __except (med_seh_filter(GetExceptionCode(), GetExceptionInformation())) { conv = nullptr; }
        }
        if (conv == nullptr) return;
        for (std::size_t i = 0; i < kImportNameCount; ++i)
        {
            void* cpd = find_arch_guarded(kImportNames[i].path);
            if (cpd == nullptr) continue;   // loads once the armory enumerates the part; retry later
            if (g_import_named[i] && g_import_cpd[i] == cpd) continue;   // done and not reloaded
            auto* frags = reinterpret_cast<TArray<void*>*>(g_get_value_ptr(cpd, L"Fragments"));
            if (frags == nullptr || frags->num <= 0 || frags->num > 64 || frags->data == nullptr) continue;
            void* uifrag = nullptr;
            for (std::int32_t j = 0; j < frags->num; ++j)
            {
                void* f = frags->data[j];
                if (f == nullptr) continue;
                void* cls = *reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(f) + 0x10);   // ClassPrivate
                const std::uint64_t want = static_cast<std::uint64_t>(g_uifrag_class.comparison_index) |
                                           (static_cast<std::uint64_t>(g_uifrag_class.number) << 32);
                if (cls != nullptr && obj_name_ci(cls) == want) { uifrag = f; break; }
            }
            if (uifrag == nullptr) { g_import_named[i] = true; g_import_cpd[i] = cpd; continue; }   // nothing to name
            const bool named = write_text_prop(lib, conv, uifrag, L"DisplayName", kImportNames[i].name);
            write_text_prop(lib, conv, uifrag, L"MarkedUpDisplayName", kImportNames[i].name);
            if (kImportNames[i].desc != nullptr)   // null = rename-only, keep authored descriptions
            {
                write_text_prop(lib, conv, uifrag, L"ShortDescription", kImportNames[i].desc);
                write_text_prop(lib, conv, uifrag, L"LongDescription", kImportNames[i].desc);
            }
            if (kImportNames[i].icon != nullptr)
            {   // FBitReactorImageReference: Mode byte @+0 (0 = image-bank), FGameplayTag @+4
                const FName bank = make_fname(kImportNames[i].icon, AddName);
                for (const wchar_t* imgprop : { L"SmallImage", L"LargeImage" })
                {
                    auto* img = reinterpret_cast<std::uint8_t*>(g_get_value_ptr(uifrag, imgprop));
                    if (img == nullptr) continue;
                    img[0] = 0;
                    std::memcpy(img + 4, &bank, 8);
                }
            }
            if (named)
            {
                g_import_named[i] = true;
                g_import_cpd[i] = cpd;
                char msg[128];
                std::snprintf(msg, sizeof(msg), "import-names: display name written for entry %zu", i);
                logf(msg);
            }
        }
    }

    // v2.16.1: lend Item.UIType.Lightsaber to the vibrosword gear items (hologram preview +
    // category icons - see the kEnableVibroUiType block). Game's own AddTag = idempotent, so
    // this runs every pass and self-heals asset reloads.
    auto apply_vibro_uitype() -> void
    {
        if (g_get_value_ptr == nullptr) return;
        if (g_lightsaber.tag_name.comparison_index == 0)
            g_lightsaber = FGameplayTag{make_fname(kLightsaberTag, AddName)};   // lazily upgrade the FindName miss
        void* gk = find_arch_guarded(kVibroGkPath);
        if (gk == nullptr) return;
        auto* items = reinterpret_cast<TArray<void*>*>(g_get_value_ptr(gk, L"GearItems"));
        if (items == nullptr || items->num <= 0 || items->num > 16 || items->data == nullptr) return;
        const auto add = reinterpret_cast<TagAddFn>(g_base + kTagAddRva);
        for (std::int32_t j = 0; j < items->num; ++j)
        {
            void* item = items->data[j];
            if (item == nullptr) continue;
            auto* tags = reinterpret_cast<FGameplayTagContainer*>(g_get_value_ptr(item, L"GameplayTags"));
            if (tags == nullptr) continue;
            add(tags, &g_lightsaber);
            if (!g_vibro_uitype_logged)
            {
                logf("vibro-fix: Item.UIType.Lightsaber lent to gear items (hologram/icons)");
                g_vibro_uitype_logged = true;
            }
        }
    }

    // v2.17.0: precompute the import FTexts once so the read-time hook only does POD work.
    // Each stored {ITextData*, flags} pair holds ONE owning reference forever (by design).
    auto precompute_import_texts() -> void
    {
        if (g_texts_ready || g_process_event == nullptr || g_get_func == nullptr) return;
        void* lib = find_arch_guarded(STR("/Script/Engine.Default__KismetTextLibrary"));
        if (lib == nullptr) return;
        void* conv = nullptr;
        g_fault_code = 0; g_fault_rva = 0;
        __try { conv = g_get_func(lib, STR("Conv_StringToText")); }
        __except (med_seh_filter(GetExceptionCode(), GetExceptionInformation())) { conv = nullptr; }
        if (conv == nullptr) return;
        bool all_ok = true;
        for (std::size_t i = 0; i < kImportNameCount; ++i)
        {
            const wchar_t* texts[2] = { kImportNames[i].name, kImportNames[i].desc };
            for (int s = 0; s < 2; ++s)
            {
                if (texts[s] == nullptr || g_imp_text[i][s] != nullptr) continue;
                struct ConvParams
                {
                    const wchar_t* data; std::int32_t num; std::int32_t max;
                    void* ret_text; std::uint32_t ret_flags; std::uint32_t pad;
                } p{};
                p.data = texts[s];
                p.num = static_cast<std::int32_t>(::wcslen(texts[s])) + 1;
                p.max = p.num;
                if (!process_event_guarded(lib, conv, &p) || p.ret_text == nullptr) { all_ok = false; continue; }
                g_imp_text[i][s] = p.ret_text;          // our stored owning reference
                g_imp_flags[i][s] = p.ret_flags;
            }
        }
        if (all_ok)
        {
            g_texts_ready = true;
            logf("import-names: texts precomputed (read-time hook armed)");
        }
    }

    // Install one precomputed FText onto a fragment property: AddRef the shared ITextData for
    // the new holder (vtbl+8, per the 0x6317540 getter disasm), then write {ptr, flags}.
    auto install_import_text(void* frag, const wchar_t* prop, std::size_t i, int slot) -> void
    {
        void* data = g_imp_text[i][slot];
        if (data == nullptr || g_get_value_ptr == nullptr) return;
        auto* dst = reinterpret_cast<std::uint8_t*>(g_get_value_ptr(frag, prop));
        if (dst == nullptr) return;
        __try
        {
            auto** vt = *reinterpret_cast<void***>(data);
            reinterpret_cast<void(__fastcall*)(void*)>(vt[1])(data);   // ITextData::AddRef
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return; }
        std::memcpy(dst, &data, 8);
        std::memcpy(dst + 8, &g_imp_flags[i][slot], 4);
    }

    // The read-time hook: whenever anything asks a part definition for its UIData fragment,
    // dress the fragment first if the part is one of ours. Runs at menu-build frequency.
    auto __fastcall hook_findfrag_uidata(void* def, std::uint8_t b) -> void*
    {
        void* r = g_findfrag_orig != nullptr ? g_findfrag_orig(def, b) : nullptr;
        if (r == nullptr || !g_texts_ready || def == nullptr) return r;
        const std::uint64_t nm = obj_name_ci(def);
        for (std::size_t i = 0; i < kImportNameCount; ++i)
        {
            if (g_imp_target[i] != nm || g_imp_frag_done[i] == r) continue;
            install_import_text(r, L"DisplayName", i, 0);
            install_import_text(r, L"MarkedUpDisplayName", i, 0);
            if (g_imp_text[i][1] != nullptr)
            {
                install_import_text(r, L"ShortDescription", i, 1);
                install_import_text(r, L"LongDescription", i, 1);
            }
            if (g_imp_icon[i] != 0 && g_get_value_ptr != nullptr)
                for (const wchar_t* imgprop : { L"SmallImage", L"LargeImage" })
                {
                    auto* img = reinterpret_cast<std::uint8_t*>(g_get_value_ptr(r, imgprop));
                    if (img == nullptr) continue;
                    img[0] = 0;                               // mode 0 = image-bank tag
                    std::memcpy(img + 4, &g_imp_icon[i], 8);
                }
            g_imp_frag_done[i] = r;
            break;
        }
        return r;
    }

    // v2.16.0 A: Anakin saber attributes := Tel's Standard set (see the kEnableRuntimeBalance
    // block). Find-only + float writes on loaded assets; idempotent; re-heals if the assets
    // get collected and reloaded (memcmp keeps re-runs silent).
    auto apply_runtime_balance() -> void
    {
        if (g_get_value_ptr == nullptr) return;
        void* src = find_arch_guarded(kAttrStandardPath);
        void* dst = find_arch_guarded(kAttrAnakinPath);
        if (src == nullptr || dst == nullptr) return;   // not loaded yet - retry next cadence
        int changed = 0;
        for (std::size_t i = 0; i < kMeleeAttrPropCount; ++i)
        {
            auto* s = reinterpret_cast<std::uint8_t*>(g_get_value_ptr(src, kMeleeAttrProps[i]));
            auto* d = reinterpret_cast<std::uint8_t*>(g_get_value_ptr(dst, kMeleeAttrProps[i]));
            if (s == nullptr || d == nullptr) continue;
            if (std::memcmp(d + 8, s + 8, 8) != 0) { std::memcpy(d + 8, s + 8, 8); ++changed; }   // Base @+8, Current @+0xC
        }
        if (changed != 0)
        {
            char msg[128];
            std::snprintf(msg, sizeof(msg), "runtime-balance: Anakin saber matched to Standard (%d attribute(s))", changed);
            logf(msg);
        }
        if (!g_balance_done)
        {   // one-time per session: prove the live asset values in the log (base/current), so a
            // "stats look wrong" report can be split into asset-state vs equip-snapshot residue
            auto dmg = [](void* a) -> const float* {
                return reinterpret_cast<const float*>(reinterpret_cast<std::uint8_t*>(g_get_value_ptr(a, L"Damage")));
            };
            const float* sd = dmg(src);
            const float* ad = dmg(dst);
            if (sd != nullptr && ad != nullptr)
            {
                char msg[160];
                std::snprintf(msg, sizeof(msg), "runtime-balance: verify Standard dmg=%.1f/%.1f Anakin dmg=%.1f/%.1f",
                              sd[2], sd[3], ad[2], ad[3]);   // FGameplayAttributeData floats @+8/+0xC
                logf(msg);
            }
        }
        g_balance_done = true;
    }

    // v2.16.0 B: BP_Vibroblade CDO archetype -> SASArch_1HMelee. The FSoftObjectPtr's FName
    // pair is located by scanning the property bytes for the current (rifle) pair, so the
    // TPersistentObjectPtr weak-ptr prefix never has to be assumed; the 8 bytes before the
    // pair are reset to an invalid FWeakObjectPtr so the cached resolve can't serve stale.
    auto apply_vibro_arch_fix() -> void
    {
        if (g_get_value_ptr == nullptr) return;
        void* cdo = find_arch_guarded(kVibroCdoPath);
        if (cdo == nullptr) return;   // loads with the armory tile / a vibro spawn - retry
        auto* prop = reinterpret_cast<std::uint8_t*>(g_get_value_ptr(cdo, L"StanceAnimationArchetypeAsset"));
        if (prop == nullptr) { g_vibro_cdo_done = true; return; }   // property gone - never retry-spam
        const FName rifle_pkg = make_fname(kRifleArchPkg, FindName);
        const FName rifle_nm  = make_fname(kRifleArchName, FindName);
        const FName melee_pkg = make_fname(kMeleeArchPkg, AddName);
        const FName melee_nm  = make_fname(kMeleeArchName, AddName);
        auto qw = [](const FName& n) {
            return static_cast<std::uint64_t>(n.comparison_index) | (static_cast<std::uint64_t>(n.number) << 32); };
        for (std::size_t off = 0; off <= 0x18; off += 4)
        {
            auto* p = reinterpret_cast<std::uint64_t*>(prop + off);
            if (rifle_pkg.comparison_index != 0 && p[0] == qw(rifle_pkg) && p[1] == qw(rifle_nm))
            {
                p[0] = qw(melee_pkg); p[1] = qw(melee_nm);
                if (off >= 8)   // invalidate the cached weak ptr that precedes the path
                { auto* wp = reinterpret_cast<std::int32_t*>(prop + off - 8); wp[0] = -1; wp[1] = 0; }
                logf("vibro-fix: BP_Vibroblade stance archetype -> SASArch_1HMelee (CDO)");
                g_vibro_cdo_done = true;
                return;
            }
            if (p[0] == qw(melee_pkg) && p[1] == qw(melee_nm)) { g_vibro_cdo_done = true; return; }   // already (pak or us)
        }
    }

    // v2.16.0 B heal: a vibro wielder whose spawn cached the stance before the CDO write got
    // the rifle archetype - re-run the game's own stance cache with the melee archetype
    // (ProcessEvent, the v1.8.1-proven recipe). Settled windows only.
    auto fix_vibro_stance(void* ch) -> bool
    {
        if (ch == nullptr || g_process_event == nullptr || g_get_func == nullptr) return false;
        if (g_is_real != nullptr && !g_is_real(ch)) return true;   // gone - drop the entry
        apply_vibro_arch_fix();
        void* arch = find_arch_guarded(kMeleeArchAsset);
        if (arch == nullptr) arch = load_arch_guarded(kMeleeArchAsset);
        if (arch == nullptr) return false;
        void* fn = nullptr;
        g_fault_code = 0; g_fault_rva = 0;
        __try { fn = g_get_func(ch, STR("CacheStanceAnimationSetFromWeapon")); }
        __except (med_seh_filter(GetExceptionCode(), GetExceptionInformation())) { fn = nullptr; }
        if (fn == nullptr) return false;
        struct { void* arch; bool badd; } params{ arch, true };
        g_in_stance_fix = true;
        const bool ok = process_event_guarded(ch, fn, &params);
        g_in_stance_fix = false;
        if (ok) logf("vibro-fix: melee stance re-cached on spawned wielder");
        return ok;
    }

    // ---- v2.18.0 assist-push: swap the class teamwork assist SHOT for the Jedi force-push
    // variant on custom Jedi (see the kEnableAssistPush block for the full recon story). ----
    void* g_assist_ch[kPendingMax]{};                 // custom Jedi currently holding the push package
    std::uint64_t g_assist_name[kPendingMax]{};       // their NamePrivate (ptr-reuse guard)
    bool g_assist_two_h[kPendingMax]{};               // lane granted (true = Padawan/TelRea, false = Anakin)
    std::uint64_t g_assist_handles[kPendingMax][6]{}; // FBitReactorAbilitySet_GrantedHandles (3 TArrays = 0x30)
    bool g_assist_ok{};                               // feature armed (exports + tags verified)
    bool g_assist_load_ok{};                          // StaticLoadObject + UBitReactorAbilitySet construct verified
    bool g_assist_defer_logged{};                     // one-time "deferring" log guard
    bool g_fname_tostr_ok{};                          // FName::ToString RVA byte-verified (diag dumps)
    std::uint64_t g_assist_absig[kPendingMax]{};      // last-seen granted-ability signature per slot
    using FNameToStrFn = void(__fastcall*)(const void* name, FStringLite* out);

    // The char's ASC via its embedded provider interface (same access pattern as swap_pwt).
    auto asc_of(void* ch) -> void*
    {
        if (ch == nullptr) return nullptr;
        void* provider = reinterpret_cast<std::uint8_t*>(ch) + kCharAscOff;
        auto** pv_vt = *reinterpret_cast<void***>(provider);
        if (pv_vt == nullptr) return nullptr;
        auto get_asc = reinterpret_cast<GetAscFn>(pv_vt[2]);
        return get_asc != nullptr ? get_asc(provider) : nullptr;
    }

    auto load_ability_set_guarded(const wchar_t* path) -> void*   // class = UBitReactorAbilitySet
    {
        if (!g_assist_load_ok) return nullptr;
        void* r = nullptr;
        g_fault_code = 0; g_fault_rva = 0;
        __try
        {
            void* cls = reinterpret_cast<ZConstructFn>(g_base + kZConstructAbilitySetRva)();
            if (cls != nullptr)
                r = reinterpret_cast<StaticLoadObjFn>(g_base + kStaticLoadObjectRva)(
                        cls, nullptr, path, nullptr, 0u, nullptr, true, nullptr);
        }
        __except (med_seh_filter(GetExceptionCode(), GetExceptionInformation())) { r = nullptr; }
        return r;
    }

    // Resolve `fn_name` on the CDO at `cdo_path` (SEH-guarded). Returns the UFunction; CDO out-param.
    auto cdo_func_guarded(const wchar_t* cdo_path, const wchar_t* fn_name, void*& cdo) -> void*
    {
        cdo = find_arch_guarded(cdo_path);
        if (cdo == nullptr || g_get_func == nullptr) return nullptr;
        void* fn = nullptr;
        g_fault_code = 0; g_fault_rva = 0;
        __try { fn = g_get_func(cdo, fn_name); }
        __except (med_seh_filter(GetExceptionCode(), GetExceptionInformation())) { fn = nullptr; }
        return fn;
    }

    // Does the ASC currently hold an ability of `cls`? (FindAbilityHandleByClass; invalid handle = -1.)
    auto asc_has_ability(void* asc, void* cls) -> bool
    {
        if (asc == nullptr || cls == nullptr) return false;
        void* cdo = nullptr;
        void* fn = cdo_func_guarded(kAbilityFnsCdo, STR("FindAbilityHandleByClass"), cdo);
        if (fn == nullptr) return false;
        struct { void* asc; void* cls; std::int32_t ret; std::int32_t pad; } p{ asc, cls, -1, 0 };
        if (!process_event_guarded(cdo, fn, &p)) return false;
        return p.ret != -1;
    }

    auto assist_slot_of(void* ch) -> std::size_t
    {
        for (std::size_t i = 0; i < kPendingMax; ++i)
            if (g_assist_ch[i] == ch && g_assist_ch[i] != nullptr && obj_name_ci(ch) == g_assist_name[i])
                return i;
        return kPendingMax;
    }

    // Drop slots whose pawn is gone or recycled (its ASC died with it; the stored handle arrays
    // leak - three tiny allocations per mission pawn, accepted like the import-names FText leak).
    auto assist_prune() -> void
    {
        for (std::size_t i = 0; i < kPendingMax; ++i)
            if (g_assist_ch[i] != nullptr &&
                ((g_is_real != nullptr && !g_is_real(g_assist_ch[i])) ||
                 obj_name_ci(g_assist_ch[i]) != g_assist_name[i]))
            {
                g_assist_ch[i] = nullptr; g_assist_name[i] = 0; g_assist_two_h[i] = false;
                std::memset(g_assist_handles[i], 0, sizeof(g_assist_handles[i]));
            }
    }

    // Revert one char: remove our push package via the stored grant handles, then restore the
    // class-native standard assist shot so the char leaves in vanilla state.
    auto revert_assist_push(void* ch) -> void
    {
        if (!g_assist_ok) return;
        const std::size_t i = assist_slot_of(ch);
        if (i == kPendingMax) return;
        void* asc = asc_of(ch);
        void* statics = nullptr;
        void* fn_rem = cdo_func_guarded(kGameStaticsCdo, STR("RemoveAbilitySetWithHandles"), statics);
        if (asc != nullptr && fn_rem != nullptr)
        {
            struct { void* asc; std::uint64_t handles[6]; } p{ asc, {} };
            std::memcpy(p.handles, g_assist_handles[i], sizeof(p.handles));
            process_event_guarded(statics, fn_rem, &p);
            void* std_cls = find_arch_guarded(kAssistStdGaPath);
            void* std_set = find_arch_guarded(kAssistSetStdPath);   // resident: every hero class references it
            if (std_cls != nullptr && std_set != nullptr && !asc_has_ability(asc, std_cls))
            {
                void* fn_grant = cdo_func_guarded(kGameStaticsCdo, STR("GrantAbilitySet"), statics);
                if (fn_grant != nullptr)
                {
                    struct { void* asc; void* set; std::uint64_t handles[6]; } gp{ asc, std_set, {} };
                    process_event_guarded(statics, fn_grant, &gp);   // handles dropped = native-equivalent
                }
            }
            logf("assist-push: reverted (standard assist restored)");
        }
        g_assist_ch[i] = nullptr; g_assist_name[i] = 0; g_assist_two_h[i] = false;
        std::memset(g_assist_handles[i], 0, sizeof(g_assist_handles[i]));
    }

    // Apply to one settled melee wielder (mission-only). v2.18.1: the custom-Jedi lane is
    // "saber wielder" = owns Weapon.Melee but NOT Weapon.Enemy.1HMelee (vibro clones keep
    // their shot). The v2.18.0 g_exotic gate was WRONG: Hawks-style customs run class
    // Class.Default + saber and never own Class.Exotic.Padawan (that's the whole reason the
    // meditation HasAll lend matches the melee tags) - the gate silently skipped everyone.
    auto apply_assist_push(void* ch) -> void
    {
        if (!g_assist_ok || ch == nullptr) return;
        if (!char_owns(ch, g_melee_spec) || char_owns(ch, g_enemy_1h)) return;
        assist_prune();
        const bool two_h = char_owns(ch, g_melee_2h);
        std::size_t slot = assist_slot_of(ch);
        if (slot != kPendingMax && g_assist_two_h[slot] == two_h) return;   // already holding the right lane
        if (slot != kPendingMax) revert_assist_push(ch);                    // lane changed mid-session
        void* asc = asc_of(ch);
        if (asc == nullptr) return;
        slot = kPendingMax;
        for (std::size_t i = 0; i < kPendingMax; ++i)
            if (g_assist_ch[i] == nullptr) { slot = i; break; }
        if (slot == kPendingMax) return;   // registry full - stay vanilla so every grant stays revertible
        // v2.18.3: strip BOTH ranged assist shots FIRST, VERIFIED before/after (the doctrine
        // lesson - no silent failure paths): the class package's GA_Backup_TeamworkAssistShot
        // AND the sidearm pistol spec's GA_TeamworkAssistShot (Common) - the recruit keeps a
        // holdout-pistol weapon spec alongside the saber and ITS shot is the actual invisible
        // pistol that outran the push in the 2.18.2 live test. RemoveAbilityByClass = exact
        // ClassPrivate match over ActivatableAbilities (@ASC+0x530, stride 0xF8) +
        // ClearAbility by handle (exec disasm 0x6c62a70). Runs before the push-present
        // early-out so a char granted earlier in the session still gets the pistol stripped.
        void* fns_cdo = nullptr;
        void* fn_rm_cls = cdo_func_guarded(kAbilityFnsCdo, STR("RemoveAbilityByClass"), fns_cdo);
        if (fn_rm_cls == nullptr) { logf("assist-push: RemoveAbilityByClass not resolvable"); return; }
        void* shot_cls[3] = { find_arch_guarded(kAssistStdGaPath), find_arch_guarded(kAssistCommonGaPath),
                              find_arch_guarded(kAssistBondGaPath) };
        int before[3] = { -1, -1, -1 }, after[3] = { -1, -1, -1 };   // -1 = class not resident (never granted)
        for (int k = 0; k < 3; ++k)
        {
            if (shot_cls[k] == nullptr) continue;
            before[k] = asc_has_ability(asc, shot_cls[k]) ? 1 : 0;
            if (before[k] == 1)
            {
                struct { void* asc; void* cls; } rp{ asc, shot_cls[k] };
                process_event_guarded(fns_cdo, fn_rm_cls, &rp);
            }
            after[k] = asc_has_ability(asc, shot_cls[k]) ? 1 : 0;
        }
        if (after[0] == 1 || after[1] == 1 || after[2] == 1)
        {
            char msg[160];
            std::snprintf(msg, sizeof(msg), "assist-push: WARN shot removal incomplete std=%d->%d common=%d->%d bond=%d->%d",
                          before[0], after[0], before[1], after[1], before[2], after[2]);
            logf(msg);
        }
        const bool use_tel = two_h && !kAssistAnakinBothLanes;
        void* push_cls = find_arch_guarded(use_tel ? kAssistPushGaTel : kAssistPushGaAna);
        if (push_cls != nullptr && asc_has_ability(asc, push_cls))
        {   // push already granted this session (slot lost to a prune, or an earlier partial
            // pass) - shots are now stripped above; nothing left to grant
            if (before[0] == 1 || before[1] == 1 || before[2] == 1)
            {
                char msg[160];
                std::snprintf(msg, sizeof(msg), "assist-push: shots stripped on already-granted char std=%d->%d common=%d->%d bond=%d->%d",
                              before[0], after[0], before[1], after[1], before[2], after[2]);
                logf(msg);
            }
            return;
        }
        void* set = find_arch_guarded(use_tel ? kAssistSetTelPath : kAssistSetAnaPath);
        if (set == nullptr) set = load_ability_set_guarded(use_tel ? kAssistSetTelPath : kAssistSetAnaPath);
        if (set == nullptr)
        {
            if (!g_assist_defer_logged) { logf("assist-push: teamwork package not resolvable yet - deferring"); g_assist_defer_logged = true; }
            return;
        }
        void* statics = nullptr;
        void* fn_grant = cdo_func_guarded(kGameStaticsCdo, STR("GrantAbilitySet"), statics);
        if (fn_grant == nullptr) { logf("assist-push: GrantAbilitySet not resolvable"); return; }
        struct { void* asc; void* set; std::uint64_t handles[6]; } gp{ asc, set, {} };
        if (!process_event_guarded(statics, fn_grant, &gp))
        {
            logf("assist-push: grant faulted - char left without assist until next settled pass");
            return;
        }
        g_assist_ch[slot] = ch; g_assist_name[slot] = obj_name_ci(ch); g_assist_two_h[slot] = two_h;
        std::memcpy(g_assist_handles[slot], gp.handles, sizeof(gp.handles));
        const int granted_abilities = static_cast<int>(gp.handles[1] & 0xFFFFFFFFu);   // TArray num
        const int push_present = push_cls != nullptr ? (asc_has_ability(asc, push_cls) ? 1 : 0)
                                                     : (asc_has_ability(asc, find_arch_guarded(use_tel ? kAssistPushGaTel : kAssistPushGaAna)) ? 1 : 0);
        char msg[224];
        std::snprintf(msg, sizeof(msg),
                      "assist-push: granted (%s package, %s wielder) shots std=%d->%d common=%d->%d bond=%d->%d push=%d handles=%d",
                      use_tel ? "TelRea" : "Anakin", two_h ? "2H" : "1H", before[0], after[0], before[1], after[1],
                      before[2], after[2], push_present, granted_abilities);
        logf(msg);
    }

    // v2.18.4: readable object-name into a narrow buffer (engine FName::ToString by RVA;
    // the tiny engine-allocated wide buffer is intentionally leaked - diagnostic-only).
    auto fname_str(const void* fname, char* out, int cap) -> void
    {
        out[0] = '?'; out[1] = 0;
        if (!g_fname_tostr_ok) return;
        FStringLite s{};
        g_fault_code = 0; g_fault_rva = 0;
        __try { reinterpret_cast<FNameToStrFn>(g_base + kFNameToStringRva)(fname, &s); }
        __except (med_seh_filter(GetExceptionCode(), GetExceptionInformation())) { return; }
        if (s.data != nullptr && s.num > 0) std::snprintf(out, cap, "%ls", s.data);
    }

    // v2.18.4 per-char diagnostic + combat-join re-strip (see the kFNameToStringRva block).
    auto assist_diag_char(std::size_t i) -> void
    {
        void* ch = g_assist_ch[i];
        if (ch == nullptr) return;
        if ((g_is_real != nullptr && !g_is_real(ch)) || obj_name_ci(ch) != g_assist_name[i]) return;
        void* asc = asc_of(ch);
        if (asc == nullptr) return;
        auto* base = reinterpret_cast<std::uint8_t*>(asc);
        std::uint8_t* data = nullptr;
        std::int32_t num = 0;
        std::uint64_t sig = 0;
        g_fault_code = 0; g_fault_rva = 0;
        __try
        {
            data = *reinterpret_cast<std::uint8_t**>(base + kAscAbilitiesOff);
            num = *reinterpret_cast<std::int32_t*>(base + kAscAbilitiesOff + 8);
            if (num < 0 || num > 64 || (num > 0 && data == nullptr)) return;
            sig = static_cast<std::uint64_t>(num) << 56;
            for (std::int32_t k = 0; k < num; ++k)
            {
                void* ability = *reinterpret_cast<void**>(data + static_cast<std::size_t>(k) * kSpecStride + kSpecAbilityOff);
                if (ability != nullptr)
                {
                    void* cls = *reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(ability) + 0x10);
                    if (cls != nullptr) sig += obj_name_ci(cls);
                }
            }
        }
        __except (med_seh_filter(GetExceptionCode(), GetExceptionInformation())) { return; }
        if (sig != g_assist_absig[i])
        {
            g_assist_absig[i] = sig;
            char line[240];
            int off = std::snprintf(line, sizeof(line), "assist-diag: abilities of %p (n=%d):", ch, num);
            for (std::int32_t k = 0; k < num; ++k)
            {
                void* ability = nullptr; void* cls = nullptr;
                g_fault_code = 0; g_fault_rva = 0;
                __try
                {
                    ability = *reinterpret_cast<void**>(data + static_cast<std::size_t>(k) * kSpecStride + kSpecAbilityOff);
                    if (ability != nullptr)
                        cls = *reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(ability) + 0x10);
                }
                __except (med_seh_filter(GetExceptionCode(), GetExceptionInformation())) { cls = nullptr; }
                char nm[96];
                if (cls != nullptr) fname_str(reinterpret_cast<std::uint8_t*>(cls) + 0x18, nm, sizeof(nm));
                else std::snprintf(nm, sizeof(nm), "nil");
                if (off > 0 && off + 100 >= static_cast<int>(sizeof(line)))
                {
                    logf(line);
                    off = std::snprintf(line, sizeof(line), "assist-diag: ...");
                }
                if (off > 0 && off < static_cast<int>(sizeof(line)))
                    off += std::snprintf(line + off, sizeof(line) - off, " %s", nm);
            }
            logf(line);
        }
        // v2.18.5: WATCH-ONLY - the re-strip is disabled while diagnosing the "assist does
        // NOTHING" case (Alex, 08-30 ~22:52: push granted+verified on all three Jedi, no
        // shots present, yet a called assist no-ops on both lanes; a strip racing an
        // on-demand grant could mask the flow we need to observe). kAssistDiagStrip=true
        // restores the 2.18.4 behavior (strip any known shot GA that (re)appears).
        if (!kAssistDiagStrip) return;
        void* fns_cdo = nullptr;
        void* fn_rm = cdo_func_guarded(kAbilityFnsCdo, STR("RemoveAbilityByClass"), fns_cdo);
        if (fn_rm == nullptr) return;
        const wchar_t* paths[3] = { kAssistStdGaPath, kAssistCommonGaPath, kAssistBondGaPath };
        const char* labels[3] = { "std", "common", "bond" };
        for (int k = 0; k < 3; ++k)
        {
            void* cls = find_arch_guarded(paths[k]);
            if (cls == nullptr || !asc_has_ability(asc, cls)) continue;
            struct { void* asc; void* cls; } rp{ asc, cls };
            process_event_guarded(fns_cdo, fn_rm, &rp);
            char msg[128];
            std::snprintf(msg, sizeof(msg), "assist-push: %s shot re-granted mid-mission - stripped (removed=%d)",
                          labels[k], asc_has_ability(asc, cls) ? 0 : 1);
            logf(msg);
        }
    }

    auto assist_diag_pass() -> void
    {
        for (std::size_t i = 0; i < kPendingMax; ++i) assist_diag_char(i);
    }

    // One pass: resolve each meditation item asset (find first - already loaded after any prior
    // grant - then load) and grant any the company does not own yet. Idempotent twice over (the
    // owned check here + the game's own MaxAmount clamp in ABrunoHQInventory::AddItem). Driven
    // from on_update; only runs in the hub (HQ inventory actor exists), retries until every item
    // is accounted for, attempt-capped so a broken path can never log-spam forever.
    constexpr unsigned kMedAttemptCap = 40;
    auto try_grant_meditations() -> void
    {
        void* world = grant_world();   // GWorld global, vtable-verified; null in menus only
        if (world == nullptr)
        {   // v2.13.2: periodic heartbeat so a stuck session names its stage (was: fully silent)
            if ((++g_med_waits & 63u) == 0) logf("meditations: still waiting (world not live - menu or loading)");
            return;
        }
        void* inv = hq_inventory_guarded(world);
        if (inv == nullptr)
        {   // mission (or faulted) - retry later, no attempt burned
            if (!g_med_log_noinv) { logf("meditations: waiting for the hub (HQ inventory not present yet)"); g_med_log_noinv = true; }
            else if ((++g_med_waits & 63u) == 0) logf("meditations: still waiting for the hub (world live, no HQ inventory - mission save?)");
            return;
        }
        ++g_med_attempts;
        int granted = 0, owned = 0, failed = 0;
        for (std::size_t i = 0; i < kMeditationCount; ++i)
        {
            void* item = find_arch_guarded(kMeditationPaths[i]);
            if (item == nullptr) item = load_item_guarded(kMeditationPaths[i]);
            if (item == nullptr) { ++failed; continue; }
            const int have = total_owned_guarded(inv, item);
            if (have < 0) { ++failed; continue; }
            if (have > 0) { ++owned; continue; }
            if (grant_item_guarded(world, item)) ++granted; else ++failed;
        }
        char msg[192];
        std::snprintf(msg, sizeof(msg), "meditations: granted=%d already-owned=%d failed=%d (attempt=%u fault=0x%llx rva=0x%llx)",
                      granted, owned, failed, g_med_attempts, (unsigned long long)g_fault_code, (unsigned long long)g_fault_rva);
        logf(msg);
        if (failed == 0) g_meds_granted = true;
    }

    // ---- Den cold-load crash guard (see the kEnableDenLoadFix block for the full story) ----
    // SEH probe: read 8 bytes at p if readable (POD body so __try is legal).
    auto probe_qword(const void* p, std::uint64_t* out) -> bool
    {
        __try { *out = *reinterpret_cast<const volatile std::uint64_t*>(p); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        return true;
    }

    // Guarded IsCompactable probe: perform EXACTLY the virtual call the engine's compaction is
    // about to make ([vtable+0x28] on the instance), under SEH. A freed instance recycled by an
    // allocation that happens to carry a plausible vtable passes any inspection - the only exact
    // test is executing the call itself with a net underneath. Returns false if the call faulted.
    auto probe_iscompactable(void* inst) -> bool
    {
        __try
        {
            auto* vt = *reinterpret_cast<void***>(inst);
            auto fn = reinterpret_cast<bool(__fastcall*)(void*)>(vt[5]);   // vtbl+0x28
            (void)fn(inst);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        return true;
    }

    // Null every element of a multicast invocation list whose instance is unreadable, has an
    // out-of-image vtable, or FAULTS in the guarded IsCompactable probe; the engine's compaction
    // drops null entries natively (its inst==null / handle==0 early-outs precede the virtual
    // call). Live entries just get their IsCompactable called one extra time (a const query the
    // engine calls on every compaction anyway). Returns the number neutralized.
    auto scrub_delegate_list(std::uint8_t* base) -> int
    {
        std::uint64_t data{}, counts{};
        if (!probe_qword(base, &data) || data == 0) return 0;
        if (!probe_qword(base + 8, &counts)) return 0;
        const auto num = static_cast<std::int32_t>(counts & 0xFFFFFFFFull);
        const auto max = static_cast<std::int32_t>(counts >> 32);
        if (num <= 0 || num > max || max > 1024) return 0;
        int fixed = 0;
        for (std::int32_t i = 0; i < num; ++i)
        {
            auto* el = reinterpret_cast<std::uint8_t*>(data) + static_cast<std::size_t>(i) * 16;
            std::uint64_t inst{};
            if (!probe_qword(el, &inst)) return fixed;   // list buffer unreadable - not ours to touch
            if (inst == 0) continue;                     // already unbound; engine compacts natively
            std::uint64_t vt{};
            const bool plausible = probe_qword(reinterpret_cast<void*>(inst), &vt) &&
                                   vt >= g_base && vt < g_base + kImageSize;
            if (plausible && probe_iscompactable(reinterpret_cast<void*>(inst)))
                continue;                                // survived the exact call the engine makes
            *reinterpret_cast<std::uint64_t*>(el) = 0;
            *reinterpret_cast<std::uint64_t*>(el + 8) = 0;
            ++fixed;
        }
        return fixed;
    }

    auto denfix_report(int fixed) -> void
    {
        if (fixed <= 0) return;
        g_denfix_count += static_cast<unsigned>(fixed);
        if (g_denfix_count <= 16u || (g_denfix_count & 63u) == 0)
        {
            char msg[128];
            std::snprintf(msg, sizeof(msg), "den-fix: neutralized %d dangling delegate instance(s) (total=%u tick=%u)",
                          fixed, g_denfix_count, g_update_ticks);
            logf(msg);
        }
    }

    // Guarded passthroughs (POD bodies so __try is legal): even with the exact-probe pre-scrub,
    // wrap the engine's own walk too - belt, braces, and a second belt.
    auto call_remove_guarded(void* base, void* handle) -> bool
    {
        __try { g_remove_deleg_orig(base, handle); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        return true;
    }
    auto call_actorit_guarded(void* self, void* world, void* a3, void* a4, void** out) -> bool
    {
        __try { *out = g_actorit_orig(self, world, a3, a4); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        return true;
    }

    // Synchronous pre-scrub at the compaction entry points: the engine is about to walk this
    // exact list on this exact thread - nothing can recycle memory between scrub and walk.
    auto __fastcall hook_remove_deleg(void* base, void* handle) -> void
    {
        if (g_remove_deleg_orig == nullptr) return;
        if (base == nullptr) { g_remove_deleg_orig(base, handle); return; }
        denfix_report(scrub_delegate_list(reinterpret_cast<std::uint8_t*>(base)));
        if (call_remove_guarded(base, handle)) return;
        logf("den-fix: RemoveDelegateInstance faulted - sanitizing and retrying");
        denfix_report(scrub_delegate_list(reinterpret_cast<std::uint8_t*>(base)));
        if (!call_remove_guarded(base, handle))
            logf("den-fix: RemoveDelegateInstance faulted twice - removal skipped (leaked handle, not a crash)");
    }

    auto __fastcall hook_actorit_ctor(void* self, void* world, void* a3, void* a4) -> void*
    {
        if (g_actorit_orig == nullptr) return nullptr;
        std::uint64_t vt{};
        const bool is_world = world != nullptr && probe_qword(world, &vt) &&
                              vt == static_cast<std::uint64_t>(g_base + kUWorldVtableRva);
        if (is_world)
            denfix_report(scrub_delegate_list(reinterpret_cast<std::uint8_t*>(world) + kWorldSpawnListOff));
        void* ret = nullptr;
        if (call_actorit_guarded(self, world, a3, a4, &ret)) return ret;
        logf("den-fix: FActorIteratorState ctor faulted - sanitizing and retrying");
        if (is_world)
            denfix_report(scrub_delegate_list(reinterpret_cast<std::uint8_t*>(world) + kWorldSpawnListOff));
        if (call_actorit_guarded(self, world, a3, a4, &ret)) return ret;
        logf("den-fix: FActorIteratorState ctor faulted twice - last unguarded attempt");
        return g_actorit_orig(self, world, a3, a4);
    }

    // Per-frame early-detection layer: scrub the current world's OnActorSpawned list straight
    // off the GWorld global (armed from the first tick; skipped while the list is locked).
    auto scrub_gworld() -> void
    {
        void* world = *reinterpret_cast<void* const*>(g_base + kGWorldRva);
        std::uint64_t vt{};
        if (world == nullptr || !probe_qword(world, &vt) ||
            vt != static_cast<std::uint64_t>(g_base + kUWorldVtableRva)) return;
        auto* base = reinterpret_cast<std::uint8_t*>(world) + kWorldSpawnListOff;
        std::uint64_t thr_lock{};   // +0x10 CompactionThreshold, +0x14 InvocationListLockCount
        if (probe_qword(base + 0x10, &thr_lock) && (thr_lock >> 32) != 0) return;   // mid-broadcast
        denfix_report(scrub_delegate_list(base));
    }

    // ---- 1.3 Force Jump: Jedi nav-filter lend (see the kEnableForceJump block for the model) ----
    auto load_navfilter_guarded(bool allow_load) -> void*
    {
        void* r = find_arch_guarded(kJediNavFilterPath);   // cheap once loaded
        if (r != nullptr || !allow_load || !g_nav_ok) return r;
        g_fault_code = 0; g_fault_rva = 0;
        __try
        {
            void* cls = reinterpret_cast<ZConstructFn>(g_base + kUClassStaticClassRva)();
            if (cls != nullptr)
                r = reinterpret_cast<StaticLoadObjFn>(g_base + kStaticLoadObjectRva)(
                        cls, nullptr, kJediNavFilterPath, nullptr, 0u, nullptr, true, nullptr);
        }
        __except (med_seh_filter(GetExceptionCode(), GetExceptionInformation())) { r = nullptr; }
        return r;
    }

    // Swap the character's BaseNavigationQueryFilter to the Jedi filter (idempotent; registers
    // the original for the revert). allow_load=false keeps the hot stance hook load-free - the
    // settled-window caller passes true and everyone afterwards hits the find path.
    auto apply_jedi_nav(void* ch, bool allow_load) -> void
    {
        if (!kEnableForceJump || g_get_value_ptr == nullptr || ch == nullptr) return;
        auto* slot = reinterpret_cast<void**>(g_get_value_ptr(ch, L"BaseNavigationQueryFilter"));
        if (slot == nullptr)
        {
            if (!g_nav_log_noprop) { logf("force-jump: BaseNavigationQueryFilter property not found on pawn"); g_nav_log_noprop = true; }
            return;
        }
        void* jedi = load_navfilter_guarded(allow_load);
        if (jedi == nullptr || *slot == jedi) return;
        // never hand the game a class object that is still loading or being destroyed:
        // RF_NeedInitialization|RF_NeedLoad|RF_NeedPostLoad|RF_NeedPostLoadSubobjects|
        // RF_BeginDestroyed|RF_FinishDestroyed must all be clear (ObjectFlags at +0x8)
        const auto flags = *reinterpret_cast<const std::uint32_t*>(reinterpret_cast<std::uint8_t*>(jedi) + 0x8);
        if ((flags & 0x1B600u) != 0) return;
        const std::uint64_t nm = obj_name_ci(ch);
        std::size_t have_i = kPendingMax, free_i = kPendingMax;
        for (std::size_t i = 0; i < kPendingMax; ++i)
        {
            if (g_nav_ch[i] == ch && g_nav_name[i] == nm) have_i = i;
            else if (g_nav_ch[i] == nullptr && free_i == kPendingMax) free_i = i;
        }
        if (have_i == kPendingMax)
        {
            if (free_i == kPendingMax) return;   // registry full - leave vanilla so every swap stays revertible
            g_nav_ch[free_i] = ch; g_nav_name[free_i] = nm; g_nav_orig[free_i] = *slot;
        }
        *slot = jedi;
        char msg[96];
        std::snprintf(msg, sizeof(msg), "force-jump: nav filter -> Jedi on %p (tick=%u)", ch, g_update_ticks);
        logf(msg);
    }

    auto revert_jedi_nav(void* ch) -> void
    {
        if (ch == nullptr) return;
        for (std::size_t i = 0; i < kPendingMax; ++i)
        {
            if (g_nav_ch[i] != ch || g_nav_name[i] != obj_name_ci(ch)) continue;
            auto* slot = g_get_value_ptr != nullptr
                             ? reinterpret_cast<void**>(g_get_value_ptr(ch, L"BaseNavigationQueryFilter")) : nullptr;
            if (slot != nullptr && g_nav_orig[i] != nullptr)
            {
                *slot = g_nav_orig[i];
                logf("force-jump: nav filter restored (sabers off)");
            }
            g_nav_ch[i] = nullptr; g_nav_name[i] = 0; g_nav_orig[i] = nullptr;
            return;
        }
    }

    // ---- 1.4 v2.11.0 Tel-rig leader mesh lend (see the kEnableLeaderSwap block) ----
    bool g_lswap_ok{};   // Z_Construct byte-verified at install
    void* g_lswap_ch[kPendingMax]{};
    std::uint64_t g_lswap_name[kPendingMax]{};
    void* g_lswap_orig[kPendingMax]{};   // original leader mesh ASSET
    bool g_lswap_log_nofn{};

    auto load_telleader_guarded(bool allow_load) -> void*
    {
        void* r = find_arch_guarded(kTelNullLeaderPath);   // cheap once resident
        if (r != nullptr || !allow_load || !g_lswap_ok) return r;
        g_fault_code = 0; g_fault_rva = 0;
        __try
        {
            void* cls = reinterpret_cast<ZConstructFn>(g_base + kZConstructSkelMeshRva)();
            if (cls != nullptr)
                r = reinterpret_cast<StaticLoadObjFn>(g_base + kStaticLoadObjectRva)(
                        cls, nullptr, kTelNullLeaderPath, nullptr, 0u, nullptr, true, nullptr);
        }
        __except (med_seh_filter(GetExceptionCode(), GetExceptionInformation())) { r = nullptr; }
        return r;
    }

    auto leader_mesh_comp(void* ch) -> void*
    {
        void* pmesh = g_get_value_ptr != nullptr ? g_get_value_ptr(ch, L"Mesh") : nullptr;
        return pmesh != nullptr ? *reinterpret_cast<void**>(pmesh) : nullptr;
    }
    auto leader_mesh_asset(void* comp) -> void*
    {
        if (comp == nullptr) return nullptr;
        void* a = *reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(comp) + 0x5D8);
        if (a == nullptr) a = *reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(comp) + 0x5E0);
        return a;
    }
    auto set_leader_asset(void* comp, void* asset) -> bool
    {
        void* fn = g_get_func != nullptr ? g_get_func(comp, L"SetSkeletalMeshAsset") : nullptr;
        if (fn == nullptr)
        {
            if (!g_lswap_log_nofn) { logf("leader-swap: SetSkeletalMeshAsset UFunction not found - feature idle"); g_lswap_log_nofn = true; }
            return false;
        }
        struct { void* NewMesh; } params{ asset };
        return process_event_guarded(comp, fn, &params) && leader_mesh_asset(comp) == asset;
    }

    auto anim_instance_of(void* comp) -> void*
    {
        void* fn = g_get_func != nullptr ? g_get_func(comp, L"GetAnimInstance") : nullptr;
        if (fn == nullptr) return nullptr;
        struct { void* ret; } p{};
        return process_event_guarded(comp, fn, &p) ? p.ret : nullptr;
    }

    struct DelegateSave { void* data; std::int32_t num; std::int32_t max; };

    // Steal every non-empty saber/montage delegate list off `ai` (zeroing the source headers so
    // the dying instance cannot free the buffers). POD entries; buffers change owner only.
    auto capture_anim_delegates(void* ai, DelegateSave* out) -> int
    {
        int captured = 0;
        if (ai == nullptr || g_get_value_ptr == nullptr) return 0;
        for (std::size_t i = 0; i < kMigrateCount; ++i)
        {
            auto* p = reinterpret_cast<TArray<std::uint8_t>*>(g_get_value_ptr(ai, kMigrateDelegates[i]));
            if (p != nullptr && p->data != nullptr && p->num > 0 && p->num <= 64)
            {
                out[i] = DelegateSave{ p->data, p->num, p->max };
                p->data = nullptr; p->num = 0; p->max = 0;
                ++captured;
            }
        }
        return captured;
    }

    // Install the stolen lists onto the NEW instance's same-named (freshly empty) properties.
    // Returns migrated slot count; unclaimable buffers stay allocated (tiny, once per swap).
    auto restore_anim_delegates(void* ai, DelegateSave* saves, int* out_entries) -> int
    {
        int migrated = 0, entries = 0;
        for (std::size_t i = 0; i < kMigrateCount; ++i)
        {
            if (saves[i].data == nullptr) continue;
            auto* q = ai != nullptr && g_get_value_ptr != nullptr
                          ? reinterpret_cast<TArray<std::uint8_t>*>(g_get_value_ptr(ai, kMigrateDelegates[i])) : nullptr;
            if (q != nullptr && q->data == nullptr)
            {
                q->data = static_cast<std::uint8_t*>(saves[i].data);
                q->num = saves[i].num; q->max = saves[i].max;
                ++migrated; entries += saves[i].num;
            }
            saves[i] = DelegateSave{};
        }
        if (out_entries != nullptr) *out_entries = entries;
        return migrated;
    }

    // v2.11.1: every early-out logs ONCE per reason - v2.11.0's first live run produced zero
    // leader-swap lines while the surrounding pipeline (fjump) landed, and silence made the
    // failing gate undiagnosable.
    auto lswap_diag(const char* why, void* ch) -> void
    {
        static const char* seen[10]{};
        for (auto& s : seen)
        {
            if (s == why) return;
            if (s == nullptr) { s = why; break; }
        }
        char msg[176];
        std::snprintf(msg, sizeof(msg), "leader-swap: DIAG %s (ch=%p tick=%u fault=0x%llx rva=0x%llx)",
                      why, ch, g_update_ticks, (unsigned long long)g_fault_code, (unsigned long long)g_fault_rva);
        logf(msg);
    }

    // Swap the wielder's leader mesh to the invisible Tel-rig null mesh (idempotent; registers
    // the original for the revert). Settled-window only - SetSkeletalMeshAsset re-initializes
    // the anim instance, which must never happen during load churn.
    auto apply_leader_swap(void* ch, bool allow_load) -> void
    {
        if (!kEnableLeaderSwap || !g_lswap_ok || ch == nullptr ||
            g_get_value_ptr == nullptr || g_process_event == nullptr || g_get_func == nullptr) return;
        if (!char_owns(ch, g_melee_2h)) return;   // Tel dual-saber lane only (Anakin 1H is native-clean)
        void* comp = leader_mesh_comp(ch);
        void* cur = leader_mesh_asset(comp);
        if (comp == nullptr) { lswap_diag("no mesh component", ch); return; }
        if (cur == nullptr) { lswap_diag("no leader asset yet", ch); return; }   // retried each quiet window
        // never touch a leader already on Tel's rig: for ROBED chars the leader IS the visible
        // robe body (SK_HSTF_TelRea) - swapping it to the null mesh would vanish the body, and
        // the anims are already same-skeleton there anyway
        static FName telskel_name{}, telnull_name{};
        if (telskel_name.comparison_index == 0) telskel_name = make_fname(L"SKEL_HSTF_TelRea", AddName);
        if (telnull_name.comparison_index == 0) telnull_name = make_fname(L"SK_HSTF_TelReaA_NullMesh", AddName);
        if (telnull_name.comparison_index != 0 &&
            reinterpret_cast<const FName*>(reinterpret_cast<std::uint8_t*>(cur) + 0x18)->comparison_index ==
                telnull_name.comparison_index)
            return;   // already swapped (silent - the self-heal pass revisits every quiet window)
        {
            void* pskel = g_get_value_ptr(cur, L"Skeleton");
            void* curskel = pskel != nullptr ? *reinterpret_cast<void**>(pskel) : nullptr;
            if (curskel != nullptr && telskel_name.comparison_index != 0 &&
                reinterpret_cast<const FName*>(reinterpret_cast<std::uint8_t*>(curskel) + 0x18)->comparison_index ==
                    telskel_name.comparison_index)
            { lswap_diag("leader already Tel-rig (robed lane) - skipped", ch); return; }
        }
        void* tel = load_telleader_guarded(allow_load);
        if (tel == nullptr)
        { lswap_diag(allow_load ? "TelReaA_NullMesh resolve/load FAILED" : "not resident and loads not allowed yet", ch); return; }
        if (cur == tel) return;   // already swapped
        const auto flags = *reinterpret_cast<const std::uint32_t*>(reinterpret_cast<std::uint8_t*>(tel) + 0x8);
        if ((flags & 0x1B600u) != 0) { lswap_diag("asset still loading - deferred", ch); return; }
        const std::uint64_t nm = obj_name_ci(ch);
        std::size_t have_i = kPendingMax, free_i = kPendingMax;
        for (std::size_t i = 0; i < kPendingMax; ++i)
        {
            if (g_lswap_ch[i] == ch && g_lswap_name[i] == nm) have_i = i;
            else if (g_lswap_ch[i] == nullptr && free_i == kPendingMax) free_i = i;
        }
        if (have_i == kPendingMax)
        {
            if (free_i == kPendingMax) { lswap_diag("registry full", ch); return; }   // stay vanilla so every swap is revertible
            g_lswap_ch[free_i] = ch; g_lswap_name[free_i] = nm; g_lswap_orig[free_i] = cur;
        }
        // v2.12.0: capture the saber/montage delegate subscriptions off the DYING instance
        // (the saber actors bound them at equip; the entries point at the actors, which
        // survive the swap - only the broadcast host changes).
        DelegateSave dsaves[kMigrateCount]{};
        void* old_ai = anim_instance_of(comp);
        const int captured = capture_anim_delegates(old_ai, dsaves);
        if (set_leader_asset(comp, tel))
        {
            // v2.11.3: re-seed the fresh instance's Proxy Table (BitReactor's weapon-
            // specialization anim selector; null = no combat pose, proven fix live).
            int seeded = 0;
            void* proxy = nullptr;
            {
                void* pp = g_get_value_ptr(ch, L"CachedProxyTable");
                proxy = pp != nullptr ? *reinterpret_cast<void**>(pp) : nullptr;
            }
            void* ai = anim_instance_of(comp);
            if (proxy != nullptr && ai != nullptr)
            {
                auto* s1 = reinterpret_cast<void**>(g_get_value_ptr(ai, L"Proxy Table"));
                if (s1 != nullptr) { *s1 = proxy; ++seeded; }
                auto* s2 = reinterpret_cast<void**>(g_get_value_ptr(ai, L"Last Frame Proxy Table"));
                if (s2 != nullptr) { *s2 = proxy; ++seeded; }
            }
            int entries = 0;
            const int migrated = restore_anim_delegates(ai, dsaves, &entries);
            char msg[224];
            std::snprintf(msg, sizeof(msg),
                          "leader-swap: Tel-rig null leader on %p (proxy %d/2, delegates %d/%d lists %d entries, ai=%p, tick=%u)",
                          ch, seeded, migrated, captured, entries, ai, g_update_ticks);
            logf(msg);
        }
        else
        {
            // swap failed: the old instance is still live - give it its delegate lists back
            restore_anim_delegates(old_ai, dsaves, nullptr);
            if (have_i == kPendingMax && free_i != kPendingMax)
            {   // drop the fresh registry entry so a later success re-captures cleanly
                g_lswap_ch[free_i] = nullptr; g_lswap_name[free_i] = 0; g_lswap_orig[free_i] = nullptr;
            }
            char msg[160];
            std::snprintf(msg, sizeof(msg), "leader-swap: SetSkeletalMeshAsset failed on %p (fault=0x%llx rva=0x%llx)",
                          ch, (unsigned long long)g_fault_code, (unsigned long long)g_fault_rva);
            logf(msg);
        }
    }

    auto revert_leader_swap(void* ch) -> void
    {
        if (ch == nullptr) return;
        for (std::size_t i = 0; i < kPendingMax; ++i)
        {
            if (g_lswap_ch[i] != ch || g_lswap_name[i] != obj_name_ci(ch)) continue;
            void* comp = leader_mesh_comp(ch);
            if (comp != nullptr && g_lswap_orig[i] != nullptr)
            {
                DelegateSave dsaves[kMigrateCount]{};
                void* old_ai = anim_instance_of(comp);
                capture_anim_delegates(old_ai, dsaves);
                if (set_leader_asset(comp, g_lswap_orig[i]))
                {
                    restore_anim_delegates(anim_instance_of(comp), dsaves, nullptr);
                    logf("leader-swap: original leader restored (sabers off)");
                }
                else
                    restore_anim_delegates(old_ai, dsaves, nullptr);
            }
            g_lswap_ch[i] = nullptr; g_lswap_name[i] = 0; g_lswap_orig[i] = nullptr;
            return;
        }
    }

    // ---- v2.11.4 GearKit notify diagnostic (see the kEnableGkDiag block) ----
    using GkFindFn = void*(__fastcall*)(void* comp, void* tags);
    GkFindFn g_gkfind_orig{};
    Trampoline g_gkfind_tr{};
    std::atomic<unsigned> g_gkdiag_lines{};

    auto __fastcall hook_gkfind(void* comp, void* tags) -> void*
    {
        const auto ret = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        void* r = g_gkfind_orig != nullptr ? g_gkfind_orig(comp, tags) : nullptr;
        const auto rva = ret - g_base;
        if (rva >= 0x6D64000 && rva < 0x6D66000 &&
            g_gkdiag_lines.fetch_add(1, std::memory_order_relaxed) < kGkDiagLineCap)
        {
            const char* who = "?";
            if (rva < 0x6D64BD0) who = "ReattachState";
            else if (rva < 0x6D64D40) who = "ActivateWeapon";
            else if (rva < 0x6D64F60) who = "DeactivateWeapon";
            else if (rva < 0x6D650D0) who = "Reattach";
            else if (rva < 0x6D65130) who = "StowWeapon";
            else who = "UnstowWeapon";
            char msg[160];
            std::snprintf(msg, sizeof(msg), "gk-diag: %s -> item=%p (comp=%p ret=0x%llx tick=%u)",
                          who, r, comp, (unsigned long long)rva, g_update_ticks);
            logf(msg);
        }
        return r;
    }

    // ---- 1.4 global saber sockets (see the kEnableStowSockets block for the model) ----
    // v2.9.0 CORRECTION: the live zero-serve result + skeleton-package recon rewrote the model.
    // The saber gear/sequences attach at SKELETON-LEVEL SOCKETS named weapon_pistol_l_socket /
    // weapon_pistol_r_socket (+ prop_rSocket_aim) - USkeletalMeshSocket exports that exist ONLY
    // on SKEL_HSTF_TelRea (and the ABP rig SKEL_Humanoid_Orig, which mesh lookups never consult);
    // the per-kit skeletons (SKEL_HAA_*) carry NO sockets, so on any non-Tel outfit the attach
    // lookup misses and the saber falls back to the actor root = the glitch. The v2.7 stow-bone
    // sockets targeted names nothing ever queries (zero serves proved it). Tel's authored socket
    // properties (extracted from SKEL_HSTF_TelRea's export data): ZERO relative transform,
    // parented directly to the like-named ANIMATED bone - the choreography carrier.
    constexpr const wchar_t* kServeSocketNames[] = {
        STR("weapon_pistol_l_socket"), STR("weapon_pistol_r_socket"), STR("prop_rSocket_aim"),
    };
    constexpr const wchar_t* kServeBoneNames[] = {
        STR("weapon_pistol_l"), STR("weapon_pistol_r"), STR("prop_r"),
    };
    constexpr std::size_t kServeCount = sizeof(kServeSocketNames) / sizeof(kServeSocketNames[0]);

    using FindSockIdxFn = void*(__fastcall*)(void* mesh, FName name, std::int32_t* out_index);
    using FindSockInfoFn = void*(__fastcall*)(void* mesh, FName name, Transform3d* out_xform,
                                              std::int32_t* out_bone, std::int32_t* out_index);
    using StaticConstructFn = void*(__fastcall*)(void* params);
    FindSockIdxFn g_findsock_orig{};
    FindSockInfoFn g_findsockinfo_orig{};
    FName g_serve_name[kServeCount]{};                  // socket FNames
    FName g_serve_bone[kServeCount]{};                  // parent bone FNames
    std::atomic<void*> g_serve_sock[kServeCount]{};     // rooted USkeletalMeshSocket objects
    bool g_stow_ok{};                 // RVAs byte-verified + both hooks installed
    bool g_stow_built{};              // all sockets constructed and published
    unsigned g_stow_fails{};          // failed construction attempts (kStowFailCap)
    std::atomic<unsigned> g_stow_hits[kServeCount]{};   // served lookups (logged from on_update)
    unsigned g_stow_hits_logged{};

    auto same_fname(FName a, FName b) -> bool
    { return a.comparison_index == b.comparison_index && a.number == b.number; }

    // UE4SS installs its own inline hook on StaticConstructObject_Internal at startup (its
    // object-construction listener), so by the time we verify, the prologue is a jmp - the
    // v2.7.0 lesson (`RVA/byte verification failed` with pristine on-disk bytes). Accept the
    // pristine bytes OR a recognizable hook-jmp prologue: the function's identity is already
    // pinned by the PE timestamp + image-size gate, and calling through the hook is fine
    // (UE4SS's detour tail-calls the original).
    auto bytes_match_or_hooked(std::uintptr_t rva, const std::uint8_t* want) -> bool
    {
        if (bytes_match(rva, want)) return true;
        if (rva + 16 > kImageSize) return false;
        const auto* p = reinterpret_cast<const std::uint8_t*>(g_base + rva);
        return p[0] == 0xE9 ||                                          // jmp rel32
               (p[0] == 0xFF && p[1] == 0x25) ||                        // jmp [rip+disp32]
               (p[0] == 0x48 && p[1] == 0xB8 && p[10] == 0xFF && p[11] == 0xE0);   // mov rax,imm64; jmp rax
    }

    // One refskeleton pass: index of the requested socket name as a bone (own) and of the
    // socket's parent bone. Same access pattern as resolve_head_bone; larger cap because body
    // meshes carry facial rigs (Hawks TORS > 768).
    auto stow_scan_bones2(const void* mesh, FName want, FName parent_name,
                          std::int32_t& own, std::int32_t& parent) -> bool
    {
        own = -1; parent = -1;
        const auto vtbl = *reinterpret_cast<const std::uintptr_t*>(mesh);
        if (vtbl == 0) return false;
        auto get_ref = *reinterpret_cast<GetRefSkelFn*>(vtbl + kGetRefSkelVtOff);
        if (get_ref == nullptr) return false;
        void* refskel = get_ref(mesh);
        if (refskel == nullptr) return false;
        const std::int32_t count = *reinterpret_cast<std::int32_t*>(reinterpret_cast<std::uint8_t*>(refskel) + kRefSkelNumOff);
        auto* data = *reinterpret_cast<std::uint8_t**>(reinterpret_cast<std::uint8_t*>(refskel) + kRefSkelArrOff);
        if (count <= 0 || count > 4096 || data == nullptr) return false;
        for (std::int32_t i = 0; i < count; ++i)
        {
            const FName bn = *reinterpret_cast<const FName*>(data + static_cast<std::size_t>(i) * kBoneInfoStride);
            if (same_fname(bn, want)) { own = i; if (parent >= 0) break; }
            else if (same_fname(bn, parent_name)) { parent = i; if (own >= 0) break; }
        }
        return true;
    }

    // SEH-guarded serve (POD body; runs on whatever thread the engine looked the socket up from -
    // read-only except the caller's own out-params and a relaxed hit counter; no logging here).
    // Tel's sockets are ZERO-offset children of their like-named animated bone, so the served
    // local transform is identity and the bone index is the parent bone's index on this mesh.
    auto serve_stow_guarded(void* mesh, FName name, std::size_t si, void* sock, Transform3d* out_x,
                            std::int32_t* out_bone, std::int32_t* out_index) -> void*
    {
        __try
        {
            std::int32_t own = -1, parent = -1;
            if (!stow_scan_bones2(mesh, name, g_serve_bone[si], own, parent)) return nullptr;
            if (own >= 0) return nullptr;      // the mesh has the name as a real bone - stand down
            if (parent < 0) return nullptr;    // no parent bone - not a body mesh, keep the miss
            if (out_x != nullptr)
            {
                out_x->rotation = Quaternion4d{0.0, 0.0, 0.0, 1.0};
                out_x->translation = Vector3d{0.0, 0.0, 0.0};
                out_x->scale = Vector3d{1.0, 1.0, 1.0};
            }
            if (out_bone != nullptr) *out_bone = parent;
            if (out_index != nullptr) *out_index = 0;
            g_stow_hits[si].fetch_add(1u, std::memory_order_relaxed);
            return sock;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    auto serve_stow(void* mesh, FName name, Transform3d* out_x, std::int32_t* out_bone,
                    std::int32_t* out_index) -> void*
    {
        std::size_t si = kServeCount;
        for (std::size_t k = 0; k < kServeCount; ++k)
            if (g_serve_name[k].comparison_index != 0 && same_fname(name, g_serve_name[k])) { si = k; break; }
        if (si == kServeCount) return nullptr;
        void* sock = g_serve_sock[si].load(std::memory_order_acquire);
        if (sock == nullptr || mesh == nullptr) return nullptr;
        return serve_stow_guarded(mesh, name, si, sock, out_x, out_bone, out_index);
    }

    std::atomic<unsigned> g_stow_native[kServeCount]{};  // lookups the game resolved NATIVELY
    auto count_native_hit(FName name) -> void
    {
        for (std::size_t k = 0; k < kServeCount; ++k)
            if (same_fname(name, g_serve_name[k]))
            { g_stow_native[k].fetch_add(1u, std::memory_order_relaxed); return; }
    }

    auto __fastcall hook_find_socket_idx(void* mesh, FName name, std::int32_t* out_index) -> void*
    {
        if (g_findsock_orig == nullptr) return nullptr;
        void* r = g_findsock_orig(mesh, name, out_index);
        if (r != nullptr) { count_native_hit(name); return r; }
        return serve_stow(mesh, name, nullptr, nullptr, out_index);
    }

    auto __fastcall hook_find_socket_info(void* mesh, FName name, Transform3d* out_x,
                                          std::int32_t* out_bone, std::int32_t* out_index) -> void*
    {
        if (g_findsockinfo_orig == nullptr) return nullptr;
        void* r = g_findsockinfo_orig(mesh, name, out_x, out_bone, out_index);
        if (r != nullptr) { count_native_hit(name); return r; }
        return serve_stow(mesh, name, out_x, out_bone, out_index);
    }

    // FStaticConstructObjectParameters, zero-extended: the callee reads only the fields below
    // plus zeroed tail bytes (empty TFunction PropertyInitCallback etc. = engine defaults).
    struct StowConstructParams
    {
        void* cls{};                       // +0x00
        void* outer{};                     // +0x08
        FName name{};                      // +0x10
        std::uint32_t set_flags{};         // +0x18 EObjectFlags
        std::uint32_t internal_flags{};    // +0x1C
        std::uint8_t copy_transients{};    // +0x20
        std::uint8_t template_archetype{}; // +0x21
        std::uint8_t pad[6]{};
        void* templ{};                     // +0x28 (read by StaticConstructObject_Internal)
        void* instance_graph{};            // +0x30
        void* external_package{};          // +0x38
        std::uint8_t tail[0x80]{};         // TFunction + any newer fields, zero = unset
    };
    static_assert(offsetof(StowConstructParams, set_flags) == 0x18 &&
                  offsetof(StowConstructParams, templ) == 0x28, "construct params layout");

    auto construct_socket_guarded(void* cls, void* outer) -> void*
    {
        void* r = nullptr;
        g_fault_code = 0; g_fault_rva = 0;
        __try
        {
            StowConstructParams p{};
            p.cls = cls; p.outer = outer;
            p.set_flags = 0xC1u;   // RF_Public | RF_Transient | RF_MarkAsRootSet (GC-proof at birth)
            r = reinterpret_cast<StaticConstructFn>(g_base + kStaticConstructRva)(&p);
        }
        __except (med_seh_filter(GetExceptionCode(), GetExceptionInformation())) { r = nullptr; }
        return r;
    }

    auto stow_class_guarded() -> void*
    {
        void* r = nullptr;
        g_fault_code = 0; g_fault_rva = 0;
        __try { r = reinterpret_cast<ZConstructFn>(g_base + kZConstructSocketRva)(); }
        __except (med_seh_filter(GetExceptionCode(), GetExceptionInformation())) { r = nullptr; }
        return r;
    }

    // USkeletalMeshSocket UPROPERTYs via reflection (FVector/FRotator are 3 doubles each in this
    // build - GetSocketLocalTransform reads RelativeRotation as doubles at +0x50, disasm-verified).
    // Tel's authored sockets carry NO relative transform (zero-offset children of their bone).
    auto stow_set_props(void* sock, FName socket_name, FName bone_name) -> bool
    {
        if (g_get_value_ptr == nullptr) return false;
        auto* sn = reinterpret_cast<FName*>(g_get_value_ptr(sock, L"SocketName"));
        auto* bn = reinterpret_cast<FName*>(g_get_value_ptr(sock, L"BoneName"));
        auto* rl = reinterpret_cast<Vector3d*>(g_get_value_ptr(sock, L"RelativeLocation"));
        auto* rr = reinterpret_cast<Vector3d*>(g_get_value_ptr(sock, L"RelativeRotation"));
        auto* rs = reinterpret_cast<Vector3d*>(g_get_value_ptr(sock, L"RelativeScale"));
        if (sn == nullptr || bn == nullptr || rl == nullptr || rr == nullptr || rs == nullptr) return false;
        *sn = socket_name;
        *bn = bone_name;
        *rl = Vector3d{0.0, 0.0, 0.0};
        *rr = Vector3d{0.0, 0.0, 0.0};
        *rs = Vector3d{1.0, 1.0, 1.0};
        return true;
    }

    // Build all served sockets (once per session; on_update calls this in a stance-quiet window).
    auto build_stow_sockets() -> void
    {
        if (!g_stow_ok || g_stow_built || g_stow_fails >= kStowFailCap) return;
        void* cls = stow_class_guarded();
        void* outer = cls != nullptr
                          ? *reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(cls) + 0x20)  // UObjectBase::OuterPrivate = /Script/Engine
                          : nullptr;
        std::size_t done = 0;
        if (outer != nullptr)
            for (std::size_t k = 0; k < kServeCount; ++k)
            {
                if (g_serve_sock[k].load(std::memory_order_relaxed) != nullptr) { ++done; continue; }
                void* s = construct_socket_guarded(cls, outer);
                if (s != nullptr && stow_set_props(s, g_serve_name[k], g_serve_bone[k]))
                { g_serve_sock[k].store(s, std::memory_order_release); ++done; }
            }
        char msg[224];
        if (done == kServeCount)
        {
            g_stow_built = true;
            std::snprintf(msg, sizeof(msg), "saber-sockets: built %zu (wpn_pistol_l/r_socket + prop_rSocket_aim, zero-offset bone children, tick=%u)",
                          done, g_update_ticks);
        }
        else
        {
            ++g_stow_fails;
            std::snprintf(msg, sizeof(msg),
                          "saber-sockets: build attempt %u incomplete (%zu/%zu, cls=%p outer=%p fault=0x%llx rva=0x%llx)%s",
                          g_stow_fails, done, kServeCount, cls, outer,
                          (unsigned long long)g_fault_code, (unsigned long long)g_fault_rva,
                          g_stow_fails >= kStowFailCap ? " - DISABLED this session" : "");
        }
        logf(msg);
    }

    // ---- 1.4 translation-retarget lend (see the kEnableRetargetFix block for the model) ----
    FName g_rt_excl[kRetargetExclCount]{};
    bool g_rt_ok{};                  // exclusion names resolved at install
    bool g_rt_log_nomesh{}, g_rt_log_layout{};   // one-time diagnostics
    void* g_rt_seen[kPendingMax]{};  // skeletons already reported (log dedup only)

    // Flip the leader-mesh skeleton's BoneTree translation-retargeting to Tel's configuration
    // (AnimationRelative everywhere except root/Weapon/ik_*). Idempotent; safe to re-run every
    // settled window (re-covers outfit swaps and freshly loaded skeletons). One-way per session.
    auto retarget_lend(void* ch) -> void
    {
        if (!kEnableRetargetFix || !g_rt_ok || g_get_value_ptr == nullptr || ch == nullptr) return;
        if (!char_owns(ch, g_melee_2h)) return;   // Tel dual-saber lane only
        void* pmesh = g_get_value_ptr(ch, L"Mesh");
        void* comp = pmesh != nullptr ? *reinterpret_cast<void**>(pmesh) : nullptr;
        if (comp == nullptr)
        {
            if (!g_rt_log_nomesh) { logf("retarget-lend: no leader mesh component"); g_rt_log_nomesh = true; }
            return;
        }
        // the engine's own asset read checks BOTH fields (GetSocketBoneName disasm: +0x5D8,
        // fallback +0x5E0) - v2.8.1 read only the first and skipped silently when it was null
        void* asset = *reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(comp) + 0x5D8);
        if (asset == nullptr) asset = *reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(comp) + 0x5E0);
        if (asset == nullptr)
        {
            if (!g_rt_log_nomesh) { logf("retarget-lend: leader mesh asset not resolved yet (will retry)"); g_rt_log_nomesh = true; }
            return;   // periodic retry pass in on_update covers it
        }
        void* pskel = g_get_value_ptr(asset, L"Skeleton");
        void* skel = pskel != nullptr ? *reinterpret_cast<void**>(pskel) : nullptr;
        if (skel == nullptr)
        {
            if (!g_rt_log_nomesh) { logf("retarget-lend: leader mesh has no Skeleton"); g_rt_log_nomesh = true; }
            return;
        }
        auto* bt = reinterpret_cast<TArray<std::uint8_t>*>(g_get_value_ptr(skel, L"BoneTree"));
        if (bt == nullptr || bt->data == nullptr || bt->num <= 0 || bt->num > 4096)
        {
            if (!g_rt_log_layout) { logf("retarget-lend: BoneTree not readable - feature idle"); g_rt_log_layout = true; }
            return;
        }
        auto* rs = reinterpret_cast<std::uint8_t*>(skel) + kSkelRefSkelOff;
        auto* info = *reinterpret_cast<std::uint8_t**>(rs + 0x20);            // FINAL FMeshBoneInfo*
        auto n = *reinterpret_cast<std::int32_t*>(rs + 0x28);
        if (info == nullptr || n != bt->num)
        {   // virtual-bone skeletons: BoneTree parallels the RAW arrays instead
            info = *reinterpret_cast<std::uint8_t**>(rs + 0x00);
            n = *reinterpret_cast<std::int32_t*>(rs + 0x08);
            if (info == nullptr || n != bt->num)
            {
                if (!g_rt_log_layout) { logf("retarget-lend: refskel/BoneTree count mismatch - feature idle"); g_rt_log_layout = true; }
                return;
            }
        }
        // The engine reads modes as ONE BYTE PER BONE from the raw member at skel+0x38
        // (GetBoneTranslationRetargetingMode disasm). Demand the reflection array and the raw
        // member agree exactly, and that the bytes look like the enum, before writing anything.
        if (bt->data != *reinterpret_cast<std::uint8_t* const*>(reinterpret_cast<std::uint8_t*>(skel) + kSkelBoneTreeOff) ||
            bt->num != *reinterpret_cast<const std::int32_t*>(reinterpret_cast<std::uint8_t*>(skel) + kSkelBoneTreeOff + 8))
        {
            if (!g_rt_log_layout) { logf("retarget-lend: BoneTree reflection/raw member disagree - feature idle"); g_rt_log_layout = true; }
            return;
        }
        for (std::int32_t i = 0; i < n && i < 16; ++i)
        {
            if (bt->data[i] > 4)
            {
                if (!g_rt_log_layout) { logf("retarget-lend: FBoneNode layout mismatch - feature idle"); g_rt_log_layout = true; }
                return;
            }
        }
        int flipped = 0;
        for (std::int32_t i = 0; i < n; ++i)
        {
            const FName bn = *reinterpret_cast<const FName*>(info + static_cast<std::size_t>(i) * kBoneInfoStride);
            bool excl = false;
            for (std::size_t k = 0; k < kRetargetExclCount; ++k)
                if (same_fname(bn, g_rt_excl[k])) { excl = true; break; }
            if (excl) continue;
            if (bt->data[i] == kModeSkeleton) { bt->data[i] = kModeAnimRelative; ++flipped; }
        }
        // Diagnostic: identify the leader skeleton by bone count + mode histogram (Hawks=1152,
        // Jed001=228, B00=632, TelRea=298). Log when work was done, and once per skeleton ptr
        // otherwise, so a flipped==0 pass is never invisible again (the v2.9.0 blind spot).
        bool seen = false;
        for (std::size_t i = 0; i < kPendingMax; ++i) if (g_rt_seen[i] == skel) { seen = true; break; }
        if (flipped > 0 || !seen)
        {
            if (!seen)
                for (std::size_t i = 0; i < kPendingMax; ++i)
                    if (g_rt_seen[i] == nullptr) { g_rt_seen[i] = skel; break; }
            unsigned hist[5] = {0, 0, 0, 0, 0};
            for (std::int32_t i = 0; i < n; ++i) if (bt->data[i] <= 4) ++hist[bt->data[i]];
            std::int32_t asset_bones = 0;
            { const auto vt = *reinterpret_cast<const std::uintptr_t*>(asset);
              auto gr = *reinterpret_cast<GetRefSkelFn*>(vt + kGetRefSkelVtOff);
              void* rsk = gr != nullptr ? gr(asset) : nullptr;
              if (rsk != nullptr) asset_bones = *reinterpret_cast<std::int32_t*>(reinterpret_cast<std::uint8_t*>(rsk) + kRefSkelNumOff); }
            char msg[224];
            std::snprintf(msg, sizeof(msg),
                          "retarget-lend: skel=%p bones=%d modes 0/1/2/3/4=%u/%u/%u/%u/%u flipped=%d (comp=%p asset=%p asset_bones=%d char=%p tick=%u)",
                          skel, n, hist[0], hist[1], hist[2], hist[3], hist[4], flipped,
                          comp, asset, asset_bones, ch, g_update_ticks);
            logf(msg);
        }
    }

    // ---- 1.4 v2.10.0 hand-IK retarget lend (see the kEnableIkLend block for the model) ----
    FName g_ik_names[kIkLendBoneCount]{};
    void* g_ik_skel_seen{};   // last skeleton object patched/verified (log dedup; reloads re-patch)
    bool g_ik_log_layout{};

    auto ik_retarget_lend() -> void
    {
        if (!kEnableIkLend || g_get_value_ptr == nullptr) return;
        if (g_ik_names[0].comparison_index == 0)
        {
            for (std::size_t k = 0; k < kIkLendBoneCount; ++k)
                g_ik_names[k] = make_fname(kIkLendBones[k], AddName);
            if (g_ik_names[0].comparison_index == 0) return;
        }
        void* skel = find_arch_guarded(kTelSkelPath);   // find-only; resident whenever Tel content is
        if (skel == nullptr) return;                    // not loaded yet - retried next quiet window
        auto* bt = reinterpret_cast<TArray<std::uint8_t>*>(g_get_value_ptr(skel, L"BoneTree"));
        if (bt == nullptr || bt->data == nullptr || bt->num <= 0 || bt->num > 4096)
        {
            if (!g_ik_log_layout) { logf("ik-lend: BoneTree not readable - feature idle"); g_ik_log_layout = true; }
            return;
        }
        auto* rs = reinterpret_cast<std::uint8_t*>(skel) + kSkelRefSkelOff;
        auto* info = *reinterpret_cast<std::uint8_t**>(rs + 0x20);            // FINAL FMeshBoneInfo*
        auto n = *reinterpret_cast<std::int32_t*>(rs + 0x28);
        if (info == nullptr || n != bt->num)
        {   // virtual-bone skeletons: BoneTree parallels the RAW arrays instead
            info = *reinterpret_cast<std::uint8_t**>(rs + 0x00);
            n = *reinterpret_cast<std::int32_t*>(rs + 0x08);
            if (info == nullptr || n != bt->num)
            {
                if (!g_ik_log_layout) { logf("ik-lend: refskel/BoneTree count mismatch - feature idle"); g_ik_log_layout = true; }
                return;
            }
        }
        if (bt->data != *reinterpret_cast<std::uint8_t* const*>(reinterpret_cast<std::uint8_t*>(skel) + kSkelBoneTreeOff) ||
            bt->num != *reinterpret_cast<const std::int32_t*>(reinterpret_cast<std::uint8_t*>(skel) + kSkelBoneTreeOff + 8))
        {
            if (!g_ik_log_layout) { logf("ik-lend: BoneTree reflection/raw member disagree - feature idle"); g_ik_log_layout = true; }
            return;
        }
        for (std::int32_t i = 0; i < n && i < 16; ++i)
        {
            if (bt->data[i] > 4)
            {
                if (!g_ik_log_layout) { logf("ik-lend: FBoneNode layout mismatch - feature idle"); g_ik_log_layout = true; }
                return;
            }
        }
        int flipped = 0, already = 0, matched = 0;
        for (std::int32_t i = 0; i < n; ++i)
        {
            const FName bn = *reinterpret_cast<const FName*>(info + static_cast<std::size_t>(i) * kBoneInfoStride);
            for (std::size_t k = 0; k < kIkLendBoneCount; ++k)
            {
                if (same_fname(bn, g_ik_names[k]))
                {
                    ++matched;
                    if (bt->data[i] == kModeSkeleton) { bt->data[i] = kModeAnimation; ++flipped; }
                    else if (bt->data[i] == kModeAnimation) ++already;
                    break;
                }
            }
        }
        if (flipped > 0 || skel != g_ik_skel_seen)
        {
            g_ik_skel_seen = skel;
            char msg[192];
            std::snprintf(msg, sizeof(msg),
                          "ik-lend: SKEL_HSTF_TelRea %p bones=%d hand-IK matched=%d flipped=%d already=%d -> Animation (tick=%u)",
                          skel, n, matched, flipped, already, g_update_ticks);
            logf(msg);
        }
    }

    // v1.9.0: retarget Tel's DUAL sabers to TEL-REA'S OWN stance sets. The Anakin control test
    // proved the char-side state we built (1HMelee front, Other, relink) is the correct recipe -
    // for weapons whose PROXY TABLE pairs with the generic melee stance. Tel's spec ships the
    // 2HSaber proxy table, authored to pair with SAS_TelRea/_Force (Tel-Rea's bespoke dual-wield
    // stance); pairing it with SAS_Human_1HMelee is the mismatched "bad pose". So for Melee.2H
    // wielders we write Tel-Rea's sets into the StanceAnimationSets UPROPERTY array directly
    // (the F10 probe proved such writes land and CurrentWeaponStanceTag follows live), then re-link.
    auto fix_stance_sets(void* ch) -> bool
    {
        if (g_get_value_ptr == nullptr || !char_owns(ch, g_melee_2h)) return false;
        const wchar_t* kTelPath   = L"/Game/Game/GameData/AnimationSets/SAS_TelRea.SAS_TelRea";
        const wchar_t* kForcePath = L"/Game/Game/GameData/AnimationSets/SAS_TelRea_Force.SAS_TelRea_Force";
        // resolve FRESH each attempt (the GC can collect unreferenced assets between attempts);
        // the UPROPERTY array write below roots them via the character
        void* tel = find_arch_guarded(kTelPath);
        if (tel == nullptr) tel = load_sas_guarded(kTelPath);
        void* force = find_arch_guarded(kForcePath);
        if (force == nullptr) force = load_sas_guarded(kForcePath);
        {
            char msg[160];
            std::snprintf(msg, sizeof(msg), "saber-stance: SAS_TelRea=%p SAS_TelRea_Force=%p (fault=0x%llx rva=0x%llx)",
                          tel, force, (unsigned long long)g_fault_code, (unsigned long long)g_fault_rva);
            logf(msg);
        }
        if (tel == nullptr) { logf("saber-stance: cannot resolve/load SAS_TelRea - deferring"); return false; }
        if (char_owns(ch, g_pwt_rifle)) flip_pwt(ch);
        if (set_binding_category(ch, g_cat_telrea))
            logf("saber-stance: binding category -> TelRea (settled window)");
        if (set_owned_tag(ch, g_bespoke, true))
            logf("saber-stance: bespoke Jedi.TelRea tag granted (settled window)");
        auto* arr = reinterpret_cast<TArray<void*>*>(g_get_value_ptr(ch, L"StanceAnimationSets"));
        if (arr == nullptr || arr->data == nullptr || arr->num < 1 || arr->num > 16)
        { logf("saber-stance: StanceAnimationSets array not writable - deferring"); return false; }
        arr->data[0] = tel;
        std::int32_t n = 1;
        if (force != nullptr && arr->max >= 2) { arr->data[1] = force; n = 2; }
        arr->num = n;
        bool has_rifle = false, has_melee = false, telrea_front = false;
        check_stance_sets(ch, has_rifle, has_melee, telrea_front);   // log the final array
        char msg[128];
        std::snprintf(msg, sizeof(msg), "saber-stance: TelRea stance write %s on %p", telrea_front ? "OK" : "FAILED", ch);
        logf(msg);
        return telrea_front;
    }

    // Force the character's anim graph to re-link its upper-body layer: re-assert PrimaryWeaponType.Other
    // (the game keeps re-adding Rifle during load), then SetAnimInstanceClass(nil)+SetAnimInstanceClass(class)
    // via ProcessEvent so the anim instance re-inits and re-links the saber layer with the correct tag live.
    auto relink_character(void* ch) -> bool
    {
        if (ch == nullptr || g_process_event == nullptr || g_get_func == nullptr || g_get_value_ptr == nullptr) { logf("relink: not ready"); return false; }
        if (g_is_real != nullptr && !g_is_real(ch)) { logf("relink: ch not real"); return false; }
        flip_pwt(ch);   // make sure Other is active right now, at the moment we re-link
        // The re-init re-links against the CURRENT stance data. If the game's last rebuild ran with
        // Rifle owned (pre-1.6.1 always did - the hook flipped AFTER the original), re-linking would
        // just re-install the rifle grip; defer instead and let the next pre-flipped rebuild fix it.
        bool has_rifle_set = false, has_melee_set = false, telrea_front = false;
        const bool sets_ok = check_stance_sets(ch, has_rifle_set, has_melee_set, telrea_front);
        if (sets_ok && !telrea_front && char_owns(ch, g_melee_2h))
        {
            // Tel's dual sabers: the active stance must be Tel-Rea's own set (pairs with the
            // 2HSaber proxy his spec ships). Anything else at the front - rifle OR the generic
            // melee stance - renders the mismatched "bad pose". Retarget, then fall through.
            if (!fix_stance_sets(ch))
            {
                logf("saber-stance: stance not TelRea yet - deferring re-link");
                return false;
            }
        }
        // mesh: ACharacter::Mesh (fall back to the component's own name)
        void* pmesh = g_get_value_ptr(ch, L"Mesh");
        void* mesh = pmesh ? *reinterpret_cast<void**>(pmesh) : nullptr;
        if (mesh == nullptr) { void* p2 = g_get_value_ptr(ch, L"CharacterMesh0"); mesh = p2 ? *reinterpret_cast<void**>(p2) : nullptr; }
        if (mesh == nullptr) { logf("relink: no mesh"); return false; }
        // class: read it off the LIVE anim instance (the exact path the working F7 test used)
        void* fn_getai = g_get_func(mesh, L"GetAnimInstance");
        if (fn_getai == nullptr) { logf("relink: no GetAnimInstance fn"); return false; }
        struct { void* ret; } ai{};
        g_process_event(mesh, fn_getai, &ai);
        if (ai.ret == nullptr) { logf("relink: no anim instance yet"); return false; }
        void* cls = *reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(ai.ret) + 0x10);  // UObjectBase::ClassPrivate
        if (cls == nullptr) { logf("relink: no class"); return false; }
        // v1.9.1: the SetAnimInstanceClass toggle is REMOVED. Alex's live observation proved the
        // re-init does NOT re-link a weapon layer - it DESTROYS it: the result is an unarmed-ish
        // stance with the sabers floating off the hands (the weapon-hold layer is linked only by
        // the game's real equip flow). The historical "F7-proven saber hold" was this same
        // artifact misread. We keep the corrected stance/tag data (harmless, needed) and stop
        // touching the anim instance.
        (void)cls;
        logf("saber-stance: state corrected (no anim re-init - equip flow owns the hold layer)");
        return true;
    }

    // (Re)arm the quiet-gated re-link for a char: existing entries get their quiet timer reset (and
    // mode updated), new chars claim a free slot. Called from the stance hook on every relevant event.
    auto pending_touch(void* ch, bool revert) -> void
    {
        if (ch == nullptr || g_process_event == nullptr || g_get_func == nullptr) return;
        const std::uint64_t name = obj_name_ci(ch);
        for (std::size_t i = 0; i < kPendingMax; ++i)
            if (g_pending_relink[i] == ch)
            {
                if (g_pending_name[i] != name) break;   // recycled address = a different char; claim fresh below
                g_pending_quiet[i] = g_update_ticks; g_pending_revert[i] = revert; return;
            }
        for (std::size_t i = 0; i < kPendingMax; ++i)
            if (g_pending_relink[i] == nullptr || g_pending_relink[i] == ch)
            {
                g_pending_relink[i] = ch; g_pending_name[i] = name; g_pending_quiet[i] = g_update_ticks;
                g_pending_last[i] = 0; g_pending_fails[i] = 0; g_pending_revert[i] = revert;
                return;
            }
    }

    auto flipped_find(void* ch) -> std::size_t
    {
        for (std::size_t i = 0; i < kPendingMax; ++i)
            if (g_flipped[i] == ch && g_flipped_name[i] == obj_name_ci(ch)) return i;
        return kPendingMax;
    }

    auto flipped_add(void* ch) -> bool
    {
        if (flipped_find(ch) != kPendingMax) return true;
        for (std::size_t i = 0; i < kPendingMax; ++i)
            if (g_flipped[i] == nullptr) { g_flipped[i] = ch; g_flipped_name[i] = obj_name_ci(ch); return true; }
        return false;   // registry full - caller must undo the flip so no char is stranded untracked
    }

    // Saber stance fix. CRITICAL ORDER (the v1.4-1.6.0 bug): the game's CacheStanceAnimationSetFromWeapon
    // rebuilds the char's StanceAnimationSets FROM THE OWNED TAGS DURING THIS CALL, and every load-time
    // call is preceded by a fresh Rifle re-grant - so flipping after the original left the stance data
    // rifle-built at every rebuild, and the later re-link faithfully re-installed the rifle grip.
    // Flip BEFORE the original: the game itself then selects the melee stance set, producing exactly the
    // data state (tags=Other + stance=melee) under which the manual re-link was proven to stick.
    // Every stance call here also resets the char's quiet timer, so the real re-link fires only once the
    // load-time churn has stopped. A char we flipped that no longer wields melee gets its rifle identity
    // restored + a re-link, so swapping back to a blaster stays correct too.
    auto __fastcall hook_cache_stance(void* context, void* frame, void* result) -> void
    {
        if (g_cache_stance_orig == nullptr) return;
        // v2.6.0: NO ctx capture here. v2.4.0 captured g_world_ctx from characters, which armed
        // the meditation grant DURING loads - its GetActorOfClass then walked a half-built world
        // and a swallowed fault leaked iterator delegate handles (the Den crash-on-load defect).
        // The context comes ONLY from hook_filter (armory open) - the v2.2/v2.3-proven rule that
        // makes a mid-load grant physically impossible. The stamp below is the world-churn clock.
        g_last_stance_tick = g_update_ticks;
        if (g_in_stance_fix) { g_cache_stance_orig(context, frame, result); return; }   // our own retarget call
        // v2.22.0 OPTION B: world still loading/churning? grant_world()==null until UWorld::HasBegunPlay
        // + PersistentLevel actors present = exactly "actors still spawning". In that window a PWT flip's
        // update_tag broadcast (see kEnableOptionB) corrupts world state; defer the flip to the settled
        // relink loop. Skipped-during-load chars are enqueued below so nothing is stranded.
        const bool loading = kEnableOptionB && grant_world() == nullptr;
        bool flipped = context != nullptr && !loading && flip_pwt(context);   // BEFORE the rebuild reads the tags
        // Dual-saber wielders: swap the presentation category BEFORE the game's stance/equip steps
        // run, so the hold layer and strike presentation resolve under Tel-Rea's binding.
        if (context != nullptr && char_owns(context, g_melee_2h))
        {
            if (set_binding_category(context, g_cat_telrea))
            {
                char msg[96];
                std::snprintf(msg, sizeof(msg), "saber-stance: binding category -> TelRea on %p (tick=%u)", context, g_update_ticks);
                logf(msg);
            }
            // NOTE: the bespoke owned-tag grant moved to the settled window (fix_stance_sets).
            // Granting it HERE fired an ASC tag broadcast during load churn; with meditation
            // gameplay-effects equipped (new listeners) that broadcast lands mid actor-spawn
            // and is the prime suspect for the OnActorSpawned delegate-list corruption
            // (2026-08-29 crash-on-load dumps: dangling delegate instances in UWorld+0x460).
        }
        g_cache_stance_orig(context, frame, result);
        if (context == nullptr) return;
        if (char_owns(context, g_melee_spec))
        {
            // NOTE: no apply_jedi_nav here anymore - v2.4.0 wrote the nav filter from this hook
            // DURING load churn, prime suspect in the crash-on-load streak. The settled-window
            // caller in on_update handles it; equip cycles re-arm via pending_touch below.
            if (!loading && flip_pwt(context)) flipped = true;      // melee tag / Rifle re-grant landed inside the original (OPTION B: not during load)
            bool hr = false, hm = false, tf = false;
            const bool cls_ok = check_stance_sets(context, hr, hm, tf, false);   // silent classification
            if (flipped)
            {
                ++g_flip_seq;
                if (flipped_add(context))
                {
                    char msg[128];
                    std::snprintf(msg, sizeof(msg), "saber-stance: flipped PrimaryWeaponType Rifle->Other on %p (tick=%u)", context, g_update_ticks);
                    logf(msg);
                    pending_touch(context, false);
                }
                else
                {   // registry full - undo the flip rather than strand a char we can't revert later
                    swap_pwt(context, g_pwt_other, g_pwt_rifle);
                    logf("saber-stance: flip registry full - flip undone, char left vanilla");
                }
            }
            else if (loading && char_owns(context, g_pwt_rifle))
            {   // v2.22.0 OPTION B: we skipped the flip because the world was loading. The char still
                // owns Rifle (the game just (re)granted it) = exactly the one we would have flipped.
                // Enqueue so the settled relink loop (on_update ~4310) re-flips Rifle->Other + re-links
                // once kQuietTicks of stance silence proves the world settled. Without this a NEW saber
                // wielder appearing during a cold load would never be queued (the else below only
                // refreshes already-present entries).
                bool already = false;
                for (std::size_t i = 0; i < kPendingMax; ++i)
                    if (g_pending_relink[i] == context && g_pending_name[i] == obj_name_ci(context)) { already = true; break; }
                pending_touch(context, false);
                if (!already)
                {
                    char msg[128];
                    std::snprintf(msg, sizeof(msg), "optB: PWT flip deferred to settled (loading) on %p (tick=%u)", context, g_update_ticks);
                    logf(msg);
                }
            }
            else if (cls_ok && !tf && char_owns(context, g_melee_2h))
            {   // dual-saber wielder whose active stance is not Tel-Rea's (silent path) - (re)arm
                pending_touch(context, false);
            }
            else
            {   // stance churn without a rifle grant: postpone a pending saber re-link until quiet
                for (std::size_t i = 0; i < kPendingMax; ++i)
                    if (g_pending_relink[i] == context && g_pending_name[i] == obj_name_ci(context) && !g_pending_revert[i])
                    { g_pending_quiet[i] = g_update_ticks; break; }
            }
        }
        else
        {
            // v2.6.0: nav revert moved out of this hot per-stance-call path into the settled
            // revert processing in on_update (same place the bespoke removal runs).
            const std::size_t f = flipped_find(context);
            if (flipped || f != kPendingMax)            // only chars WE flipped (this call or earlier)
            {
                const bool reverted = flip_pwt_back(context);
                if (f != kPendingMax) { g_flipped[f] = nullptr; g_flipped_name[f] = 0; }
                if (set_binding_category(context, g_cat_hum2hrifle))
                    logf("saber-stance: binding category restored (sabers off)");
                // bespoke removal deferred to the settled revert path (same broadcast risk)
                if (reverted)
                {
                    logf("saber-stance: saber unequipped - restored rifle identity, re-link queued");
                    pending_touch(context, true);
                }
            }
            // v2.16.0: vibro wielders (Weapon.Enemy.1HMelee) - track for a settled stance
            // re-cache; a mission spawn may have cached the rifle archetype before the CDO
            // write landed (the CDO only loads with the spawn itself). Idempotent heal.
            if (kEnableVibroArchFix && g_enemy_1h.tag_name.comparison_index != 0 &&
                char_owns(context, g_enemy_1h))
            {
                bool tracked = false;
                for (std::size_t i = 0; i < 8; ++i)
                    if (g_vibro_heal[i] == context && g_vibro_heal_name[i] == obj_name_ci(context))
                    { g_vibro_heal_quiet[i] = g_update_ticks; tracked = true; break; }
                if (!tracked)
                    for (std::size_t i = 0; i < 8; ++i)
                        if (g_vibro_heal[i] == nullptr)
                        {
                            g_vibro_heal[i] = context;
                            g_vibro_heal_name[i] = obj_name_ci(context);
                            g_vibro_heal_quiet[i] = g_update_ticks;
                            break;
                        }
            }
        }
    }

    auto resolve_and_hook() -> void
    {
        const auto module = ::GetModuleHandleW(nullptr);
        if (module == nullptr) { logf("refused: main module not found"); return; }
        g_base = reinterpret_cast<std::uintptr_t>(module);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(g_base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) { logf("refused: bad dos header"); return; }
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(g_base + static_cast<std::uintptr_t>(dos->e_lfanew));
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
            nt->FileHeader.TimeDateStamp != kPeTimestamp || nt->OptionalHeader.SizeOfImage != kImageSize)
        {
            logf("refused: unsupported game build");
            return;
        }
        if (!bytes_match(kDoesPartRva, kDoesPartBytes) || !bytes_match(kFilterRva, kFilterBytes) ||
            !bytes_match(kTagCopyRva, kTagCopyBytes) || !bytes_match(kTagDtorRva, kTagDtorBytes) ||
            !bytes_match(kTagAddRva, kTagAddBytes))
        {
            logf("refused: unexpected function bytes");
            return;
        }

        const auto ue4ss = ::GetModuleHandleW(L"UE4SS.dll");
        if (ue4ss == nullptr) { logf("refused: UE4SS.dll not found"); return; }
        g_fname_ctor = reinterpret_cast<FNameCtorFn>(::GetProcAddress(ue4ss, "??0FName@Unreal@RC@@QEAA@PEB_WW4EFindName@12@PEAX@Z"));
        g_resize_alloc = reinterpret_cast<ResizeAllocFn>(::GetProcAddress(ue4ss, "?ResizeAllocation@ForAnyElementType@?$TSizedHeapAllocator@$0CA@UFMemory@Unreal@RC@@@Unreal@RC@@QEAAXHH_K@Z"));
        if (g_fname_ctor == nullptr || g_resize_alloc == nullptr) { logf("refused: UE4SS export not found"); return; }

        for (std::size_t i = 0; i < kOfferCount; ++i)
            g_ids[i] = FPrimaryAssetId{make_fname(L"CustomizationPartDefinition", AddName), make_fname(kOfferParts[i], AddName)};

        for (std::size_t i = 0; i < kLendMax; ++i)
        {
            const FName n = make_fname(kLendTags[i], FindName);
            if (n.comparison_index != 0) g_lend[g_lend_count++] = FGameplayTag{n};
        }
        if (g_lend_count == 0) { logf("refused: no lend tag was found"); return; }

        // All-species armour: flag the CPD_H_Outfit_* tiles and resolve the human/structural
        // tags we lend only to them (kMandoAllSpecies). Non-fatal if any tag is missing.
        for (std::size_t i = 0; i < kOfferCount; ++i)
            g_is_outfit[i] = has_prefix(kOfferParts[i], STR("CPD_H_Outfit_"));
        // Vibrosword lane: flag the offered enemy weapon specs and resolve their B1 gate tag
        // (AddName - the tag registers with the game's own table once its data loads).
        for (std::size_t i = 0; i < kOfferCount; ++i)
            g_is_vibro[i] = has_prefix(kOfferParts[i], STR("CPD_WeaponSpec_Enemy_"));
        g_b1 = FGameplayTag{make_fname(STR("br.Customization.Part.Character.Class.Battledroid.B1"), AddName)};
        for (std::size_t i = 0; i < kOutfitLendMax; ++i)
        {
            const FName n = make_fname(kOutfitLendTags[i], FindName);
            if (n.comparison_index != 0) g_outfit_lend[g_outfit_lend_count++] = FGameplayTag{n};
        }

        g_does_part_orig = reinterpret_cast<DoesPartFn>(install_hook(g_base + kDoesPartRva, reinterpret_cast<void*>(&hook_does_part), 15, g_does_part_tr));
        if (g_does_part_orig == nullptr) { logf("refused: requirement hook failed"); return; }
        g_filter_orig = reinterpret_cast<FilterFn>(install_hook(g_base + kFilterRva, reinterpret_cast<void*>(&hook_filter), 16, g_filter_tr));
        if (g_filter_orig == nullptr) { remove_hook(g_does_part_tr); g_does_part_orig = nullptr; logf("refused: filter hook failed"); return; }

        // Hook the spec/talent/weapon lock tag-queries (see kMatchesRva). Non-fatal if it can't install.
        for (std::size_t i = 0; i < kNameMax; ++i)
        {
            const FName n = make_fname(kNameTags[i], FindName);
            if (n.comparison_index != 0) g_names[g_names_count++] = FGameplayTag{n};
        }
        g_uitype = FGameplayTag{make_fname(kUiTypeTag, FindName)};
        g_lightsaber = FGameplayTag{make_fname(kLightsaberTag, FindName)};
        if (g_names_count != 0 && bytes_match(kMatchesRva, kMatchesBytes))
            g_matches_orig = reinterpret_cast<MatchesFn>(install_hook(g_base + kMatchesRva, reinterpret_cast<void*>(&hook_matches), 14, g_matches_tr));

        // 1.3: Force-meditation equip lend + grant plumbing. History: the v1.1.1-era hooks were
        // never installed in any shipped build ("did nothing useful") because no character other
        // than Tel-Rea ever OWNED the items - the eligibility lend alone shows nothing. With the
        // grant pass (try_grant_meditations, driven from on_update) the lend becomes the real
        // gate, so it is installed now. hook_can_equip stays uninstalled on purpose:
        // CanCharacterEquipItemType itself calls RequirementsMet, so the one narrow lend covers
        // both armory tile eligibility and the equip click.
        if (kGrantMeditations)
        {
            if (g_bespoke.tag_name.comparison_index == 0)
                g_bespoke = FGameplayTag{make_fname(kBespokeTag, AddName)};
            if (g_exotic.tag_name.comparison_index == 0)
                g_exotic = FGameplayTag{make_fname(kExoticTag, AddName)};
            if (g_melee_spec.tag_name.comparison_index == 0)
                g_melee_spec = FGameplayTag{make_fname(STR("br.Customization.Part.Character.Specializations.Weapon.Melee"), AddName)};
            if (g_melee_2h.tag_name.comparison_index == 0)
                g_melee_2h = FGameplayTag{make_fname(STR("br.Customization.Part.Character.Specializations.Weapon.Melee.2H"), AddName)};
            if (g_melee_1h.tag_name.comparison_index == 0)
                g_melee_1h = FGameplayTag{make_fname(STR("br.Customization.Part.Character.Specializations.Weapon.Melee.1H"), AddName)};
            if (g_deflect_name.tag_name.comparison_index == 0)
                g_deflect_name = FGameplayTag{make_fname(kDeflectNameTag, AddName)};
            if (g_deflect_name_tel.tag_name.comparison_index == 0)
                g_deflect_name_tel = FGameplayTag{make_fname(kDeflectNameTagTel, AddName)};
            if (g_uifrag_class.comparison_index == 0)
                g_uifrag_class = make_fname(STR("BitReactorUIDataFragment"), AddName);   // import-name fragment walk
            if (g_enemy_1h.tag_name.comparison_index == 0)
                g_enemy_1h = FGameplayTag{make_fname(STR("br.Customization.Part.Character.Specializations.Weapon.Enemy.1HMelee"), AddName)};
            // v2.17.0: resolve the import targets (CPD object leaf names) + icon tags once,
            // and arm the read-time UIData fragment hook (see kFindFragRva block).
            for (std::size_t i = 0; i < kImportNameCount; ++i)
            {
                const wchar_t* leaf = ::wcsrchr(kImportNames[i].path, L'.');
                if (leaf != nullptr && g_imp_target[i] == 0)
                {
                    const FName n = make_fname(leaf + 1, AddName);
                    g_imp_target[i] = static_cast<std::uint64_t>(n.comparison_index) |
                                      (static_cast<std::uint64_t>(n.number) << 32);
                }
                if (kImportNames[i].icon != nullptr && g_imp_icon[i] == 0)
                {
                    const FName ic = make_fname(kImportNames[i].icon, AddName);
                    g_imp_icon[i] = static_cast<std::uint64_t>(ic.comparison_index) |
                                    (static_cast<std::uint64_t>(ic.number) << 32);
                }
            }
            if (kEnableImportNames && bytes_match(kFindFragRva, kFindFragBytes))
                g_findfrag_orig = reinterpret_cast<FindFragFn>(install_hook(g_base + kFindFragRva, reinterpret_cast<void*>(&hook_findfrag_uidata), 16, g_findfrag_tr));
            if (bytes_match(kHasAllRva, kHasAllBytes))
                g_hasall_orig = reinterpret_cast<HasAllFn>(install_hook(g_base + kHasAllRva, reinterpret_cast<void*>(&hook_hasall), 15, g_hasall_tr));
            g_med_grant_ok = bytes_match(kGrantItemRva, kGrantItemBytes) &&
                             bytes_match(kGetHqInventoryRva, kGetHqInventoryBytes) &&
                             bytes_match(kGetTotalOwnedRva, kGetTotalOwnedBytes) &&
                             bytes_match(kZConstructUtilityItemRva, kZConstructUtilityItemBytes) &&
                             bytes_match(kStaticLoadObjectRva, kStaticLoadObjectBytes);
        }

        // 1.3: Force Jump - arm the Jedi nav-filter load path (the writes themselves ride the
        // saber-stance machinery: apply on melee wielders, revert on unequip).
        if (kEnableForceJump)
            g_nav_ok = bytes_match(kUClassStaticClassRva, kUClassStaticClassBytes) &&
                       bytes_match(kStaticLoadObjectRva, kStaticLoadObjectBytes);

        // 1.4 v2.11.0: Tel-rig leader mesh lend - arm the mesh load path (apply/revert ride the
        // same saber-stance pending machinery as the nav lend).
        if (kEnableLeaderSwap)
            g_lswap_ok = bytes_match(kZConstructSkelMeshRva, kZConstructSkelMeshBytes) &&
                         bytes_match(kStaticLoadObjectRva, kStaticLoadObjectBytes);

        // v2.11.4 diagnostic: instrument the GearKit notify chokepoint (remove after diagnosis).
        if (kEnableGkDiag && bytes_match(kGkFindRva, kGkFindBytes))
            g_gkfind_orig = reinterpret_cast<GkFindFn>(install_hook(g_base + kGkFindRva, reinterpret_cast<void*>(&hook_gkfind), 15, g_gkfind_tr));

        // Den cold-load crash guard: synchronous scrub hooks at the two compaction entry points.
        if (kEnableDenLoadFix)
        {
            if (bytes_match(kRemoveDelegRva, kRemoveDelegBytes))
                g_remove_deleg_orig = reinterpret_cast<RemoveDelegFn>(install_hook(g_base + kRemoveDelegRva, reinterpret_cast<void*>(&hook_remove_deleg), 15, g_remove_deleg_tr));
            if (bytes_match(kActorItCtorRva, kActorItCtorBytes))
                g_actorit_orig = reinterpret_cast<ActorItCtorFn>(install_hook(g_base + kActorItCtorRva, reinterpret_cast<void*>(&hook_actorit_ctor), 14, g_actorit_tr));
        }

        // EXPERIMENTAL (increment 1): the read-only resolution probe now runs from hook_filter (which
        // reliably fires while the armory builds its part lists). No separate trigger hook needed.

        // Lift the in-game "Change Specialization" padlock.
        const std::uint8_t ret_void[1] = {0xC3};              // ret
        const std::uint8_t ret_false[3] = {0x32, 0xC0, 0xC3};  // xor al,al ; ret
        const std::uint8_t zero_eax[7] = {0x31, 0xC0, 0x90, 0x90, 0x90, 0x90, 0x90}; // xor eax,eax ; nop*5
        const std::uint8_t xor_eax_nop[3] = {0x31, 0xC0, 0x90};                       // xor eax,eax ; nop
        const bool unlock_add = apply_ret_patch(kAddLockRva, kAddLockBytes, ret_void, 1);
        const bool unlock_frag = apply_ret_patch(kGetSlotLockedRva, kGetSlotLockedBytes, ret_false, 3);
        const bool unlock_ui = apply_ret_patch(kUiLockReadRva, kUiLockReadBytes, zero_eax, 7);
        const bool unlock_slot = apply_ret_patch(kSlotLockLookupRva, kSlotLockLookupBytes, xor_eax_nop, 3);
        // v2.19.0 BUILD 1 rider: surface every class-lock neutralizer's install result so a silently
        // drifted byte-signature in a shipped build is visible in the log instead of re-locking
        // classes invisibly. Expect all five = 1; any 0 means that RVA/signature needs refreshing.
        {
            char lockmsg[192];
            std::snprintf(lockmsg, sizeof(lockmsg),
                          "class-lock neutralizers: matches_hook=%d add=%d frag=%d ui=%d slot=%d (all 5 must be 1)",
                          g_matches_orig != nullptr ? 1 : 0,
                          unlock_add ? 1 : 0, unlock_frag ? 1 : 0, unlock_ui ? 1 : 0, unlock_slot ? 1 : 0);
            logf(lockmsg);
        }

        // Helmet voice modulator (ported from Sternab, MIT). Resolve reflection exports + the voice donor
        // id + the four helmets, then hook the voiceover-preset solver. Non-fatal if anything is missing.
        if (kEnableHelmetVoice)
        {
            g_get_value_ptr = reinterpret_cast<GetValuePtrFn>(::GetProcAddress(ue4ss, "?GetValuePtrByPropertyNameInChain@UObject@Unreal@RC@@QEAAPEAXPEB_W@Z"));
            g_is_real = reinterpret_cast<IsRealFn>(::GetProcAddress(ue4ss, "?IsReal@UObject@Unreal@RC@@SA_NPEBX@Z"));
            g_voice_src_id = FPrimaryAssetId{make_fname(L"CustomizationPartDefinition", AddName), make_fname(L"CPD_H_Outfit_Clo008_HELM_TintB", AddName)};
            for (std::size_t i = 0; i < kOfferCount; ++i)
                g_voice_helmet[i] = g_is_outfit[i] && has_suffix(kOfferParts[i], STR("_HELM"))
                    && (has_prefix(kOfferParts[i], STR("CPD_H_Outfit_Man"))
                        || has_prefix(kOfferParts[i], STR("CPD_H_Outfit_Cly")));   // Jedi hoods must NOT get the clone voice RTPC
            if (g_get_value_ptr != nullptr && bytes_match(kSolveVoiceRva, kSolveVoiceBytes) &&
                bytes_match(kGetSlotInstanceRva, kGetSlotInstanceBytes) && bytes_match(kGetPartDefFromIdRva, kGetPartDefFromIdBytes))
                g_solve_voice_orig = reinterpret_cast<SolveVoiceFn>(install_hook(g_base + kSolveVoiceRva, reinterpret_cast<void*>(&hook_solve_voice), 19, g_solve_voice_tr));
        }

        // Helmet clipping fit (ported from Sternab, MIT). Resolve the head bone name + target mesh names
        // (AddName so they match even before the meshes load), then hook the two palette builders.
        if (kEnableHelmetFit)
        {
            g_head_name = make_fname(L"head", AddName);
            for (std::size_t i = 0; i < kFitMeshCount; ++i) g_fit_names[i] = make_fname(kFitMeshNames[i], AddName);
            if (bytes_match(kUpdateRefRva, kUpdateRefBytes) && bytes_match(kUpdatePrevRefRva, kUpdateRefBytes))
            {
                g_update_ref_orig = reinterpret_cast<UpdateRefFn>(install_hook(g_base + kUpdateRefRva, reinterpret_cast<void*>(&hook_update_ref), 14, g_update_ref_tr));
                g_update_prev_orig = reinterpret_cast<UpdateRefFn>(install_hook(g_base + kUpdatePrevRefRva, reinterpret_cast<void*>(&hook_update_prev), 14, g_update_prev_tr));
            }
        }

        // Saber stance fix: lightsaber wielders get the saber (not rifle) stance/grip. Non-fatal if missing.
        if (kEnableSaberStance && bytes_match(kCacheStanceRva, kCacheStanceBytes))
        {
            // Resolve the UE4SS SDK exports used by the deferred anim re-link.
            if (g_get_value_ptr == nullptr) g_get_value_ptr = reinterpret_cast<GetValuePtrFn>(::GetProcAddress(ue4ss, "?GetValuePtrByPropertyNameInChain@UObject@Unreal@RC@@QEAAPEAXPEB_W@Z"));
            if (g_is_real == nullptr) g_is_real = reinterpret_cast<IsRealFn>(::GetProcAddress(ue4ss, "?IsReal@UObject@Unreal@RC@@SA_NPEBX@Z"));
            g_process_event = reinterpret_cast<ProcessEventFn>(::GetProcAddress(ue4ss, "?ProcessEvent@UObject@Unreal@RC@@QEAAXPEAVUFunction@23@PEAX@Z"));
            g_get_func = reinterpret_cast<GetFuncFn>(::GetProcAddress(ue4ss, "?GetFunctionByNameInChain@UObject@Unreal@RC@@QEAAPEAVUFunction@23@PEB_W@Z"));
            g_static_find = reinterpret_cast<StaticFindFn>(::GetProcAddress(ue4ss, "?StaticFindObject_InternalSlow@UObjectGlobals@Unreal@RC@@YAPEAVUObject@23@PEAVUClass@23@PEAV423@PEB_W_N@Z"));
            g_load_arch_ok = bytes_match(kStaticLoadObjectRva, kStaticLoadObjectBytes) &&
                             bytes_match(kZConstructArchClassRva, kZConstructArchClassBytes);
            // AddName (not FindName): the Animation.PrimaryWeaponType.* tags aren't registered yet at
            // mod-load; AddName resolves to the same global FName the game's tag uses once it registers.
            g_pwt_rifle  = FGameplayTag{make_fname(STR("Animation.PrimaryWeaponType.Rifle"), AddName)};
            g_pwt_other  = FGameplayTag{make_fname(STR("Animation.PrimaryWeaponType.Other"), AddName)};
            g_melee_spec = FGameplayTag{make_fname(STR("br.Customization.Part.Character.Specializations.Weapon.Melee"), AddName)};
            g_melee_2h   = FGameplayTag{make_fname(STR("br.Customization.Part.Character.Specializations.Weapon.Melee.2H"), AddName)};
            g_cat_telrea     = FGameplayTag{make_fname(STR("BitReactor.Ability.Presentationtags.Binding.AnimationCategory.TelRea"), AddName)};
            g_cat_hum2hrifle = FGameplayTag{make_fname(STR("BitReactor.Ability.Presentationtags.Binding.AnimationCategory.Humanoid.2HRifle"), AddName)};
            if (g_bespoke.tag_name.comparison_index == 0)
                g_bespoke = FGameplayTag{make_fname(kBespokeTag, AddName)};   // Tel-Rea's bespoke class identity
            if (g_exotic.tag_name.comparison_index == 0)
                g_exotic = FGameplayTag{make_fname(kExoticTag, AddName)};     // Padawan-class marker (meditation lend)
            if (g_deflect_name.tag_name.comparison_index == 0)
                g_deflect_name = FGameplayTag{make_fname(kDeflectNameTag, AddName)};   // deflection whitelist lend (1H lane)
            if (g_deflect_name_tel.tag_name.comparison_index == 0)
                g_deflect_name_tel = FGameplayTag{make_fname(kDeflectNameTagTel, AddName)};   // deflection whitelist lend (2H lane)
            for (std::size_t i = 0; i < kSasCount; ++i) g_sas_names[i] = make_fname(kSasNameStrs[i], AddName);
            if (g_pwt_rifle.tag_name.comparison_index != 0 && g_pwt_other.tag_name.comparison_index != 0 &&
                g_melee_spec.tag_name.comparison_index != 0)
                g_cache_stance_orig = reinterpret_cast<CacheStanceFn>(install_hook(g_base + kCacheStanceRva, reinterpret_cast<void*>(&hook_cache_stance), 14, g_cache_stance_tr));
        }

        // 1.4: global saber stow sockets (see the kEnableStowSockets block). Non-fatal if missing.
        if (kEnableStowSockets)
        {
            if (g_get_value_ptr == nullptr)
                g_get_value_ptr = reinterpret_cast<GetValuePtrFn>(::GetProcAddress(ue4ss, "?GetValuePtrByPropertyNameInChain@UObject@Unreal@RC@@QEAAPEAXPEB_W@Z"));
            // AddName: the bone/socket names must match the FNames the loader registers later
            bool names_ok = true;
            for (std::size_t k = 0; k < kServeCount; ++k)
            {
                g_serve_name[k] = make_fname(kServeSocketNames[k], AddName);
                g_serve_bone[k] = make_fname(kServeBoneNames[k], AddName);
                if (g_serve_name[k].comparison_index == 0 || g_serve_bone[k].comparison_index == 0) names_ok = false;
            }
            const bool ok_fsai = bytes_match(kFindSocketAndIndexRva, kFindSocketAndIndexBytes);
            const bool ok_fsi  = bytes_match(kFindSocketInfoRva, kFindSocketInfoBytes);
            const bool ok_sco  = bytes_match_or_hooked(kStaticConstructRva, kStaticConstructBytes);  // UE4SS hooks this one
            const bool ok_zc   = bytes_match(kZConstructSocketRva, kZConstructSocketBytes);
            g_stow_ok = g_get_value_ptr != nullptr && names_ok &&
                        ok_fsai && ok_fsi && ok_sco && ok_zc;
            if (!g_stow_ok)
            {
                char msg[160];
                std::snprintf(msg, sizeof(msg), "stow-sockets: verification detail fsai=%d fsi=%d sco=%d zc=%d gvp=%d",
                              ok_fsai ? 1 : 0, ok_fsi ? 1 : 0, ok_sco ? 1 : 0, ok_zc ? 1 : 0,
                              g_get_value_ptr != nullptr ? 1 : 0);
                logf(msg);
            }
            if (g_stow_ok)
            {
                g_findsock_orig = reinterpret_cast<FindSockIdxFn>(install_hook(g_base + kFindSocketAndIndexRva, reinterpret_cast<void*>(&hook_find_socket_idx), 14, g_findsock_tr));
                g_findsockinfo_orig = reinterpret_cast<FindSockInfoFn>(install_hook(g_base + kFindSocketInfoRva, reinterpret_cast<void*>(&hook_find_socket_info), 15, g_findsockinfo_tr));
                if (g_findsock_orig == nullptr || g_findsockinfo_orig == nullptr)
                {   // half-installed would answer DoesSocketExist and GetSocketTransform inconsistently
                    remove_hook(g_findsock_tr); remove_hook(g_findsockinfo_tr);
                    g_findsock_orig = nullptr; g_findsockinfo_orig = nullptr;
                    g_stow_ok = false;
                    logf("stow-sockets: hook install failed - feature off");
                }
            }
            else
                logf("stow-sockets: RVA/byte verification failed - feature off");
        }

        // 1.4: translation-retarget lend (see the kEnableRetargetFix block). Pure reflection +
        // verified layout reads; the only hard dependency is the property-pointer export.
        if (kEnableRetargetFix)
        {
            if (g_get_value_ptr == nullptr)
                g_get_value_ptr = reinterpret_cast<GetValuePtrFn>(::GetProcAddress(ue4ss, "?GetValuePtrByPropertyNameInChain@UObject@Unreal@RC@@QEAAPEAXPEB_W@Z"));
            for (std::size_t k = 0; k < kRetargetExclCount; ++k)
                g_rt_excl[k] = make_fname(kRetargetExcl[k], AddName);
            g_rt_ok = g_get_value_ptr != nullptr && g_rt_excl[0].comparison_index != 0;
        }

        // v2.18.2: phase-discriminator arm (fixes hub_now() saying "hub" in missions - the
        // v2.14.1+ silent-withhold bug that killed leader swap/deflect/assist in missions).
        g_phase_ok = bytes_match(kIsTacticalPhaseRva, kIsTacticalPhaseBytes) &&
                     bytes_match(kIsHqPhaseRva, kIsHqPhaseBytes);
        if (!g_phase_ok) logf("phase: native verification failed - hub/mission detection on legacy probe");

        // v2.18.0: assist-push feature arm (see the kEnableAssistPush block). Grant/remove ride
        // ProcessEvent UFUNCTIONs; the only byte-verified natives are the loader pair.
        if (kEnableAssistPush)
        {
            g_assist_load_ok = bytes_match(kStaticLoadObjectRva, kStaticLoadObjectBytes) &&
                               bytes_match(kZConstructAbilitySetRva, kZConstructAbilitySetBytes);
            g_assist_ok = g_process_event != nullptr && g_get_func != nullptr && g_static_find != nullptr &&
                          g_is_real != nullptr &&
                          g_melee_spec.tag_name.comparison_index != 0 &&
                          g_melee_2h.tag_name.comparison_index != 0;
            if (!g_assist_ok) logf("assist-push: prerequisites missing - feature off");
            g_fname_tostr_ok = bytes_match(kFNameToStringRva, kFNameToStringBytes);
            if (!g_fname_tostr_ok) logf("assist-diag: FName::ToString verification failed - dumps show '?'");
        }

        char ready[320];
        std::snprintf(ready, sizeof(ready), "ready build=24874058 ver=2.22.0 offer=%zu phase=%d lend=%zu unlock_query=%d all_species=%d outfit_lend=%zu voice=%d fit=%d saber=%d relink=%d meds=%d fjump=%d denfix=%d stow=%d rt=%d ik=%d lswap=%d deflect=%d assist=%d clsfix=%d",
                      kOfferCount, g_phase_ok ? 1 : 0, g_lend_count, g_matches_orig ? 1 : 0, kMandoAllSpecies ? 1 : 0, g_outfit_lend_count, g_solve_voice_orig ? 1 : 0,
                      (g_update_ref_orig && g_update_prev_orig) ? 1 : 0,
                      g_cache_stance_orig ? (g_load_arch_ok ? 2 : 1) : 0,
                      (g_process_event && g_get_func) ? 1 : 0,
                      (g_hasall_orig ? 1 : 0) + (g_med_grant_ok ? 2 : 0),   // meds: bit0 lend hook, bit1 grant path -> 3 = fully armed
                      (kEnableForceJump && g_nav_ok) ? 1 : 0,
                      (kEnableDenLoadFix ? 1 : 0) + (g_remove_deleg_orig ? 2 : 0) + (g_actorit_orig ? 4 : 0),   // denfix: 7 = frame scan + both sync hooks
                      (g_findsock_orig != nullptr && g_findsockinfo_orig != nullptr) ? 1 : 0,   // stow: 1 = both lookup hooks armed
                      (kEnableRetargetFix && g_rt_ok) ? 1 : 0,   // rt: 1 = translation-retarget lend armed
                      (kEnableIkLend && g_get_value_ptr != nullptr && g_static_find != nullptr) ? 1 : 0,   // ik: 1 = hand-IK lend armed
                      (kEnableLeaderSwap && g_lswap_ok) ? 1 : 0,   // lswap: 1 = Tel-rig leader lend armed
                      (kEnableDeflectLend && g_deflect_name.tag_name.comparison_index != 0 &&
                       g_deflect_name_tel.tag_name.comparison_index != 0) ? 1 : 0,   // deflect: 1 = lane-matched whitelist name lend armed
                      (kEnableAssistPush && g_assist_ok) ? (g_assist_load_ok ? 2 : 1) : 0,   // assist: 2 = grant + loader armed, 1 = find-only
                      kEnableClassLockFix ? 1 : 0);   // clsfix: 1 = class-lock hub strip active (0 = gated off for the den build)
        logf(ready);
    }

    // ---- CppUserModBase ABI replica (object size 0xC0; vtable order verbatim) ----
    class ModBase
    {
      public:
        virtual ~ModBase() {}
        virtual void on_update() {}
        virtual void on_unreal_init() {}
        virtual void on_ui_init() {}
        virtual void on_program_start() {}
        virtual void vt_lua_start_dep_named(void*, void*, void*, void*, void*) {}
        virtual void vt_lua_start_dep(void*, void*, void*, void*) {}
        virtual void vt_lua_stop_dep_named(void*, void*, void*, void*, void*) {}
        virtual void vt_lua_stop_dep(void*, void*, void*, void*) {}
        virtual void on_dll_load(void*) {}
        virtual void render_tab() {}
        virtual void vt_lua_start_named(void*, void*, void*, void*, void*) {}
        virtual void vt_lua_start(void*, void*, void*, void*) {}
        virtual void vt_lua_stop_named(void*, void*, void*, void*, void*) {}
        virtual void vt_lua_stop(void*, void*, void*, void*) {}
        virtual void on_cpp_mods_loaded() {}

      protected:
        std::vector<std::shared_ptr<void>> GUITabs{};

      public:
        std::wstring ModName{};
        std::wstring ModVersion{};
        std::wstring ModDescription{};
        std::wstring ModAuthors{};
        std::wstring ModIntendedSDKVersion{};
    };
    static_assert(sizeof(ModBase) == 0xC0, "CppUserModBase object must be 0xC0 bytes");

    class ZCUnlockedMod final : public ModBase
    {
      public:
        ZCUnlockedMod()
        {
            ModName = L"ZCUnlocked";
            ModVersion = L"2.9.3";
            ModDescription = L"Hero classes, specs, weapons and Mandalorian armour (any species) - freely swappable, helmet voice + clipping fit";
            ModAuthors = L"Alex";
        }
        ~ZCUnlockedMod() override
        {
            remove_hook(g_findsockinfo_tr);
            remove_hook(g_findsock_tr);
            remove_hook(g_actorit_tr);
            remove_hook(g_remove_deleg_tr);
            remove_hook(g_cache_stance_tr);
            remove_hook(g_findfrag_tr);
            remove_hook(g_update_prev_tr);
            remove_hook(g_update_ref_tr);
            remove_hook(g_solve_voice_tr);
            remove_hook(g_getgk_tr);
            remove_hook(g_hasall_tr);
        remove_hook(g_gkfind_tr);
            remove_hook(g_can_equip_tr);
            remove_hook(g_matches_tr);
            remove_hook(g_filter_tr);
            remove_hook(g_does_part_tr);
        }
        void on_unreal_init() override { resolve_and_hook(); }

        // Per-frame (game thread, between frames): drive the quiet-gated saber-stance re-links.
        // An entry re-links only after kQuietTicks with NO stance-setup calls for its char - the
        // "load settled" moment where the manual re-link is proven to stick. If Rifle sneaks back
        // (before, during, or after the re-link) the cycle re-arms instead of finishing, so the
        // loop always terminates in the proven-good state.
        void on_update() override
        {
            ++g_update_ticks;
            // Den cold-load crash guard: early-detection frame scan (the synchronous hooks at
            // the compaction entry points are the real guarantee - see kEnableDenLoadFix block).
            if (kEnableDenLoadFix) scrub_gworld();
            // 1.3: grant the Force meditations once a world context exists (captured ONLY from
            // hook_filter = the armory/creator opening) and we are in the hub (HQ inventory
            // actor exists). This is the exact v2.2/v2.3 recipe that ran for weeks without a
            // crash: the subsystem ctx is process-lifetime-valid, so GetInventory is safe even
            // if an attempt lands during a later load (v2.4.0's crashes came from CHARACTER
            // contexts dying mid-load, not from the attempt timing - see the v2.6 review notes).
            // v2.6.1: the stance-quiet gate is GONE - armory browsing churns stance calls and
            // starved the grant indefinitely (and the empty pool then let the game strip
            // equipped meditations via its HasItemID validation).
            // v2.6.2: no context capture at all - the grant reads the engine's GWorld global
            // (vtable-verified) each attempt. Attempts run every ~4s from session start; they
            // no-op in menus (GWorld null-checked) and missions (no HQ inventory actor).
            if (kGrantMeditations && g_med_grant_ok && !g_meds_granted &&
                g_med_attempts < kMedAttemptCap && (g_update_ticks & 255u) == 0)
                try_grant_meditations();
            // v2.15.0: imported-part display names - settled hub windows, capped retries (the
            // CPD only becomes findable once the armory enumerates it; see kImportNames).
            if (kEnableImportNames && (g_update_ticks & 255u) == 128u && g_import_name_tries < 40)
            {
                bool pending_names = false;
                for (std::size_t i = 0; i < kImportNameCount; ++i)
                    if (!g_import_named[i]) { pending_names = true; break; }
                if (pending_names && hub_now()) { ++g_import_name_tries; apply_import_names(); }
            }
            // v2.16.0: pak-free runtime tweaks - find-only + memory writes on loaded objects,
            // run forever (idempotent + silent when nothing changed) so GC reloads re-heal.
            // v2.16.1: NO quiet gate + 64-tick cadence. The original quiet gate repeated the
            // v2.6.0 starvation mistake: armory browsing churns stance calls, so the balance
            // write could lose the race against the player's equip click (equip SNAPSHOTS the
            // attribute values) = Alex's "stats don't always load correctly". These passes do
            // no world walks and no loads - churn-safe; ~1s cadence closes the race.
            if ((kEnableRuntimeBalance || kEnableVibroArchFix || kEnableVibroUiType) &&
                (g_update_ticks & 63u) == 32u)
            {
                if (kEnableRuntimeBalance) apply_runtime_balance();
                if (kEnableVibroArchFix) apply_vibro_arch_fix();
                if (kEnableVibroUiType) apply_vibro_uitype();
                if (kEnableImportNames) precompute_import_texts();   // v2.17.0: arms the read-time hook
            }
            // v2.16.0: settled vibro stance heal (see hook_cache_stance tracking)
            if (kEnableVibroArchFix)
                for (std::size_t i = 0; i < 8; ++i)
                {
                    void* ch = g_vibro_heal[i];
                    if (ch == nullptr) continue;
                    if (g_update_ticks - g_vibro_heal_quiet[i] < kQuietTicks) continue;
                    if (g_is_real != nullptr && !g_is_real(ch)) { g_vibro_heal[i] = nullptr; continue; }
                    if (obj_name_ci(ch) != g_vibro_heal_name[i]) { g_vibro_heal[i] = nullptr; continue; }
                    if (!char_owns(ch, g_enemy_1h)) { g_vibro_heal[i] = nullptr; continue; }   // vibro swapped off
                    if (fix_vibro_stance(ch)) g_vibro_heal[i] = nullptr;
                    else g_vibro_heal_quiet[i] = g_update_ticks;   // retry after another quiet window
                }
            // 1.4: build the stow sockets once, in a stance-quiet window. Construction is
            // memory-only (native class object, no asset loads, no world walks); the quiet gate
            // just keeps even that out of load churn. First eligible window is usually the main
            // menu. The lookup hooks serve nothing until the sockets publish.
            if (kEnableStowSockets && g_stow_ok && !g_stow_built && g_stow_fails < kStowFailCap &&
                (g_update_ticks & 255u) == 128u && g_update_ticks - g_last_stance_tick >= kWorldQuietTicks)
                build_stow_sockets();
            if (kEnableStowSockets && g_stow_built && (g_update_ticks & 2047u) == 0)
            {   // hit counters are bumped lock-free in the hooks; report growth from here only
                unsigned h[kServeCount], nh[kServeCount], total = 0;
                for (std::size_t k = 0; k < kServeCount; ++k)
                {
                    h[k] = g_stow_hits[k].load(std::memory_order_relaxed);
                    nh[k] = g_stow_native[k].load(std::memory_order_relaxed);
                    total += h[k] + nh[k];
                }
                if (total != g_stow_hits_logged)
                {
                    g_stow_hits_logged = total;
                    char msg[192];
                    std::snprintf(msg, sizeof(msg),
                                  "saber-sockets: served wpl=%u wpr=%u aim=%u | native wpl=%u wpr=%u aim=%u (tick=%u)",
                                  h[0], h[1], h[2], nh[0], nh[1], nh[2], g_update_ticks);
                    logf(msg);
                }
            }
            // retarget-lend retry: pending entries clear once their re-link completes, so also
            // re-run the lend for every tracked wielder each quiet stretch (covers the leader
            // mesh asset streaming in late and outfit swaps; the lend is idempotent and cheap).
            if (kEnableRetargetFix && g_rt_ok && (g_update_ticks & 511u) == 384u &&
                g_update_ticks - g_last_stance_tick >= kWorldQuietTicks && g_is_real != nullptr)
                for (std::size_t i = 0; i < kPendingMax; ++i)
                    if (g_flipped[i] != nullptr && g_is_real(g_flipped[i]) &&
                        obj_name_ci(g_flipped[i]) == g_flipped_name[i])
                        retarget_lend(g_flipped[i]);
            // 1.4 v2.10.0: hand-IK retarget lend - patch Tel's SOURCE skeleton whenever it is
            // resident (find-only + byte write, no loads, no world walks; idempotent, so asset
            // reloads across missions re-patch themselves each quiet stretch).
            if (kEnableIkLend && (g_update_ticks & 511u) == 384u &&
                g_update_ticks - g_last_stance_tick >= kWorldQuietTicks)
                ik_retarget_lend();
            // v2.11.1: leader-swap self-heal - the pending pass runs once per stance change and
            // can miss (asset not yet loadable, mesh not resolved); re-apply for every tracked
            // dual-saber wielder each quiet stretch (idempotent: already-swapped chars early-out).
            if (kEnableLeaderSwap && g_lswap_ok && (g_update_ticks & 511u) == 384u &&
                g_update_ticks - g_last_stance_tick >= kWorldQuietTicks && g_is_real != nullptr)
            {
                // v2.14.1: self-heal is mission-only too; in the hub it actively reverts any
                // swapped pawn (den pose fix - see the pending-branch comment).
                const bool heal_hub = hub_now();
                for (std::size_t i = 0; i < kPendingMax; ++i)
                    if (g_flipped[i] != nullptr && g_is_real(g_flipped[i]) &&
                        obj_name_ci(g_flipped[i]) == g_flipped_name[i])
                    {
                        if (!heal_hub) apply_leader_swap(g_flipped[i], g_meds_granted);
                        else           revert_leader_swap(g_flipped[i]);
                    }
            }
            // v2.18.0: assist-push self-heal - the pending pass can run before the teamwork
            // package resolves; re-apply for tracked wielders each quiet stretch (idempotent:
            // a char already holding the right lane early-outs on the slot check).
            if (kEnableAssistPush && g_assist_ok && (g_update_ticks & 511u) == 384u &&
                g_update_ticks - g_last_stance_tick >= kWorldQuietTicks && g_is_real != nullptr)
            {
                const bool heal_hub = hub_now();
                for (std::size_t i = 0; i < kPendingMax; ++i)
                    if (g_flipped[i] != nullptr && g_is_real(g_flipped[i]) &&
                        obj_name_ci(g_flipped[i]) == g_flipped_name[i])
                    {
                        if (!heal_hub) apply_assist_push(g_flipped[i]);
                        else           revert_assist_push(g_flipped[i]);
                    }
            }
            // v2.18.4: fast assist diagnostics + combat-join re-strip (every ~2s in missions;
            // NO quiet gate - the pass is find-only + ProcessEvent reads, no loads).
            if (kEnableAssistPush && g_assist_ok && (g_update_ticks & 127u) == 64u && !hub_now())
                assist_diag_pass();
            // v2.19.0 BUILD 1 (class-lock fix): while in the hub, strip any lent Info.Name tag off
            // EVERY registered char (not just those still in the relink queue) so a leftover lend can
            // never trip the ANY(Info.Name) spec/talent lock in the armory. ~0.5s cadence.
            // v2.21.0: settled-gated (grant_world() != nullptr = world has begun play + actors
            // present) so the strip's ASC broadcast never fires during a hub LOAD - hub_now() alone
            // returns true during loading too, which is the broadcast-during-load pattern behind the
            // den crash. The armory (where the lock is seen) is only reachable in a live hub anyway.
            if (kEnableClassLockFix && kEnableDeflectLend && (g_update_ticks & 31u) == 16u &&
                grant_world() != nullptr && hub_now())
                strip_lent_names_in_hub();
            for (std::size_t i = 0; i < kPendingMax; ++i)
            {
                void* ch = g_pending_relink[i];
                if (ch == nullptr) continue;
                if (g_update_ticks - g_pending_quiet[i] < kQuietTicks) continue;               // stance system not quiet yet
                if (g_pending_last[i] != 0 && g_update_ticks - g_pending_last[i] < kMinRelinkGapTicks) continue;
                if (g_is_real != nullptr && !g_is_real(ch)) { g_pending_relink[i] = nullptr; continue; }
                if (obj_name_ci(ch) != g_pending_name[i]) { g_pending_relink[i] = nullptr; continue; }   // address recycled by another object
                if (!g_pending_revert[i] && !char_owns(ch, g_melee_spec)) { g_pending_relink[i] = nullptr; continue; }   // saber gone; revert (if any) is queued by the hook
                if (!g_pending_revert[i] && char_owns(ch, g_pwt_rifle))
                {   // Rifle came back without a stance call - flip again and wait out another quiet window
                    flip_pwt(ch);
                    g_pending_quiet[i] = g_update_ticks;
                    char msg[128];
                    std::snprintf(msg, sizeof(msg), "saber-stance: rifle re-granted silently - re-flipped, waiting (tick=%u)", g_update_ticks);
                    logf(msg);
                    continue;
                }
                if (!g_pending_revert[i])
                {
                    // v2.6.0: the nav-filter LOAD is additionally gated on the meditation grant
                    // having succeeded - that only ever happens in a fully-settled hub, so the
                    // first sync-load of the filter package can never run under a loading screen
                    // (v2.4.x loaded it in "settled" windows that were still mid-load). Missions
                    // afterwards resolve it via the cheap find path.
                    apply_jedi_nav(ch, g_meds_granted);
                    retarget_lend(ch);   // 1.4: Tel-style translation retargeting for dual-saber wielders
                    // v2.14.1: the leader swap is MISSION-ONLY. Its proxy re-seed installs the
                    // COMBAT weapon-hold pose table (PxTable_Wep_2HSaber) - on den pawns that is
                    // the persistent armed-saber pose Alex reported (the den log showed the swap
                    // applying there; the 2.13.2 name-tag gate alone did not clear it). The den
                    // needs none of the strike machinery; mission pawns are fresh spawns that get
                    // the swap through this same branch (in_hub false there).
                    const bool in_hub = hub_now();
                    if (!in_hub)
                        apply_leader_swap(ch, g_meds_granted);   // v2.11.0: Tel-rig null leader (THE strike fix)
                    else
                        revert_leader_swap(ch);   // hub: undo any swap this pawn got earlier in the session
                    // v2.13.0/1: deflection whitelist lend - BP_BaseProjectile/SM_UnitReaction only
                    // present deflections for owners of Anakin/Tel-Rea/Trilla name tags, and the
                    // name also picks the deflect anim family (see kDeflectNameTag block), so it is
                    // matched to the weapon lane. v2.13.2: mission-only, same hub gate as above.
                    if (kEnableDeflectLend)
                    {
                        const bool two_h = char_owns(ch, g_melee_2h);
                        if (in_hub)
                        {
                            const bool a = set_owned_tag(ch, g_deflect_name, false);
                            const bool t = set_owned_tag(ch, g_deflect_name_tel, false);
                            if (a || t) logf("deflect-lend: name tag withheld (hub - mission-only lend)");
                        }
                        else
                        {
                            if (set_owned_tag(ch, two_h ? g_deflect_name : g_deflect_name_tel, false))
                                logf("deflect-lend: mismatched lane name tag removed");
                            if (set_owned_tag(ch, two_h ? g_deflect_name_tel : g_deflect_name, true))
                                logf(two_h ? "deflect-lend: whitelist name tag granted (Info.Name.Tel-ReaVokoss, settled)"
                                           : "deflect-lend: whitelist name tag granted (Info.Name.Anakin, settled)");
                            if (kEnableClassLockFix) lentname_register(ch);   // v2.19.0 BUILD 1 (gated off in the den build): track for the hub strip
                        }
                    }
                    // v2.18.0: teamwork assist shot -> force push for custom Jedi (mission-only,
                    // g_exotic lane; presentation rides the deflect-lend name tags above, which
                    // DT_AG_ForcePush keys its choreography on - same lane matching).
                    if (kEnableAssistPush)
                    {
                        if (in_hub) revert_assist_push(ch);
                        else        apply_assist_push(ch);
                    }
                }
                else
                {
                    revert_jedi_nav(ch);                              // settled nav revert (sabers off)
                    revert_leader_swap(ch);                           // v2.11.0: original leader back
                    if (set_owned_tag(ch, g_bespoke, false))          // settled bespoke removal
                        logf("saber-stance: bespoke tag removed (sabers off, settled)");
                    if (kEnableDeflectLend)
                    {
                        const bool a = set_owned_tag(ch, g_deflect_name, false);
                        const bool t = set_owned_tag(ch, g_deflect_name_tel, false);
                        if (a || t) logf("deflect-lend: whitelist name tag removed (sabers off, settled)");
                    }
                    if (kEnableAssistPush) revert_assist_push(ch);   // v2.18.0: standard assist back
                }
                g_pending_last[i] = g_update_ticks;
                const unsigned seq_before = g_flip_seq;
                if (relink_character(ch))
                {
                    if (!g_pending_revert[i] && (char_owns(ch, g_pwt_rifle) || g_flip_seq != seq_before))
                    {   // Rifle re-granted during the re-init (even if the hook already flipped it
                        // back) - the layer may have linked rifle, so go around again after quiet
                        flip_pwt(ch);
                        g_pending_quiet[i] = g_update_ticks;
                        logf("saber-stance: rifle re-granted during re-link - retrying after quiet");
                    }
                    else
                    {
                        char msg[128];
                        std::snprintf(msg, sizeof(msg), "saber-stance: %s re-link complete on %p (tick=%u)",
                                      g_pending_revert[i] ? "revert" : "saber", ch, g_update_ticks);
                        logf(msg);
                        g_pending_relink[i] = nullptr;
                    }
                }
                else
                {
                    g_pending_quiet[i] = g_update_ticks;   // wait a full quiet window before retrying
                    if (++g_pending_fails[i] >= kMaxRelinkFails) g_pending_relink[i] = nullptr;
                }
            }
            // sweep dead characters out of the flipped + nav registries so stale pointers never match
            if ((g_update_ticks & 1023u) == 0 && g_is_real != nullptr)
                for (std::size_t i = 0; i < kPendingMax; ++i)
                {
                    if (g_flipped[i] != nullptr && !g_is_real(g_flipped[i])) { g_flipped[i] = nullptr; g_flipped_name[i] = 0; }
                    if (g_nav_ch[i] != nullptr && !g_is_real(g_nav_ch[i])) { g_nav_ch[i] = nullptr; g_nav_name[i] = 0; g_nav_orig[i] = nullptr; }
                    if (g_lswap_ch[i] != nullptr && !g_is_real(g_lswap_ch[i])) { g_lswap_ch[i] = nullptr; g_lswap_name[i] = 0; g_lswap_orig[i] = nullptr; }
                    if (kEnableClassLockFix && g_lentname_ch[i] != nullptr && !g_is_real(g_lentname_ch[i])) { g_lentname_ch[i] = nullptr; g_lentname_id[i] = 0; }   // v2.19.0 BUILD 1 (gated off in the den build)
                }
        }
    };
}

extern "C" __declspec(dllexport) ModBase* start_mod() { return new ZCUnlockedMod(); }
extern "C" __declspec(dllexport) void uninstall_mod(ModBase* mod) { delete mod; }

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_self = inst;
        ::DisableThreadLibraryCalls(inst);
        if (::GetModuleFileNameW(inst, g_log_path, MAX_PATH))
        {
            wchar_t* slash = wcsrchr(g_log_path, L'\\');
            if (slash) { slash[1] = 0; wcsncat(g_log_path, L"ZCUnlocked.log", MAX_PATH - wcslen(g_log_path) - 1); }
            else g_log_path[0] = 0;
        }
    }
    return TRUE;
}

#include "mod.h"
#include "stub/populators.h"
#include "mod/pop/kv_conditional.h"
#include "stub/usermessages_sv.h"
#include "stub/entities.h"
#include "stub/objects.h"
#include "stub/gamerules.h"
#include "stub/tfplayer.h"
#include "stub/nextbot_cc.h"
#include "stub/nextbot_cc_behavior.h"
#include "stub/team.h"
#include "util/scope.h"
#include "util/iterate.h"
#include "util/override.h"


namespace Mod::Pop::Wave_Extensions
{
	struct SentryGunInfo
	{
		~SentryGunInfo()
		{
			if (sentry != nullptr) {
				sentry->DetonateObject();
			}
		}
		
		bool use_hint = true;
		std::vector<CHandle<CTFBotHintSentrygun>> hints;
		Vector origin;
		QAngle angles;
		
		int teamnum = TF_TEAM_BLUE;
		float delay = 0.0f;
		int level   = 0;
		
		bool spawned = false;
		CHandle<CObjectSentrygun> sentry;
	};
	
	struct BossInfo
	{
		~BossInfo()
		{
			if (boss != nullptr) {
				boss->Remove();
			}
		}
		
		Vector origin;
		
		CHalloweenBaseBoss::HalloweenBossType type = CHalloweenBaseBoss::INVALID;
		int teamnum = TF_TEAM_HALLOWEEN_BOSS;
		int health  = -1;
		float delay = 0.0f;
		float lifetime = FLT_MAX;
		
		bool spawned = false;
		CHandle<CHalloweenBaseBoss> boss;
	};
	
	struct SkeletonInfo
	{
		~SkeletonInfo()
		{
			/* WARNING: this removes ALL tf_zombie entities, not just "this one" */
			for (int i = 0; i < IZombieAutoList::AutoList().Count(); ++i) {
				auto skeleton = rtti_cast<CZombie *>(IZombieAutoList::AutoList()[i]);
				if (skeleton == nullptr) continue;
				
				skeleton->Remove();
			}
		}
		
		Vector origin;
		
		CZombie::SkeletonType_t type = CZombie::SKELETON_NORMAL;
		int teamnum = TF_TEAM_HALLOWEEN_BOSS;
		float delay = 0.0f;
		float lifetime = 0.0f;
		
		bool spawned = false;
		// CHandle<CZombie> handle; // REMOVE ME IF POSSIBLE
	};
	
	struct WaveData
	{
		std::vector<std::string>   explanation;
		std::vector<SentryGunInfo> sentryguns;
		std::vector<BossInfo>      bosses;
		std::vector<SkeletonInfo>  skeletons;
		std::vector<std::string>   sound_loops;
		
		bool red_team_wipe_causes_wave_loss = false;
		
		IntervalTimer t_wavestart;
	};
	
	
	std::map<CWave *, WaveData> waves;
	
	
	// TODO: make the search process here less egregiously inefficient please!
	template<typename T, typename = std::enable_if_t<std::is_base_of_v<CHalloweenBaseBoss, T>>>
	static BossInfo *GetInfoForBoss(T *boss)
	{
		auto baseboss = rtti_cast<CHalloweenBaseBoss *>(boss);
		
		for (auto& wave : waves) {
			for (auto& info : wave.second.bosses) {
				if (info.spawned && info.boss == baseboss) {
					return &info;
				}
			}
		}
		
		return nullptr;
	}
	template<typename T>
	static bool HaveInfoForBoss(T *boss)
	{
		return (GetInfoForBoss(boss) != nullptr);
	}
	
	
	DETOUR_DECL_MEMBER(void, CWave_dtor0)
	{
		auto wave = reinterpret_cast<CWave *>(this);
		
//		DevMsg("CWave %08x: dtor0\n", (uintptr_t)wave);
		waves.erase(wave);
		
		DETOUR_MEMBER_CALL(CWave_dtor0)();
	}
	
	DETOUR_DECL_MEMBER(void, CWave_dtor2)
	{
		auto wave = reinterpret_cast<CWave *>(this);
		
//		DevMsg("CWave %08x: dtor2\n", (uintptr_t)wave);
		waves.erase(wave);
		
		DETOUR_MEMBER_CALL(CWave_dtor2)();
	}
	
	
	static bool FindSentryHint(const char *name, std::vector<CHandle<CTFBotHintSentrygun>>& hints)
	{
		ForEachEntityByClassname("bot_hint_sentrygun", [&](CBaseEntity *ent){
			if (FStrEq(STRING(ent->GetEntityName()), name)) {
				auto hint = rtti_cast<CTFBotHintSentrygun *>(ent);
				if (hint != nullptr) {
					hints.emplace_back(hint);
				}
			}
		});
		
		return !hints.empty();
	}
	
	
	static void Parse_Explanation(CWave *wave, KeyValues *kv)
	{
		waves[wave].explanation.clear();
		
		FOR_EACH_SUBKEY(kv, subkey) {
			waves[wave].explanation.emplace_back(subkey->GetString());
		}
	}
	
	static void Parse_SentryGun(CWave *wave, KeyValues *kv)
	{
		SentryGunInfo info;
		
		bool got_position = false;
		
		FOR_EACH_SUBKEY(kv, subkey) {
			const char *name = subkey->GetName();
			
			if (FStrEq(name, "HintName")) {
				if (!FindSentryHint(subkey->GetString(), info.hints)) {
					Warning("Could not find a bot_hint_sentrygun entity named \"%s\".\n", subkey->GetString());
					return;
				}
			} else if (FStrEq(name, "TeamNum")) {
				info.teamnum = Clamp(subkey->GetInt(), (int)TF_TEAM_RED, (int)TF_TEAM_BLUE);
			//	DevMsg("TeamNum \"%s\" --> %d\n", subkey->GetString(), info.teamnum);
			} else if (FStrEq(name, "Delay")) {
				info.delay = Max(0.0f, subkey->GetFloat());
			//	DevMsg("Delay \"%s\" --> %.1f\n", subkey->GetString(), info.delay);
			} else if (FStrEq(name, "Level")) {
				info.level = Clamp(subkey->GetInt(), 1, 3);
			//	DevMsg("Level \"%s\" --> %d\n", subkey->GetString(), info.level);
			} else if (FStrEq(name, "Position")) {
				FOR_EACH_SUBKEY(subkey, subsub) {
					const char *name = subsub->GetName();
					float value      = subsub->GetFloat();
					
					if (FStrEq(name, "X")) {
						info.origin.x = value;
					} else if (FStrEq(name, "Y")) {
						info.origin.y = value;
					} else if (FStrEq(name, "Z")) {
						info.origin.z = value;
					} else if (FStrEq(name, "Pitch")) {
						info.angles.x = value;
					} else if (FStrEq(name, "Yaw")) {
						info.angles.y = value;
					} else if (FStrEq(name, "Roll")) {
						info.angles.z = value;
					} else {
						Warning("Unknown key \'%s\' in SentryGun Position sub-block.\n", name);
					}
				}
				info.use_hint = false;
				got_position = true;
			} else {
				Warning("Unknown key \'%s\' in SentryGun block.\n", name);
			}
		}
		
		bool fail = false;
		if ((info.use_hint && info.hints.empty()) || (!info.use_hint && !got_position)) {
			Warning("Missing HintName key or Position block in SentryGun block.\n");
			fail = true;
		}
		if (info.level == -1) {
			Warning("Missing Level key in SentryGun block.\n");
			fail = true;
		}
		if (fail) return;
		
		DevMsg("Wave %08x: add SentryGun\n", (uintptr_t)wave);
		waves[wave].sentryguns.push_back(info);
	}
	
	static void Parse_HalloweenBoss(CWave *wave, KeyValues *kv)
	{
		BossInfo info;
		
		bool got_position = false;
		
		FOR_EACH_SUBKEY(kv, subkey) {
			const char *name = subkey->GetName();
			
			if (FStrEq(name, "BossType")) {
				if (FStrEq(subkey->GetString(), "HHH")) {
					info.type = CHalloweenBaseBoss::HEADLESS_HATMAN;
				} else if (FStrEq(subkey->GetString(), "MONOCULUS")) {
					info.type = CHalloweenBaseBoss::EYEBALL_BOSS;
				} else if (FStrEq(subkey->GetString(), "Merasmus")) {
					info.type = CHalloweenBaseBoss::MERASMUS;
				} else {
					Warning("Invalid value \'%s\' for BossType key in HalloweenBoss block.\n", subkey->GetString());
				}
			//	DevMsg("BossType \"%s\" --> %d\n", subkey->GetString(), info.type);
			} else if (FStrEq(name, "TeamNum")) {
				info.teamnum = subkey->GetInt();
			//	DevMsg("TeamNum \"%s\" --> %d\n", subkey->GetString(), info.teamnum);
			} else if (FStrEq(name, "Health")) {
				info.health = Max(1, subkey->GetInt());
			//	DevMsg("Health \"%s\" --> %d\n", subkey->GetString(), info.health);
			} else if (FStrEq(name, "Delay")) {
				info.delay = Max(0.0f, subkey->GetFloat());
			//	DevMsg("Delay \"%s\" --> %.1f\n", subkey->GetString(), info.delay);
			} else if (FStrEq(name, "Lifetime")) {
				info.lifetime = Max(0.0f, subkey->GetFloat());
			//	DevMsg("Lifetime \"%s\" --> %.1f\n", subkey->GetString(), info.lifetime);
			} else if (FStrEq(name, "Position")) {
				FOR_EACH_SUBKEY(subkey, subsub) {
					const char *name = subsub->GetName();
					float value      = subsub->GetFloat();
					
					if (FStrEq(name, "X")) {
						info.origin.x = value;
					} else if (FStrEq(name, "Y")) {
						info.origin.y = value;
					} else if (FStrEq(name, "Z")) {
						info.origin.z = value;
					} else {
						Warning("Unknown key \'%s\' in HalloweenBoss Position sub-block.\n", name);
					}
				}
				got_position = true;
			} else {
				Warning("Unknown key \'%s\' in HalloweenBoss block.\n", name);
			}
		}
		
		bool fail = false;
		if (!got_position) {
			Warning("Missing Position block in HalloweenBoss block.\n");
			fail = true;
		}
		if (info.type == CHalloweenBaseBoss::INVALID) {
			Warning("Missing BossType key in HalloweenBoss block.\n");
			fail = true;
		}
		if (info.type != CHalloweenBaseBoss::EYEBALL_BOSS && info.lifetime != FLT_MAX) {
			Warning("Only HalloweenBoss blocks of type MONOCULUS may specify a Lifetime value.\n");
			/* eh... non-fatal warning, I guess */
		//	fail = true;
		}
		if (info.teamnum != TF_TEAM_HALLOWEEN_BOSS) {
			if (info.type == CHalloweenBaseBoss::EYEBALL_BOSS) {
				if (info.teamnum != TF_TEAM_RED && info.teamnum != TF_TEAM_BLUE) {
					Warning("Invalid value for TeamNum key in HalloweenBoss block: MONOCULUS must be team 5 or 2 or 3.\n");
					fail = true;
				}
			} else {
				Warning("Invalid value for TeamNum key in HalloweenBoss block: HHH and Merasmus must be team 5.\n");
				fail = true;
			}
		}
		if (fail) return;
		
		DevMsg("Wave %08x: add HalloweenBoss\n", (uintptr_t)wave);
		waves[wave].bosses.push_back(info);
	}
	
	static void Parse_Skeleton(CWave *wave, KeyValues *kv)
	{
		SkeletonInfo info;
		
		bool got_position = false;
		
		FOR_EACH_SUBKEY(kv, subkey) {
			const char *name = subkey->GetName();
			
			if (FStrEq(name, "SkeletonType")) {
				if (FStrEq(subkey->GetString(), "Normal")) {
					info.type = CZombie::SKELETON_NORMAL;
				} else if (FStrEq(subkey->GetString(), "King")) {
					info.type = CZombie::SKELETON_KING;
				} else if (FStrEq(subkey->GetString(), "Small")) {
					info.type = CZombie::SKELETON_SMALL;
				} else {
					Warning("Invalid value \'%s\' for SkeletonType key in Skeleton block.\n", subkey->GetString());
				}
			//	DevMsg("SkeletonType \"%s\" --> %d\n", subkey->GetString(), info.type);
			} else if (FStrEq(name, "TeamNum")) {
				info.teamnum = subkey->GetInt();
			//	DevMsg("TeamNum \"%s\" --> %d\n", subkey->GetString(), info.teamnum);
			} else if (FStrEq(name, "Delay")) {
				info.delay = Max(0.0f, subkey->GetFloat());
			//	DevMsg("Delay \"%s\" --> %.1f\n", subkey->GetString(), info.delay);
			} else if (FStrEq(name, "Lifetime")) {
				info.lifetime = Max(0.0f, subkey->GetFloat());
			//	DevMsg("Lifetime \"%s\" --> %.1f\n", subkey->GetString(), info.lifetime);
			} else if (FStrEq(name, "Position")) {
				FOR_EACH_SUBKEY(subkey, subsub) {
					const char *name = subsub->GetName();
					float value      = subsub->GetFloat();
					
					if (FStrEq(name, "X")) {
						info.origin.x = value;
					} else if (FStrEq(name, "Y")) {
						info.origin.y = value;
					} else if (FStrEq(name, "Z")) {
						info.origin.z = value;
					} else {
						Warning("Unknown key \'%s\' in Skeleton Position sub-block.\n", name);
					}
				}
				got_position = true;
			} else {
				Warning("Unknown key \'%s\' in Skeleton block.\n", name);
			}
		}
		
		bool fail = false;
		if (!got_position) {
			Warning("Missing Position block in Skeleton block.\n");
			fail = true;
		}
		if (info.teamnum != TF_TEAM_HALLOWEEN_BOSS && info.teamnum != TF_TEAM_RED && info.teamnum != TF_TEAM_BLUE) {
			Warning("Invalid value for TeamNum key in Skeleton block: must be team 5 or 2 or 3.\n");
			fail = true;
		}
		if (fail) return;
		
		DevMsg("Wave %08x: add Skeleton\n", (uintptr_t)wave);
		waves[wave].skeletons.push_back(info);
	}
	
	static void Parse_SoundLoop(CWave *wave, KeyValues *kv)
	{
		if (!waves[wave].sound_loops.empty()) {
			Warning("Multiple \'SoundLoop\' blocks found in the same Wave!\n");
			return;
		}
		
		FOR_EACH_SUBKEY(kv, subkey) {
			const char *name = subkey->GetName();
			
			if (FStrEq(name, "SoundFile")) {
				waves[wave].sound_loops.push_back(subkey->GetString());
			} else {
				Warning("Unknown key \'%s\' in SoundLoop block.\n", name);
			}
		}
	}
	
	DETOUR_DECL_MEMBER(bool, CWave_Parse, KeyValues *kv)
	{
		auto wave = reinterpret_cast<CWave *>(this);
		
	//	DevMsg("CWave::Parse\n");
		
		std::vector<KeyValues *> del_kv;
		FOR_EACH_SUBKEY(kv, subkey) {
			const char *name = subkey->GetName();
			
			bool del = true;
			if (FStrEq(name, "Explanation")) {
				Parse_Explanation(wave, subkey);
			} else if (FStrEq(name, "SentryGun")) {
				Parse_SentryGun(wave, subkey);
			} else if (FStrEq(name, "HalloweenBoss")) {
				Parse_HalloweenBoss(wave, subkey);
			} else if (FStrEq(name, "Skeleton")) {
				Parse_Skeleton(wave, subkey);
			} else if (FStrEq(name, "SoundLoop")) {
				Parse_SoundLoop(wave, subkey);
			} else if (FStrEq(name, "RedTeamWipeCausesWaveLoss")) {
				waves[wave].red_team_wipe_causes_wave_loss = subkey->GetBool();
			} else {
				del = false;
			}
			
			if (del) {
	//			DevMsg("Key \"%s\": processed, will delete\n", name);
				del_kv.push_back(subkey);
			} else {
	//			DevMsg("Key \"%s\": passthru\n", name);
			}
		}
		
		for (auto subkey : del_kv) {
	//		DevMsg("Deleting key \"%s\"\n", subkey->GetName());
			kv->RemoveSubKey(subkey);
			subkey->deleteThis();
		}
		
		return DETOUR_MEMBER_CALL(CWave_Parse)(kv);
	}
	
	
	static void PrintToChatAll(const char *str)
	{
		int msg_type = usermessages->LookupUserMessage("SayText2");
		if (msg_type == -1) return;
		
		CReliableBroadcastRecipientFilter filter;
		
		bf_write *msg = engine->UserMessageBegin(&filter, msg_type);
		if (msg == nullptr) return;
		
		msg->WriteByte(0x00);
		msg->WriteByte(0x00);
		msg->WriteString(str);
		
		engine->MessageEnd();
	}
	
	static void ParseColorsAndPrint(const char *line)
	{
		std::vector<char> output;
		
		/* always start with reset so that colors will work properly */
		output.push_back('\x01');
		
		bool in_braces = false;
		int brace_idx = 0;
		
		int i = 0;
		char c;
		while ((c = line[i]) != '\0') {
			if (in_braces) {
				if (c == '}') {
					const char *brace_str = line + brace_idx;
					int brace_len = i - brace_idx;
					
					if (V_strnicmp(brace_str, "Reset", brace_len) == 0) {
						output.push_back('\x01');
					} else if (V_strnicmp(brace_str, "Blue", brace_len) == 0) {
						output.insert(output.end(), {'\x07', '9', '9', 'c', 'c', 'f', 'f'});
					} else if (V_strnicmp(brace_str, "Red", brace_len) == 0) {
						output.insert(output.end(), {'\x07', 'f', 'f', '3', 'f', '3', 'f'});
					} else if (V_strnicmp(brace_str, "Green", brace_len) == 0) {
						output.insert(output.end(), {'\x07', '9', '9', 'f', 'f', '9', '9'});
					} else if (V_strnicmp(brace_str, "DarkGreen", brace_len) == 0) {
						output.insert(output.end(), {'\x07', '4', '0', 'f', 'f', '4', '0'});
					} else if (V_strnicmp(brace_str, "Yellow", brace_len) == 0) {
						output.insert(output.end(), {'\x07', 'f', 'f', 'b', '2', '0', '0'});
					} else if (V_strnicmp(brace_str, "Grey", brace_len) == 0) {
						output.insert(output.end(), {'\x07', 'c', 'c', 'c', 'c', 'c', 'c'});
					} else {
						/* RGB hex code */
						if (brace_len >= 6) {
							output.push_back('\x07');
							output.push_back(brace_str[0]);
							output.push_back(brace_str[1]);
							output.push_back(brace_str[2]);
							output.push_back(brace_str[3]);
							output.push_back(brace_str[4]);
							output.push_back(brace_str[5]);
						}
					}
					
					in_braces = false;
				}
			} else {
				if (c == '{') {
					in_braces = true;
					brace_idx = i + 1;
				} else {
					output.push_back(c);
				}
			}
			
			++i;
		}
		
		/* append an extra space at the end to make empty lines show up */
		output.push_back(' ');
		output.push_back('\n');
		output.push_back('\0');
		
		PrintToChatAll(output.data());
	}
	
	static void ShowWaveExplanation()
	{
		/* wave will be null after game is won and in other corner cases */
		CWave *wave = g_pPopulationManager->GetCurrentWave();
		if (wave == nullptr) return;
		
		const auto& explanation = waves[wave].explanation;
		for (const auto& line : explanation) {
			ParseColorsAndPrint(line.c_str());
		}
	}
	
	
	RefCount rc_JumpToWave;
	DETOUR_DECL_MEMBER(void, CPopulationManager_JumpToWave, unsigned int wave, float f1)
	{
		SCOPED_INCREMENT(rc_JumpToWave);
	//	DevMsg("[%8.3f] JumpToWave\n", gpGlobals->curtime);
		DETOUR_MEMBER_CALL(CPopulationManager_JumpToWave)(wave, f1);
		ShowWaveExplanation();
	}
	
	DETOUR_DECL_MEMBER(void, CPopulationManager_WaveEnd, bool b1)
	{
	//	DevMsg("[%8.3f] WaveEnd\n", gpGlobals->curtime);
		DETOUR_MEMBER_CALL(CPopulationManager_WaveEnd)(b1);
		ShowWaveExplanation();
	}
	
	DETOUR_DECL_MEMBER(void, CMannVsMachineStats_RoundEvent_WaveEnd, bool success)
	{
		DETOUR_MEMBER_CALL(CMannVsMachineStats_RoundEvent_WaveEnd)(success);
		if (!success && rc_JumpToWave == 0) {
		//	DevMsg("[%8.3f] RoundEvent_WaveEnd\n", gpGlobals->curtime);
			ShowWaveExplanation();
		}
	}
	
	
	static CObjectSentrygun *SpawnSentryGun(const Vector& origin, const QAngle& angles, int teamnum, int level)
	{
		auto sentry = rtti_cast<CObjectSentrygun *>(CreateEntityByName("obj_sentrygun"));
		if (sentry == nullptr) {
			Warning("SpawnSentryGun: CreateEntityByName(\"obj_sentrygun\") failed\n");
			return nullptr;
		}
		
	//	DevMsg("[%8.3f] SpawnSentryGun: [hint #%d \"%s\"] [teamnum %d] [level %d]\n",
	//		gpGlobals->curtime, ENTINDEX(sg_info.hint), STRING(sg_info.hint->GetEntityName()), sg_info.teamnum, sg_info.level);
		
		sentry->SetAbsOrigin(origin);
		sentry->SetAbsAngles(angles);
		
		sentry->Spawn();
		
		sentry->ChangeTeam(teamnum);
		sentry->m_nDefaultUpgradeLevel = level - 1;
		
		sentry->InitializeMapPlacedObject();
		
		DevMsg("SpawnSentryGun: #%d, %08x, level %d, health %d, maxhealth %d\n",
			ENTINDEX(sentry), (uintptr_t)sentry, level, sentry->GetHealth(), sentry->GetMaxHealth());
		
		return sentry;
	}
	
	static void SpawnSentryGuns(SentryGunInfo& info)
	{
		if (info.use_hint && info.hints.empty()) {
			Warning("SpawnSentryGuns: info.hints.empty()\n");
			return;
		}
		
		if (info.use_hint) {
			for (const auto& hint : info.hints) {
				info.sentry = SpawnSentryGun(hint->GetAbsOrigin(), hint->GetAbsAngles(), info.teamnum, info.level);
			}
		} else {
			info.sentry = SpawnSentryGun(info.origin, info.angles, info.teamnum, info.level);
		}
		
		if (info.sentry != nullptr) {
			info.spawned = true;
		}
	}
	
	
	static void SpawnBoss(BossInfo& info)
	{
		CHalloweenBaseBoss *boss = CHalloweenBaseBoss::SpawnBossAtPos(info.type, info.origin, info.teamnum, nullptr);
		if (boss == nullptr) {
			Warning("SpawnBoss: CHalloweenBaseBoss::SpawnBossAtPos(type %d, teamnum %d) failed\n", info.type, info.teamnum);
			return;
		}
		
		if (info.health > 0) {
			boss->SetMaxHealth(info.health);
			boss->SetHealth   (info.health);
		}
		
		info.boss = boss;
		info.spawned = true;
	}
	
	
	static void SpawnSkeleton(SkeletonInfo& info)
	{
		CZombie *skeleton = CZombie::SpawnAtPos(info.origin, info.lifetime, info.teamnum, nullptr, info.type);
		if (skeleton == nullptr) {
			Warning("SpawnSkeleton: CZombie::SpawnAtPos(type %d, teamnum %d) failed\n", info.type, info.teamnum);
			return;
		}
		
		info.spawned = true;
	}
	
	
	DETOUR_DECL_MEMBER(void, CWave_ActiveWaveUpdate)
	{
		auto wave = reinterpret_cast<CWave *>(this);
		
		auto it = waves.find(wave);
		if (it == waves.end()) {
			DETOUR_MEMBER_CALL(CWave_ActiveWaveUpdate)();
			return;
		}
		WaveData& data = (*it).second;
		
		if (!data.t_wavestart.HasStarted()) {
			data.t_wavestart.Start();
		}
		
		/* since we are pre-detour and ActiveWaveUpdate only happens in RND_RUNNING, we are safe to skip the check */
		if (data.red_team_wipe_causes_wave_loss/* && TFGameRules()->State_Get() == GR_STATE_RND_RUNNING*/) {
			CTFTeam *team_red = TFTeamMgr()->GetTeam(TF_TEAM_RED);
			if (team_red != nullptr && team_red->GetNumPlayers() != 0) {
				int num_red_humans       = 0;
				int num_red_humans_alive = 0;
				
				ForEachTFPlayerOnTeam(TFTeamMgr()->GetTeam(TF_TEAM_RED), [&](CTFPlayer *player){
					if (player->IsBot()) return;
					
					++num_red_humans;
					if (player->IsAlive()) {
						++num_red_humans_alive;
					}
				});
				
				/* if red team actually contains zero humans, then don't do anything */
				if (num_red_humans > 0 && num_red_humans_alive == 0) {
					/* not entirely sure what effect the win reason parameter has (if it even has an effect at all) */
					TFGameRules()->SetWinningTeam(TF_TEAM_BLUE, WINREASON_OPPONENTS_DEAD, true, false);
				}
			}
		}
		
		// ^^^^   PRE-DETOUR ===================================================
		
		DETOUR_MEMBER_CALL(CWave_ActiveWaveUpdate)();
		
		// vvvv  POST-DETOUR ===================================================
		
		for (auto& info : data.sentryguns) {
			if (!info.spawned && !data.t_wavestart.IsLessThen(info.delay)) {
				SpawnSentryGuns(info);
			}
		}
		
		for (auto& info : data.bosses) {
			if (!info.spawned && !data.t_wavestart.IsLessThen(info.delay)) {
				SpawnBoss(info);
			}
		}
		
		for (auto& info : data.skeletons) {
			if (!info.spawned && !data.t_wavestart.IsLessThen(info.delay)) {
				SpawnSkeleton(info);
			}
		}
	}
	
	DETOUR_DECL_MEMBER(bool, CWave_IsDoneWithNonSupportWaves)
	{
		bool done = DETOUR_MEMBER_CALL(CWave_IsDoneWithNonSupportWaves)();
		
		auto wave = reinterpret_cast<CWave *>(this);
		
		/* don't let the wave end until all Halloween bosses have been killed */
		auto it = waves.find(wave);
		if (it != waves.end()) {
			WaveData& data = (*it).second;
			
			for (auto& info : data.bosses) {
				if (info.spawned && info.boss != nullptr && info.boss->IsAlive()) {
					return false;
				}
			}
		}
		
		/* don't let the wave end until all skeletons have been killed */
		for (int i = 0; i < IZombieAutoList::AutoList().Count(); ++i) {
			auto skeleton = rtti_cast<CZombie *>(IZombieAutoList::AutoList()[i]);
			if (skeleton == nullptr) continue;
			
			if (skeleton->IsAlive()) {
				return false;
			}
		}
		
		return done;
	}
	
	
	std::string soundloop_active;
	
	
	static void StopSoundLoop()
	{
		ConColorMsg(Color(0xff, 0x00, 0x00, 0xff), "[SoundLoop] StopSoundLoop \"%s\"\n", soundloop_active.c_str());
		
		if (TFGameRules() != nullptr) {
			TFGameRules()->BroadcastSound(SOUND_FROM_LOCAL_PLAYER, soundloop_active.c_str(), SND_STOP);
		}
		
		soundloop_active.clear();
	}
	
	static void StartSoundLoop(const std::string& filename)
	{
		if (!soundloop_active.empty()) {
			StopSoundLoop();
		}
		
		ConColorMsg(Color(0x00, 0xff, 0x00, 0xff), "[SoundLoop] StartSoundLoop \"%s\"\n", filename.c_str());
		
		/* if filename is explicitly "", then don't play anything */
		if (TFGameRules() != nullptr && filename != "") {
			TFGameRules()->BroadcastSound(SOUND_FROM_LOCAL_PLAYER, filename.c_str(), SND_NOFLAGS);
			soundloop_active = filename;
		}
	}
	
	
	DETOUR_DECL_MEMBER(void, CTeamplayRoundBasedRules_State_Enter, gamerules_roundstate_t newState)
	{
		auto oldState = TeamplayRoundBasedRules()->State_Get();
		
		ConColorMsg(Color(0xff, 0x00, 0xff, 0xff),
			"[SoundLoop] CTeamplayRoundBasedRules: %s -> %s\n",
			GetRoundStateName(oldState), GetRoundStateName(newState));
		
		if (oldState != GR_STATE_RND_RUNNING && newState == GR_STATE_RND_RUNNING) {
			if (TFGameRules()->IsMannVsMachineMode()) {
				auto it = waves.find(g_pPopulationManager->GetCurrentWave());
				if (it != waves.end()) {
					WaveData& data = (*it).second;
					
					if (!data.sound_loops.empty()) {
						std::string sound_loop = *select_random(data.sound_loops);
						StartSoundLoop(sound_loop);
					}
				}
			}
		} else if (oldState == GR_STATE_RND_RUNNING && newState != GR_STATE_RND_RUNNING) {
			StopSoundLoop();
		}
		
		DETOUR_MEMBER_CALL(CTeamplayRoundBasedRules_State_Enter)(newState);
	}
	
	
	/* set MONOCULUS's lifetime from the spawner parameter instead of from the global convars */
	DETOUR_DECL_MEMBER(ActionResult<CEyeballBoss>, CEyeballBossIdle_OnStart, CEyeballBoss *actor, Action<CEyeballBoss> *action)
	{
		auto me = reinterpret_cast<CEyeballBossIdle *>(this);
		
		auto result = DETOUR_MEMBER_CALL(CEyeballBossIdle_OnStart)(actor, action);
		
		if (TFGameRules()->IsMannVsMachineMode()) {
			BossInfo *info = GetInfoForBoss(actor);
			if (info != nullptr) {
				me->m_ctLifetime.Start(info->lifetime);
			}
		}
		
		return result;
	}
	
	
	/* block attempts by MONOCULUS to switch to CEyeballBossTeleport */
	DETOUR_DECL_MEMBER(ActionResult<CEyeballBoss>, CEyeballBossIdle_Update, CEyeballBoss *actor, float dt)
	{
		auto result = DETOUR_MEMBER_CALL(CEyeballBossIdle_Update)(actor, dt);
		
		if (result.transition == ActionTransition::CHANGE_TO && strcmp(result.reason, "Moving...") == 0) {
			if (TFGameRules()->IsMannVsMachineMode() && HaveInfoForBoss(actor)) {
				delete result.action;
				
				result.transition = ActionTransition::CONTINUE;
				result.action     = nullptr;
				result.reason     = nullptr;
			}
		}
		
		return result;
	}
	
	
	RefCount rc_CEyeballBossDead_Update__and_is_from_spawner;
	DETOUR_DECL_MEMBER(ActionResult<CEyeballBoss>, CEyeballBossDead_Update, CEyeballBoss *actor, float dt)
	{
		SCOPED_INCREMENT_IF(rc_CEyeballBossDead_Update__and_is_from_spawner,
			(TFGameRules()->IsMannVsMachineMode() && HaveInfoForBoss(actor)));
		
		return DETOUR_MEMBER_CALL(CEyeballBossDead_Update)(actor, dt);
	}
	
	/* prevent MONOCULUS's death from spawning a teleport vortex */
	DETOUR_DECL_STATIC(CBaseEntity *, CBaseEntity_Create, const char *szName, const Vector& vecOrigin, const QAngle& vecAngles, CBaseEntity *pOwner)
	{
		if (rc_CEyeballBossDead_Update__and_is_from_spawner > 0 && strcmp(szName, "teleport_vortex") == 0) {
			return nullptr;
		}
		
		return DETOUR_STATIC_CALL(CBaseEntity_Create)(szName, vecOrigin, vecAngles, pOwner);
	}
	
	
	/* temporarily spoof tf_max_active_zombie to prevent skeletons from suiciding due to being over-limit */
	DETOUR_DECL_MEMBER(void, CZombie_Spawn)
	{
		COverrideRAII<CConVarOverride_IntVal> override("tf_max_active_zombie", 10000);
		DETOUR_MEMBER_CALL(CZombie_Spawn)();
	}
	
	
#if 0
	RefCount rc_CZombieBehavior_OnKilled__and_is_king;
	DETOUR_DECL_MEMBER(EventDesiredResult<CZombie>, CZombieBehavior_OnKilled, CZombie *actor, const CTakeDamageInfo& info)
	{
		SCOPED_INCREMENT_IF(rc_CZombieBehavior_OnKilled__and_is_king,
			(TFGameRules()->IsMannVsMachineMode() && actor != nullptr && actor->m_iSkeletonType == CZombie::SKELETON_KING));
		return DETOUR_MEMBER_CALL(CZombieBehavior_OnKilled)(actor, info);
	}
	
	/* don't drop spells when skeleton kings are killed */
	DETOUR_DECL_MEMBER(void, CTFGameRules_DropSpellPickup, const Vector& where, int tier)
	{
		if (rc_CZombieBehavior_OnKilled__and_is_king > 0) {
			return;
		}
		
		DETOUR_MEMBER_CALL(CTFGameRules_DropSpellPickup)(where, tier);
	}
#endif
	
	
	class CMod : public IMod, public IModCallbackListener
	{
	public:
		CMod() : IMod("Pop:Wave_Extensions")
		{
			MOD_ADD_DETOUR_MEMBER(CWave_dtor0, "CWave::~CWave [D0]");
			MOD_ADD_DETOUR_MEMBER(CWave_dtor2, "CWave::~CWave [D2]");
			
			MOD_ADD_DETOUR_MEMBER(CWave_Parse, "CWave::Parse");
			
			MOD_ADD_DETOUR_MEMBER(CPopulationManager_JumpToWave,          "CPopulationManager::JumpToWave");
			MOD_ADD_DETOUR_MEMBER(CPopulationManager_WaveEnd,             "CPopulationManager::WaveEnd");
			MOD_ADD_DETOUR_MEMBER(CMannVsMachineStats_RoundEvent_WaveEnd, "CMannVsMachineStats::RoundEvent_WaveEnd");
			
			MOD_ADD_DETOUR_MEMBER(CWave_ActiveWaveUpdate,          "CWave::ActiveWaveUpdate");
			MOD_ADD_DETOUR_MEMBER(CWave_IsDoneWithNonSupportWaves, "CWave::IsDoneWithNonSupportWaves");
			
			MOD_ADD_DETOUR_MEMBER(CTeamplayRoundBasedRules_State_Enter, "CTeamplayRoundBasedRules::State_Enter");
			
			MOD_ADD_DETOUR_MEMBER(CEyeballBossIdle_OnStart, "CEyeballBossIdle::OnStart");
			
			MOD_ADD_DETOUR_MEMBER(CEyeballBossIdle_Update, "CEyeballBossIdle::Update");
			
			MOD_ADD_DETOUR_MEMBER(CEyeballBossDead_Update, "CEyeballBossDead::Update");
			MOD_ADD_DETOUR_STATIC(CBaseEntity_Create,      "CBaseEntity::Create");
			
			MOD_ADD_DETOUR_MEMBER(CZombie_Spawn, "CZombie::Spawn");
			
#if 0
			MOD_ADD_DETOUR_MEMBER(CZombieBehavior_OnKilled,     "CZombieBehavior::OnKilled");
			MOD_ADD_DETOUR_MEMBER(CTFGameRules_DropSpellPickup, "CTFGameRules::DropSpellPickup");
#endif
		}
		
		virtual void OnUnload() override
		{
			waves.clear();
			StopSoundLoop();
		}
		
		virtual void OnDisable() override
		{
			waves.clear();
			StopSoundLoop();
		}
		
		virtual bool ShouldReceiveCallbacks() const override { return this->IsEnabled(); }
		
		virtual void LevelInitPreEntity() override
		{
			waves.clear();
			StopSoundLoop();
		}
		
		virtual void LevelShutdownPostEntity() override
		{
			waves.clear();
			StopSoundLoop();
		}
	};
	CMod s_Mod;
	
	
	ConVar cvar_enable("sig_pop_wave_extensions", "0", FCVAR_NOTIFY,
		"Mod: enable extended KV in CWave::Parse",
		[](IConVar *pConVar, const char *pOldValue, float flOldValue){
			s_Mod.Toggle(static_cast<ConVar *>(pConVar)->GetBool());
		});
	
	
	class CKVCond_Wave : public IKVCond
	{
	public:
		virtual bool operator()() override
		{
			return s_Mod.IsEnabled();
		}
	};
	CKVCond_Wave cond;
}


/* wave explanation TODO:

each time a wave starts, reset some tracking info
- keep track of which steam IDs have seen the explanation and which haven't

if a new player connects, and the wave has an explanation, and they haven't seen it,
then show it *just* to them, a few seconds after they pick a class






*/

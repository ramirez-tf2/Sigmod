#include "mod.h"
#include "re/nextbot.h"
#include "stub/tfplayer.h"


namespace Mod::Bot::RunFast
{
	constexpr uint8_t s_Buf_Verify[] = {
		0xe8, 0x08, 0x73, 0xfe, 0xff,                   // +0000  call Path::Compute<CTFBotPathCost>
		0xc7, 0x44, 0x24, 0x04, 0x00, 0x00, 0x00, 0x40, // +0005  mov dword ptr [esp+0x4],2.0f
		0xc7, 0x04, 0x24, 0x00, 0x00, 0x80, 0x3f,       // +000D  mov dword ptr [esp],1.0f
		0xe8, 0xdc, 0xd8, 0xae, 0x00,                   // +0014  call RandomFloat
	};
	constexpr uint8_t s_Buf_Patch[] = {
		0xe8, 0x08, 0x73, 0xfe, 0xff, // +0000  call Path::Compute<CTFBotPathCost>
		0xd9, 0xee,                   // +0005  fldz
		0x90, 0x90, 0x90, 0x90, 0x90, // +0007  nop nop nop nop nop
		0x90, 0x90, 0x90, 0x90, 0x90, // +000C  nop nop nop nop nop
		0x90, 0x90, 0x90, 0x90, 0x90, // +0011  nop nop nop nop nop
		0x90, 0x90, 0x90,             // +0016  nop nop nop
	};
	static_assert(sizeof(s_Buf_Verify) == sizeof(s_Buf_Patch));
	
	struct CPatch_CTFBotPushToCapturePoint_Update : public CPatch
	{
		CPatch_CTFBotPushToCapturePoint_Update() : CPatch(sizeof(s_Buf_Verify)) {}
		
		virtual const char *GetFuncName() const override { return "CTFBotPushToCapturePoint::Update"; }
		virtual uint32_t GetFuncOffMin() const override { return 0x0000; }
		virtual uint32_t GetFuncOffMax() const override { return 0x0240; } // @ 0x01d3
		
		virtual bool GetVerifyInfo(ByteBuf& buf, ByteBuf& mask) const override
		{
			buf.CopyFrom(s_Buf_Verify);
			
			mask.SetRange(0x00 + 1, 4, 0x00);
			mask.SetRange(0x14 + 1, 4, 0x00);
			
			return true;
		}
		
		virtual bool GetPatchInfo(ByteBuf& buf, ByteBuf& mask) const override
		{
			buf.CopyFrom(s_Buf_Patch);
			
			mask.SetAll(0xff);
			mask.SetRange(0x00, 5, 0x00);
			
			return true;
		}
	};
	
	
	DETOUR_DECL_MEMBER(float, CTFBotLocomotion_GetRunSpeed)
	{
		auto loco = reinterpret_cast<ILocomotion *>(this);
		CTFPlayer *bot = ToTFPlayer(loco->GetBot()->GetEntity());
		
		if (bot != nullptr) {
			return bot->TeamFortress_CalculateMaxSpeed();
		} else {
			Warning("Can't determine actual max speed in detour for CTFBotLocomotion::GetRunSpeed!\n");
			return 3200.0f;
		}
	}
	
	
	DETOUR_DECL_MEMBER(void, PlayerLocomotion_Jump)
	{
		/* don't let bots try to jump, it just slows them down */
	}
	
	
	class CMod : public IMod
	{
	public:
		CMod() : IMod("Bot:RunFast")
		{
			this->AddPatch(new CPatch_CTFBotPushToCapturePoint_Update());
			MOD_ADD_DETOUR_MEMBER(CTFBotLocomotion_GetRunSpeed, "CTFBotLocomotion::GetRunSpeed");
			MOD_ADD_DETOUR_MEMBER(PlayerLocomotion_Jump,        "PlayerLocomotion::Jump");
		}
	};
	CMod s_Mod;
	
	
	ConVar cvar_enable("sig_bot_runfast", "0", FCVAR_NOTIFY,
		"Mod: make super-fast bot locomotion feasible",
		[](IConVar *pConVar, const char *pOldValue, float flOldValue){
			s_Mod.Toggle(static_cast<ConVar *>(pConVar)->GetBool());
		});
	
	
	CON_COMMAND(sig_bot_runfast_config, "Auto-set a bunch of convars for better results")
	{
		static ConVarRef nb_update_frequency             ("nb_update_frequency");
		static ConVarRef nb_update_framelimit            ("nb_update_framelimit");
		static ConVarRef nb_update_maxslide              ("nb_update_maxslide");
		static ConVarRef nb_last_area_update_tolerance   ("nb_last_area_update_tolerance");
		static ConVarRef nb_player_move_direct           ("nb_player_move_direct");
		static ConVarRef nb_path_segment_influence_radius("nb_path_segment_influence_radius");
		static ConVarRef tf_bot_path_lookahead_range     ("tf_bot_path_lookahead_range");
		
		nb_update_frequency.SetValue(0.001f);
		nb_update_framelimit.SetValue(1000);
		nb_update_maxslide.SetValue(0);
		
		nb_last_area_update_tolerance.SetValue(1.0f);
		nb_player_move_direct.SetValue(1);
	//	nb_path_segment_influence_radius.SetValue(100);
		tf_bot_path_lookahead_range.SetValue(600);
	}
}

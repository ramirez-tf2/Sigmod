#include "mod.h"
#include "stub/tfbot_behavior.h"


namespace Mod::AI::Improved_UseItem
{
	ConVar cvar_debug("sig_ai_improved_useitem_debug", "0", FCVAR_NOTIFY,
		"Mod: debug CTFBotUseItemImproved and descendants");
	
	
	// TODO: figure out why the hell this was made to be Action<CTFBot> rather than IHotplugAction<CTFBot>;
	// ostensibly it should be the latter, however when I make it the latter, the game goes crashy:
	// the stack trace seems to indicate that an EIP --> garbage situation occurs in the m_DestroyedActions.PurgeAndDeleteElements()
	// step of Behavior<CTFBot>::Update, under CTFBotMainAction's dtor and CTFBotTacticalMonitor's dtor
	// (so it's some kind of "our IHotplugAction virtual dtor doesn't interact well with the game code" situation)
	class CTFBotUseItemImproved : public IHotplugAction<CTFBot>
	{
	public:
		enum class State : int
		{
			FIRE, // wait for the weapon switch delay, then use it
			WAIT, // wait for the item to take effect, then finish
		};
		
		CTFBotUseItemImproved(CTFWeaponBase *item) :
			m_hItem(item) {}
		
		virtual ActionResult<CTFBot> OnStart(CTFBot *actor, Action<CTFBot> *action) override
		{
			if (cvar_debug.GetBool()) {
				DevMsg("[%8.3f] CTFBot%s(#%d): OnStart\n", gpGlobals->curtime, this->GetName(), ENTINDEX(actor));
			}
			
			this->m_State = State::FIRE;
			
			this->m_flSwitchTime = this->m_hItem->m_flNextPrimaryAttack;
			actor->PushRequiredWeapon(this->m_hItem);
			
			this->m_ctTimeout.Start(10.0f);
			
			return ActionResult<CTFBot>::Continue();
		}
		
		virtual ActionResult<CTFBot> Update(CTFBot *actor, float dt) override
		{
			if (this->m_ctTimeout.IsElapsed()) {
				if (cvar_debug.GetBool()) {
					DevMsg("[%8.3f] CTFBot%s(#%d): Timeout elapsed!\n", gpGlobals->curtime, this->GetName(), ENTINDEX(actor));
				}
				return ActionResult<CTFBot>::Done("Timed out");
			}
			
			switch (this->m_State) {
			
			case State::FIRE:
				if (gpGlobals->curtime >= this->m_flSwitchTime) {
					if (cvar_debug.GetBool()) {
						DevMsg("[%8.3f] CTFBot%s(#%d): Using item now\n", gpGlobals->curtime, this->GetName(), ENTINDEX(actor));
					}
					actor->PressFireButton(1.0f);
					this->m_State = State::WAIT;
				}
				break;
			
			case State::WAIT:
				if (this->IsDone(actor)) {
					if (cvar_debug.GetBool()) {
						DevMsg("[%8.3f] CTFBot%s(#%d): Done using item\n", gpGlobals->curtime, this->GetName(), ENTINDEX(actor));
					}
					return ActionResult<CTFBot>::Done("Item used");
				}
				break;
			
			}
			
			return ActionResult<CTFBot>::Continue();
		}
		
		virtual void OnEnd(CTFBot *actor, Action<CTFBot> *action) override
		{
			if (cvar_debug.GetBool()) {
				DevMsg("[%8.3f] CTFBot%s(#%d): OnEnd\n", gpGlobals->curtime, this->GetName(), ENTINDEX(actor));
			}
			
			actor->ReleaseFireButton();
			actor->PopRequiredWeapon();
		}
		
		virtual ActionResult<CTFBot> OnSuspend(CTFBot *actor, Action<CTFBot> *action) override
		{
			if (cvar_debug.GetBool()) {
				DevMsg("[%8.3f] CTFBot%s(#%d): OnSuspend\n", gpGlobals->curtime, this->GetName(), ENTINDEX(actor));
			}
			
			return ActionResult<CTFBot>::Done("Interrupted");
		}
		
	
	protected:
		virtual bool IsDone(CTFBot *actor) = 0;
		
		static bool IsPossible(CTFBot *actor)
		{
			/* the bot cannot actually PressFireButton in these cases */
			if (actor->m_Shared->IsControlStunned())             return false;
			if (actor->m_Shared->IsLoserStateStunned())          return false;
			if (actor->HasAttribute(CTFBot::ATTR_SUPPRESS_FIRE)) return false;
			
			return true;
		}
		
	private:
		CHandle<CTFWeaponBase> m_hItem;
		
		State m_State;
		float m_flSwitchTime;
		
		CountdownTimer m_ctTimeout;
	};
	
	
	class CTFBotUseBuffItem : public CTFBotUseItemImproved
	{
	public:
		CTFBotUseBuffItem(CTFWeaponBase *item) :
			CTFBotUseItemImproved(item) {}
		
		virtual const char *GetName() const override { return "UseBuffItem"; }
		
		static bool IsPossible(CTFBot *actor)
		{
			/* workaround for integer-division bug where, if mult_buff_duration > 10 (technically, if the final adjusted duration > 100 sec),
			 * then CTFPlayerShared::m_bRageDraining will be true, but CTFPlayerShared::m_flRageMeter will stay at 100.0f indefinitely!
			 * (basically: CTFBot::OpportunisticallyUseWeaponAbilities only checks CTFBuffItem::IsFull(), i.e. is m_flRageMeter >= 100.0f;
			 *  what we want is to ALSO check if rage is active via m_bRageDraining, and refuse to use the item if it is currently active) */
			if (actor->m_Shared->m_flRageMeter < 100.0f) return false;
			if (actor->m_Shared->m_bRageDraining)        return false;
			
			return CTFBotUseItemImproved::IsPossible(actor);
		}
		
	private:
		virtual bool IsDone(CTFBot *actor) override
		{
			return actor->m_Shared->m_bRageDraining;
		}
	};
	
	
	class CTFBotUseLunchBoxItem : public CTFBotUseItemImproved
	{
	public:
		enum class TauntState : int
		{
			BEFORE, // taunt has not yet begun
			DURING, // taunt is in progress
			AFTER,  // taunt has completed
		};
		
		CTFBotUseLunchBoxItem(CTFWeaponBase *item) :
			CTFBotUseItemImproved(item) {}
		
		virtual const char *GetName() const override { return "UseLunchBoxItem"; }
		
		virtual ActionResult<CTFBot> OnStart(CTFBot *actor, Action<CTFBot> *action) override
		{
			this->m_TauntState = TauntState::BEFORE;
			
			return CTFBotUseItemImproved::OnStart(actor, action);
		}
		
		using CTFBotUseItemImproved::IsPossible;
		
	private:
		virtual bool IsDone(CTFBot *actor) override
		{
			switch (this->m_TauntState) {
			
			case TauntState::BEFORE:
				if (actor->m_Shared->InCond(TF_COND_TAUNTING)) {
					this->m_TauntState = TauntState::DURING;
				}
				return false;
			
			case TauntState::DURING:
				if (!actor->m_Shared->InCond(TF_COND_TAUNTING)) {
					this->m_TauntState = TauntState::AFTER;
				}
				return false;
			
			case TauntState::AFTER:
				return true;
			
			}
			
			// should never happen
			return true;
		}
		
		TauntState m_TauntState;
	};
	
	
	DETOUR_DECL_MEMBER(Action<CTFBot> *, CTFBot_OpportunisticallyUseWeaponAbilities)
	{
		auto bot = reinterpret_cast<CTFBot *>(this);
		
		auto result = reinterpret_cast<CTFBotUseItem *>(DETOUR_MEMBER_CALL(CTFBot_OpportunisticallyUseWeaponAbilities)());
		if (result != nullptr) {
			CTFWeaponBase *item = result->m_hItem;
			if (item != nullptr) {
				if (item->GetWeaponID() == TF_WEAPON_BUFF_ITEM) {
					delete result;
					
					if (CTFBotUseBuffItem::IsPossible(bot)) {
						return new CTFBotUseBuffItem(item);
					} else {
						return nullptr;
					}
				}
				
				if (item->GetWeaponID() == TF_WEAPON_LUNCHBOX) {
					delete result;
					
					if (CTFBotUseLunchBoxItem::IsPossible(bot)) {
						return new CTFBotUseLunchBoxItem(item);
					} else {
						return nullptr;
					}
				}
			}
		}
		
		return result;
	}
	
	
	class CMod : public IMod
	{
	public:
		CMod() : IMod("AI:Improved_UseItem")
		{
			MOD_ADD_DETOUR_MEMBER(CTFBot_OpportunisticallyUseWeaponAbilities, "CTFBot::OpportunisticallyUseWeaponAbilities");
		}
	};
	CMod s_Mod;
	
	
	ConVar cvar_enable("sig_ai_improved_useitem", "0", FCVAR_NOTIFY,
		"Mod: use improved replacement for CTFBotUseItem",
		[](IConVar *pConVar, const char *pOldValue, float flOldValue){
			s_Mod.Toggle(static_cast<ConVar *>(pConVar)->GetBool());
		});
}

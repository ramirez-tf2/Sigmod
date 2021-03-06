#ifndef _INCLUDE_SIGSEGV_STUB_TRACE_H_
#define _INCLUDE_SIGSEGV_STUB_TRACE_H_

// Various somewhat-tracing-related stuff copy-n-pasted from SDK 2013


#include "stub/baseentity.h"


//-----------------------------------------------------------------------------
// class CFlaggedEntitiesEnum
//-----------------------------------------------------------------------------
// enumerate entities that match a set of edict flags into a static array
class CFlaggedEntitiesEnum : public IPartitionEnumerator
{
public:
	CFlaggedEntitiesEnum( CBaseEntity **pList, int listMax, int flagMask );

	// This gets called	by the enumeration methods with each element
	// that passes the test.
	virtual IterationRetval_t EnumElement( IHandleEntity *pHandleEntity );
	
	int GetCount() { return m_count; }
	bool AddToList( CBaseEntity *pEntity );
	
private:
	CBaseEntity		**m_pList;
	int				m_listMax;
	int				m_flagMask;
	int				m_count;
};


void DebugDrawLine( const Vector& vecAbsStart, const Vector& vecAbsEnd, int r, int g, int b, bool test, float duration );

int UTIL_EntitiesAlongRay( const Ray_t &ray, CFlaggedEntitiesEnum *pEnum );

int UTIL_EntitiesAlongRay( CBaseEntity **pList, int listMax, const Ray_t &ray, int flagMask );

void UTIL_TraceModel( const Vector &vecStart, const Vector &vecEnd, const Vector &hullMin, 
					  const Vector &hullMax, CBaseEntity *pentModel, int collisionGroup, trace_t *ptr );

bool PassServerEntityFilter( const IHandleEntity *pTouch, const IHandleEntity *pPass );

bool StandardFilterRules( IHandleEntity *pHandleEntity, int fContentsMask );


class CTraceFilterIgnorePlayers : public CTraceFilterSimple
{
public:
	CTraceFilterIgnorePlayers(const IHandleEntity *passentity, int collisionGroup) :
		CTraceFilterSimple(passentity, collisionGroup) {}
	
	virtual bool ShouldHitEntity(IHandleEntity *pHandleEntity, int contentsMask) override
	{
		CBaseEntity *pEntity = EntityFromEntityHandle(pHandleEntity);
		if (pEntity != nullptr && pEntity->IsPlayer()) return false;
		
		return CTraceFilterSimple::ShouldHitEntity(pHandleEntity, contentsMask);
	}
};


class CTraceFilterIgnoreTeammates : public CTraceFilterSimple
{
public:
	CTraceFilterIgnoreTeammates(const CBaseEntity *pEntity, int collisionGroup) :
		CTraceFilterSimple(pEntity, collisionGroup), m_iTeamNum(pEntity->GetTeamNumber()) {}
	
	virtual bool ShouldHitEntity(IHandleEntity *pHandleEntity, int contentsMask) override
	{
		CBaseEntity *pEntity = EntityFromEntityHandle(pHandleEntity);
		
		if ((pEntity->IsPlayer() || pEntity->IsCombatItem()) && (pEntity->GetTeamNumber() == this->m_iTeamNum || this->m_iTeamNum == TEAM_ANY)) return false;
		
		return CTraceFilterSimple::ShouldHitEntity(pHandleEntity, contentsMask);
	}
	
private:
	int m_iTeamNum;
};


class CTraceFilterIgnoreFriendlyCombatItems : public CTraceFilterSimple
{
public:
	CTraceFilterIgnoreFriendlyCombatItems(const CBaseEntity *pEntity, int collisionGroup, bool bNoChain = false) :
		CTraceFilterSimple(pEntity, collisionGroup), m_iTeamNum(pEntity->GetTeamNumber()), m_bNoChain(bNoChain) {}
	
	virtual bool ShouldHitEntity(IHandleEntity *pHandleEntity, int contentsMask) override
	{
		CBaseEntity *pEntity = EntityFromEntityHandle(pHandleEntity);
		
		if (pEntity->IsCombatItem()) {
			if (pEntity->GetTeamNumber() == this->m_iTeamNum) return false;
			
			if (this->m_bNoChain) return true;
		}
		
		return CTraceFilterSimple::ShouldHitEntity(pHandleEntity, contentsMask);
	}
	
private:
	int m_iTeamNum;
	bool m_bNoChain;
};


#endif

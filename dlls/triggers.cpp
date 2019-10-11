/***
*
*	Copyright (c) 1999, 2000 Valve LLC. All rights reserved.
*	
*	This product contains software technology licensed from Id 
*	Software, Inc. ("Id Technology").  Id Technology (c) 1996 Id Software, Inc. 
*	All Rights Reserved.
*
*   Use, distribution, and modification of this source code and/or resulting
*   object code is restricted to non-commercial enhancements to products from
*   Valve LLC.  All other use, distribution, or modification is prohibited
*   without written permission from Valve LLC.
*
****/
/*

===== triggers.cpp ========================================================

  spawn and use functions for editor-placed triggers              

*/

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "player.h"
#include "saverestore.h"
#include "trains.h"			// trigger_camera has train functionality
#include "gamerules.h"
#include "talkmonster.h"
#include "weapons.h" //LRC, for trigger_hevcharge
#include "movewith.h" //LRC
#include "locus.h" //LRC
//#include "hgrunt.h"
//#include "islave.h"

#include "triggers.h"

#define	SF_TRIGGER_PUSH_START_OFF	2//spawnflag that makes trigger_push spawn turned OFF
#define SF_TRIGGER_HURT_TARGETONCE	1// Only fire hurt target once
#define	SF_TRIGGER_HURT_START_OFF	2//spawnflag that makes trigger_hurt spawn turned OFF
#define	SF_TRIGGER_HURT_NO_CLIENTS	8// clients may not touch this trigger.
#define SF_TRIGGER_HURT_CLIENTONLYFIRE	16// trigger hurt will only fire its target if it is hurting a client
#define SF_TRIGGER_HURT_CLIENTONLYTOUCH 32// only clients may touch this trigger.

extern DLL_GLOBAL BOOL		g_fGameOver;

extern void SetMovedir(entvars_t* pev);
extern Vector VecBModelOrigin( entvars_t* pevBModel );

LINK_ENTITY_TO_CLASS( func_friction, CFrictionModifier );

// Global Savedata for changelevel friction modifier
TYPEDESCRIPTION	CFrictionModifier::m_SaveData[] = 
{
	DEFINE_FIELD( CFrictionModifier, m_frictionFraction, FIELD_FLOAT ),
};

IMPLEMENT_SAVERESTORE(CFrictionModifier,CBaseEntity);


// Modify an entity's friction
void CFrictionModifier :: Spawn( void )
{
	pev->solid = SOLID_TRIGGER;
	SET_MODEL(ENT(pev), STRING(pev->model));    // set size and link into world
	pev->movetype = MOVETYPE_NONE;
	SetTouch( ChangeFriction );
}


// Sets toucher's friction to m_frictionFraction (1.0 = normal friction)
void CFrictionModifier :: ChangeFriction( CBaseEntity *pOther )
{
	if ( pOther->pev->movetype != MOVETYPE_BOUNCEMISSILE && pOther->pev->movetype != MOVETYPE_BOUNCE )
		pOther->pev->friction = m_frictionFraction;
}



// Sets toucher's friction to m_frictionFraction (1.0 = normal friction)
void CFrictionModifier :: KeyValue( KeyValueData *pkvd )
{
	if (FStrEq(pkvd->szKeyName, "modifier"))
	{
		m_frictionFraction = atof(pkvd->szValue) / 100.0;
		pkvd->fHandled = TRUE;
	}
	else
		CBaseEntity::KeyValue( pkvd );
}


// This trigger will fire when the level spawns (or respawns if not fire once)
// It will check a global state before firing.  It supports delay and killtargets

#define SF_AUTO_FIREONCE		0x0001
#define SF_AUTO_FROMPLAYER		0x0002

LINK_ENTITY_TO_CLASS( trigger_auto, CAutoTrigger );

TYPEDESCRIPTION	CAutoTrigger::m_SaveData[] = 
{
	DEFINE_FIELD( CAutoTrigger, m_globalstate, FIELD_STRING ),
	DEFINE_FIELD( CAutoTrigger, triggerType, FIELD_INTEGER ),
};

IMPLEMENT_SAVERESTORE(CAutoTrigger,CBaseDelay);

void CAutoTrigger::KeyValue( KeyValueData *pkvd )
{
	if (FStrEq(pkvd->szKeyName, "globalstate"))
	{
		m_globalstate = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "triggerstate"))
	{
		int type = atoi( pkvd->szValue );
		switch( type )
		{
		case 0:
			triggerType = USE_OFF;
			break;
		case 2:
			triggerType = USE_TOGGLE;
			break;
		default:
			triggerType = USE_ON;
			break;
		}
		pkvd->fHandled = TRUE;
	}
	else
		CBaseDelay::KeyValue( pkvd );
}


void CAutoTrigger::Spawn( void )
{
	Precache();
}


void CAutoTrigger::Precache( void )
{
	// This gap between precache and nextthink could be the cause of a "flash" between worldspawn and the trigger
	// I am trying to have a trigger_camera from the very beginning, possibly with a quick fade-in.
	// The worldspawn fade-in is slow (you cannot modify the fade-in or hold rate).
	UTIL_DesiredThink( this ); //LRC - don't think until the player has spawned.
//	SetNextThink( 0.1 );
}


void CAutoTrigger::Think( void )
{
	if ( !m_globalstate || gGlobalState.EntityGetState( m_globalstate ) == GLOBAL_ON )
	{
		if (pev->spawnflags & SF_AUTO_FROMPLAYER)
		{
			CBaseEntity* pPlayer = UTIL_FindEntityByClassname(NULL, "player");
			if (pPlayer)
				SUB_UseTargets( pPlayer, triggerType, 0 );
			else
			{
				ALERT(at_error,"trigger_auto: \"From Player\" is ticked, but no player found! Delaying...\n");
				// We have to have player activated for trigger_camera, because we have to ensure the player exists
				// before we can take the POV (point of view) away from him (or her!).
				// Smaller intervals here are better (trigger from very start - particularly good for fade-ins
				// using env_fade rather than the worldspawn property (which does not allow the fade-in and hold
				// time to be defined).
				// However, smaller intervals mean that more thinking and messaging is done until the player is spawned...
				SetNextThink( 0.05 );
				return;
			}
		}
		else
			SUB_UseTargets( this, triggerType, 0 );
		if ( pev->spawnflags & SF_AUTO_FIREONCE )
			UTIL_Remove( this );
	}
}

#define SF_RELAY_FIREONCE		0x00000001
#define SF_RELAY_DEBUG			0x00000002
#define SF_RELAY_USESAME		0x80000000

LINK_ENTITY_TO_CLASS( trigger_relay, CTriggerRelay );

TYPEDESCRIPTION	CTriggerRelay::m_SaveData[] = 
{
	DEFINE_FIELD( CTriggerRelay, m_triggerType, FIELD_INTEGER ),
	DEFINE_FIELD( CTriggerRelay, m_sMaster, FIELD_STRING ),
};

IMPLEMENT_SAVERESTORE(CTriggerRelay,CBaseDelay);

void CTriggerRelay::KeyValue( KeyValueData *pkvd )
{
	if (FStrEq(pkvd->szKeyName, "master"))
	{
		m_sMaster = ALLOC_STRING(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "m_iszAltTarget"))
	{
		m_iszAltTarget = ALLOC_STRING(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "triggerstate"))
	{
		int type = atoi( pkvd->szValue );
		switch( type )
		{
		case 0:
			m_triggerType = USE_OFF;
			break;
		case 2:
			m_triggerType = USE_TOGGLE;
			break;
		case 4:
			m_triggerType = USE_KILL;
			break;
		case 5:
			m_triggerType = USE_SAME;
			break;
		case 7:
			m_triggerType = USE_SET;
			break;
		default:
			m_triggerType = USE_ON;
			break;
		}
		pkvd->fHandled = TRUE;
	}
	else
		CBaseDelay::KeyValue( pkvd );
}


void CTriggerRelay::Spawn( void )
{
}

void CTriggerRelay::Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	if (!UTIL_IsMasterTriggered(m_sMaster,pActivator))
	{
		if (m_iszAltTarget)
		{
			//FIXME: the alternate target should really use m_flDelay.
			if (pev->spawnflags & SF_RELAY_USESAME)
				FireTargets( STRING(m_iszAltTarget), pActivator, this, useType, 0 );
			else
				FireTargets( STRING(m_iszAltTarget), pActivator, this, m_triggerType, 0 );
			if (pev->spawnflags & SF_RELAY_DEBUG)
				ALERT(at_debug,"DEBUG: trigger_relay \"%s\" locked by master %s - fired alternate target %s\n",STRING(pev->targetname), STRING(m_sMaster), STRING(m_iszAltTarget));
			if ( pev->spawnflags & SF_RELAY_FIREONCE )
			{
				if (pev->spawnflags & SF_RELAY_DEBUG)
					ALERT(at_debug, "trigger_relay \"%s\" removes itself.\n");
				UTIL_Remove( this );
			}
		}
		else if (pev->spawnflags & SF_RELAY_DEBUG)
			ALERT(at_debug,"DEBUG: trigger_relay \"%s\" wasn't activated: locked by master %s\n",STRING(pev->targetname), STRING(m_sMaster));
		return;
	}
	if (pev->spawnflags & SF_RELAY_DEBUG)
	{
		ALERT(at_debug,"DEBUG: trigger_relay \"%s\" was sent %s",STRING(pev->targetname), GetStringForUseType(useType));
		if (pActivator)
		{
			if (FStringNull(pActivator->pev->targetname))
				ALERT(at_debug," from \"%s\"", STRING(pActivator->pev->classname));
			else
				ALERT(at_debug," from \"%s\"", STRING(pActivator->pev->targetname));
		}
		else
			ALERT(at_debug," (no locus)");
	}

	if (FStringNull(pev->target) && !m_iszKillTarget)
	{
		if (pev->spawnflags & SF_RELAY_DEBUG) ALERT(at_debug, ".\n");
		return;
	}

	if (pev->message)
		value = CalcLocus_Ratio(pActivator, STRING(pev->message));

	if (m_triggerType == USE_SAME)
	{
		if (pev->spawnflags & SF_RELAY_DEBUG)
		{
			if (m_flDelay)
				ALERT(at_debug,": will send %s(same) in %f seconds.\n",GetStringForUseType(useType),m_flDelay);
			else
				ALERT(at_debug,": sending %s(same) now.\n",GetStringForUseType(useType));
		}
		SUB_UseTargets( pActivator, useType, value );
	}
	else if (m_triggerType == USE_SET)
	{
		if (pev->spawnflags & SF_RELAY_DEBUG)
		{
			if (m_flDelay)
				ALERT(at_debug,": will send ratio %f in %f seconds.\n",value,m_flDelay);
			else
				ALERT(at_debug,": sending ratio %f now.\n",value);
		}
		SUB_UseTargets( pActivator, useType, value );
	}
	else
	{
		if (pev->spawnflags & SF_RELAY_DEBUG)
		{
			if (m_flDelay)
				ALERT(at_debug,": will send %s in %f seconds.\n",GetStringForUseType(m_triggerType),m_flDelay);
			else
				ALERT(at_debug,": sending %s now.\n",GetStringForUseType(m_triggerType));
		}
		SUB_UseTargets( pActivator, m_triggerType, 0 );
	}
	if ( pev->spawnflags & SF_RELAY_FIREONCE )
	{
		if (pev->spawnflags & SF_RELAY_DEBUG)
			ALERT(at_debug, "trigger_relay \"%s\" removes itself.\n");
		UTIL_Remove( this );
	}
}

//===========================================
//LRC - trigger_rottest, temporary new entity
//===========================================
class CTriggerRotTest : public CBaseDelay
{
public:
	void PostSpawn( void );
//	void Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value );
	void Think( void );

	virtual int		Save( CSave &save );
	virtual int		Restore( CRestore &restore );

	static	TYPEDESCRIPTION m_SaveData[];

private:
	CBaseEntity* m_pMarker;
	CBaseEntity* m_pReference;
	CBaseEntity* m_pBridge;
	CBaseEntity* m_pHinge;
};
LINK_ENTITY_TO_CLASS( trigger_rottest, CTriggerRotTest );

TYPEDESCRIPTION	CTriggerRotTest::m_SaveData[] = 
{
	DEFINE_FIELD( CTriggerRotTest, m_pMarker, FIELD_CLASSPTR ),
	DEFINE_FIELD( CTriggerRotTest, m_pReference, FIELD_CLASSPTR ),
	DEFINE_FIELD( CTriggerRotTest, m_pBridge, FIELD_CLASSPTR ),
	DEFINE_FIELD( CTriggerRotTest, m_pHinge, FIELD_CLASSPTR ),
};

IMPLEMENT_SAVERESTORE(CTriggerRotTest,CBaseDelay);

void CTriggerRotTest::PostSpawn( void )
{
	m_pMarker = UTIL_FindEntityByTargetname(NULL, STRING(pev->target));
	m_pReference = UTIL_FindEntityByTargetname(NULL, STRING(pev->netname));
	m_pBridge = UTIL_FindEntityByTargetname(NULL, STRING(pev->noise1));
	m_pHinge = UTIL_FindEntityByTargetname(NULL, STRING(pev->message));
	pev->sanity = 0; // initial angle
	if (pev->armortype == 0) //angle offset
		pev->armortype = 30;
	SetNextThink( 1 );
}

void CTriggerRotTest::Think( void )
{
//	ALERT(at_debug, "Using angle = %.2f\n", pev->armorvalue);
	if (m_pReference)
	{
		m_pReference->pev->origin = pev->origin;
		m_pReference->pev->origin.x = m_pReference->pev->origin.x + pev->health;
//		ALERT(at_debug, "Set Reference = %.2f %.2f %.2f\n", m_pReference->pev->origin.x, m_pReference->pev->origin.y, m_pReference->pev->origin.z);
	}
	if (m_pMarker)
	{
		Vector vecTemp = UTIL_AxisRotationToVec( (m_pHinge->pev->origin - pev->origin).Normalize(), pev->sanity );
		m_pMarker->pev->origin = pev->origin + pev->health * vecTemp;

//		ALERT(at_debug, "vecTemp = %.2f %.2f %.2f\n", vecTemp.x, vecTemp.y, vecTemp.z);
//		ALERT(at_debug, "Set Marker = %.2f %.2f %.2f\n", m_pMarker->pev->origin.x, m_pMarker->pev->origin.y, m_pMarker->pev->origin.z);
	}
	if (m_pBridge)
	{
		Vector vecTemp = UTIL_AxisRotationToAngles( (m_pHinge->pev->origin - pev->origin).Normalize(), pev->sanity );
		m_pBridge->pev->origin = pev->origin;
		m_pBridge->pev->angles = vecTemp;

//		ALERT(at_debug, "vecTemp = %.2f %.2f %.2f\n", vecTemp.x, vecTemp.y, vecTemp.z);
//		ALERT(at_debug, "Set Marker = %.2f %.2f %.2f\n", m_pMarker->pev->origin.x, m_pMarker->pev->origin.y, m_pMarker->pev->origin.z);
	}
	pev->sanity += pev->armortype * 0.1;
	SetNextThink( 0.1 );
}

//**********************************************************
// The Multimanager Entity - when fired, will fire up to 16 targets 
// at specified times.
// FLAG:		THREAD (create clones when triggered)
// FLAG:		CLONE (this is a clone for a threaded execution)

LINK_ENTITY_TO_CLASS( multi_manager, CMultiManager );

// Global Savedata for multi_manager
TYPEDESCRIPTION	CMultiManager::m_SaveData[] = 
{
	DEFINE_FIELD( CMultiManager, m_cTargets, FIELD_INTEGER ),
	DEFINE_FIELD( CMultiManager, m_index, FIELD_INTEGER ),
	DEFINE_FIELD( CMultiManager, m_iState, FIELD_INTEGER ), //LRC
	DEFINE_FIELD( CMultiManager, m_iMode, FIELD_INTEGER ), //LRC
	DEFINE_FIELD( CMultiManager, m_startTime, FIELD_TIME ),
	DEFINE_FIELD( CMultiManager, m_triggerType, FIELD_INTEGER ), //LRC
	DEFINE_ARRAY( CMultiManager, m_iTargetName, FIELD_STRING, MAX_MULTI_TARGETS ),
	DEFINE_ARRAY( CMultiManager, m_flTargetDelay, FIELD_FLOAT, MAX_MULTI_TARGETS ),
	DEFINE_FIELD( CMultiManager, m_sMaster, FIELD_STRING ), //LRC
	DEFINE_FIELD( CMultiManager, m_hActivator, FIELD_EHANDLE ),
	DEFINE_FIELD( CMultiManager, m_flWait, FIELD_FLOAT ), //LRC
	DEFINE_FIELD( CMultiManager, m_flMaxWait, FIELD_FLOAT ), //LRC
	DEFINE_FIELD( CMultiManager, m_iszThreadName, FIELD_STRING ), //LRC
	DEFINE_FIELD( CMultiManager, m_iszLocusThread, FIELD_STRING ), //LRC
};

IMPLEMENT_SAVERESTORE(CMultiManager,CBaseEntity);

void CMultiManager :: KeyValue( KeyValueData *pkvd )
{
	
	// UNDONE: Maybe this should do something like this:
	//CBaseToggle::KeyValue( pkvd );
	// if ( !pkvd->fHandled )
	// ... etc.
	//
	//LRC- that would support Delay, Killtarget, Lip, Distance, Wait and Master.
	// Wait is already supported. I've added master here. To hell with the others.

	if (FStrEq(pkvd->szKeyName, "wait"))
	{
		m_flWait = atof(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "maxwait"))
	{
		m_flMaxWait = atof(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "master")) //LRC
	{
		m_sMaster = ALLOC_STRING(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "m_iszThreadName")) //LRC
	{
		m_iszThreadName = ALLOC_STRING(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "m_iszLocusThread")) //LRC
	{
		m_iszLocusThread = ALLOC_STRING(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "mode")) //LRC
	{
		m_iMode = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "triggerstate")) //LRC
	{
		switch( atoi( pkvd->szValue ) )
		{
			case 4:  pev->spawnflags |= SF_MULTIMAN_SAMETRIG;   break;
			case 1:	 m_triggerType = USE_ON;     break; //LRC- yes, this algorithm is different
			case 2:	 m_triggerType = USE_OFF;    break; //from the trigger_relay equivalent-
			case 3:  m_triggerType = USE_KILL;   break; //trigger_relay's got to stay backwards
			default: m_triggerType = USE_TOGGLE; break; //compatible.
		}
		pev->spawnflags |= SF_MULTIMAN_TRIGCHOSEN;
		pkvd->fHandled = TRUE;
	}
	else // add this field to the target list
	{
		// this assumes that additional fields are targetnames and their values are delay values.
		if ( m_cTargets < MAX_MULTI_TARGETS )
		{
			char tmp[128];

			UTIL_StripToken( pkvd->szKeyName, tmp );
			m_iTargetName [ m_cTargets ] = ALLOC_STRING( tmp );
			m_flTargetDelay [ m_cTargets ] = atof (pkvd->szValue);
			m_cTargets++;
			pkvd->fHandled = TRUE;
		}
		else //LRC
		{
			m_cTargets++;
		}
	}
}


void CMultiManager :: Spawn( void )
{
	pev->solid = SOLID_NOT;
	SetUse ( ManagerUse );
	SetThink ( ManagerThink);

	m_iState = STATE_OFF;

	//LRC
	if (m_cTargets > MAX_MULTI_TARGETS)
	{
		ALERT(at_debug, "WARNING: multi_manager \"%s\" has too many targets (limit is %d, it has %d)\n", STRING(pev->targetname), MAX_MULTI_TARGETS, m_cTargets);
		m_cTargets = MAX_MULTI_TARGETS;
	}

	if (!FBitSet(pev->spawnflags,SF_MULTIMAN_TRIGCHOSEN))
		m_triggerType = USE_TOGGLE;
	
	// Sort targets
	// Quick and dirty bubble sort
	int swapped = 1;

	while ( swapped )
	{
		swapped = 0;
		for ( int i = 1; i < m_cTargets; i++ )
		{
			if ( m_flTargetDelay[i] < m_flTargetDelay[i-1] )
			{
				// Swap out of order elements
				int name = m_iTargetName[i];
				float delay = m_flTargetDelay[i];
				m_iTargetName[i] = m_iTargetName[i-1];
				m_flTargetDelay[i] = m_flTargetDelay[i-1];
				m_iTargetName[i-1] = name;
				m_flTargetDelay[i-1] = delay;
				swapped = 1;
			}
		}
	}

	if ( pev->spawnflags & SF_MULTIMAN_SPAWNFIRE)
	{
		SetThink ( UseThink );
		SetUse( NULL );
		UTIL_DesiredThink( this );
	}
}


BOOL CMultiManager::HasTarget( string_t targetname )
{ 
	for ( int i = 0; i < m_cTargets; i++ )
		if ( FStrEq(STRING(targetname), STRING(m_iTargetName[i])) )
			return TRUE;
	
	return FALSE;
}


void CMultiManager :: UseThink ( void )
{
	SetThink( ManagerThink );
	SetUse( ManagerUse );
	Use( this, this, USE_TOGGLE, 0 );
}

// Designers were using this to fire targets that may or may not exist -- 
// so I changed it to use the standard target fire code, made it a little simpler.
void CMultiManager :: ManagerThink ( void )
{
	//LRC- different manager modes
	if (m_iMode)
	{
		// special triggers have no time delay, so we can clean up before firing
		if (pev->spawnflags & SF_MULTIMAN_LOOP)
		{
//			ALERT(at_debug,"Manager loops back\n");
			// if it's a loop, start again!
			if (m_flMaxWait) //LRC- random time to wait?
				m_startTime = RANDOM_FLOAT( m_flWait, m_flMaxWait );
			else if (m_flWait) //LRC- constant time to wait?
				m_startTime = m_flWait;
			else //LRC- just start immediately.
				m_startTime = 0;
			if (pev->spawnflags & SF_MULTIMAN_DEBUG)
				ALERT(at_debug, "DEBUG: multi_manager \"%s\": restarting loop.\n", STRING(pev->targetname));
			SetNextThink( m_startTime );
			m_startTime = m_fNextThink;
			m_iState = STATE_TURN_ON;
//			ALERT(at_debug, "MM loops, nextthink %f\n", m_fNextThink);
		}
		else if ( IsClone() || pev->spawnflags & SF_MULTIMAN_ONLYONCE )
		{
			if (pev->spawnflags & SF_MULTIMAN_DEBUG)
				ALERT(at_debug, "DEBUG: multi_manager \"%s\": killed.\n", STRING(pev->targetname));
			SetThink( SUB_Remove );
			SetNextThink( 0.1 );
			SetUse( NULL );
		}
		else
		{
			if (pev->spawnflags & SF_MULTIMAN_DEBUG)
				ALERT(at_debug, "DEBUG: multi_manager \"%s\": last burst.\n", STRING(pev->targetname));
			m_iState = STATE_OFF;
			SetThink( NULL );
			SetUse ( ManagerUse );// allow manager re-use 
		}

		int i = 0;
		if (m_iMode == MM_MODE_CHOOSE) // choose one of the members, and fire it
		{
			float total = 0;
			for (i = 0; i < m_cTargets; i++) { total += m_flTargetDelay[i]; }

			// no weightings given, so just pick one.
			if (total == 0)
			{
				const char *sTarg = STRING(m_iTargetName[RANDOM_LONG(0, m_cTargets-1)]);
				if (pev->spawnflags & SF_MULTIMAN_DEBUG)
					ALERT(at_debug, "DEBUG: multi_manager \"%s\": firing \"%s\" (random choice).\n", STRING(pev->targetname), sTarg);
				FireTargets(sTarg,m_hActivator,this,m_triggerType,0);
			}
			else // pick one by weighting
			{
				float chosen = RANDOM_FLOAT(0,total);
				float curpos = 0;
				for (i = 0; i < m_cTargets; i++)
				{
					curpos += m_flTargetDelay[i];
					if (curpos >= chosen)
					{
						if (pev->spawnflags & SF_MULTIMAN_DEBUG)
							ALERT(at_debug, "DEBUG: multi_manager \"%s\": firing \"%s\" (weighted random choice).\n", STRING(pev->targetname), STRING(m_iTargetName[i]));
						FireTargets(STRING(m_iTargetName[i]),m_hActivator,this,m_triggerType,0);
						break;
					}
				}
			}
		}
		else if (m_iMode == MM_MODE_PERCENT) // try to call each member
		{
			for (i = 0; i < m_cTargets; i++)
			{
				if ( RANDOM_LONG( 0, 100 ) <= m_flTargetDelay[i] )
				{
					if (pev->spawnflags & SF_MULTIMAN_DEBUG)
						ALERT(at_debug, "DEBUG: multi_manager \"%s\": firing \"%s\" (%f%% chance).\n", STRING(pev->targetname), STRING(m_iTargetName[i]), m_flTargetDelay[i]);
					FireTargets(STRING(m_iTargetName[i]),m_hActivator,this,m_triggerType,0);
				}
			}
		}
		else if (m_iMode == MM_MODE_SIMULTANEOUS)
		{
			for (i = 0; i < m_cTargets; i++)
			{
				if (pev->spawnflags & SF_MULTIMAN_DEBUG)
					ALERT(at_debug, "DEBUG: multi_manager \"%s\": firing \"%s\" (simultaneous).\n", STRING(pev->targetname), STRING(m_iTargetName[i]));
				FireTargets(STRING(m_iTargetName[i]),m_hActivator,this,m_triggerType,0);
			}
		}

		return;
	}

// ok, so m_iMode is 0; we're doing normal time-based stuff.
	
	float	time;
	int		finalidx;
	int		index = m_index; // store the current index

	time = gpGlobals->time - m_startTime;

//	ALERT(at_debug,"Manager think for time %f\n",time);

	// find the last index we're going to fire this time
	finalidx = m_index;
	while (finalidx < m_cTargets && m_flTargetDelay[ finalidx ] <= time)
		finalidx++;

	if ( finalidx >= m_cTargets )// will we finish firing targets this time?
	{
		if (pev->spawnflags & SF_MULTIMAN_LOOP)
		{
//			ALERT(at_debug,"Manager loops back\n");
			// if it's a loop, start again!
			m_index = 0;
			if (m_flMaxWait) //LRC- random time to wait?
			{
				m_startTime = RANDOM_FLOAT( m_flWait, m_flMaxWait );
				m_iState = STATE_TURN_ON; // while we're waiting, we're in state TURN_ON
			}
			else if (m_flWait) //LRC- constant time to wait?
			{
				m_startTime = m_flWait;
				m_iState = STATE_TURN_ON;
			}
			else //LRC- just start immediately.
			{
				m_startTime = 0;
				m_iState = STATE_ON;
			}
			if (pev->spawnflags & SF_MULTIMAN_DEBUG)
				ALERT(at_debug, "DEBUG: multi_manager \"%s\": restarting loop.\n", STRING(pev->targetname));
			SetNextThink( m_startTime );
			m_startTime += gpGlobals->time;
		}
		else
		{
			m_iState = STATE_OFF; //LRC- STATE_OFF means "yes, we've finished".
			if ( IsClone() || pev->spawnflags & SF_MULTIMAN_ONLYONCE )
			{
				SetThink( SUB_Remove );
				SetNextThink( 0.1 );
				SetUse( NULL );
				// Cthulhu: but we still need to do our last triggers...
				while ( index < m_cTargets && m_flTargetDelay[ index ] <= time )
				{
			//		ALERT(at_debug,"Manager sends %d to %s\n",m_triggerType,STRING(m_iTargetName[m_index]));
					FireTargets( STRING( m_iTargetName[ index ] ), m_hActivator, this, m_triggerType, 0 );
					index++;
				}

				if (pev->spawnflags & SF_MULTIMAN_DEBUG)
					ALERT(at_debug, "DEBUG: multi_manager \"%s\": killed.\n", STRING(pev->targetname));
				return;
			}
			else
			{
				SetThink( NULL );
				SetUse ( ManagerUse );// allow manager re-use 
				if (pev->spawnflags & SF_MULTIMAN_DEBUG)
					ALERT(at_debug, "DEBUG: multi_manager \"%s\": last burst.\n", STRING(pev->targetname));
			}
		}
	}
	else
	{
		m_index = finalidx;
		m_iState = STATE_ON; //LRC- while we're in STATE_ON we're firing targets, and haven't finished yet.
		AbsoluteNextThink( m_startTime + m_flTargetDelay[ m_index ] );
	}

	// we don't do stuff while master is not triggered
	if (!UTIL_IsMasterTriggered(m_sMaster,m_hActivator))
		return;

	while ( index < m_cTargets && m_flTargetDelay[ index ] <= time )
	{
//		ALERT(at_debug,"Manager sends %d to %s\n",m_triggerType,STRING(m_iTargetName[m_index]));
		if (pev->spawnflags & SF_MULTIMAN_DEBUG)
			ALERT(at_debug, "DEBUG: multi_manager \"%s\": firing \"%s\".\n", STRING(pev->targetname), STRING( m_iTargetName[ index ] ));
		FireTargets( STRING( m_iTargetName[ index ] ), m_hActivator, this, m_triggerType, 0 );
		index++;
	}
}

CMultiManager *CMultiManager::Clone( void )
{
	CMultiManager *pMulti = GetClassPtr( (CMultiManager *)NULL );

	edict_t *pEdict = pMulti->pev->pContainingEntity;
	memcpy( pMulti->pev, pev, sizeof(*pev) );
	pMulti->pev->pContainingEntity = pEdict;

	pMulti->pev->spawnflags |= SF_MULTIMAN_CLONE;
	pMulti->m_cTargets = m_cTargets;
	if (m_iszThreadName) pMulti->pev->targetname = m_iszThreadName; //LRC
	pMulti->m_triggerType = m_triggerType; //LRC
	pMulti->m_iMode = m_iMode; //LRC
	pMulti->m_sMaster = m_sMaster; //LRC
	pMulti->m_flWait = m_flWait; //LRC
	pMulti->m_flMaxWait = m_flMaxWait; //LRC
	memcpy( pMulti->m_iTargetName, m_iTargetName, sizeof( m_iTargetName ) );
	memcpy( pMulti->m_flTargetDelay, m_flTargetDelay, sizeof( m_flTargetDelay ) );

	return pMulti;
}


// The USE function builds the time table and starts the entity thinking.
void CMultiManager :: ManagerUse ( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	if (pev->spawnflags & SF_MULTIMAN_LOOP)
	{
		if (m_iState != STATE_OFF) // if we're on, or turning on...
		{
			if (useType != USE_ON) // ...then turn it off if we're asked to.
			{
				if (pev->spawnflags & SF_MULTIMAN_DEBUG)
					ALERT(at_debug, "DEBUG: multi_manager \"%s\": Loop halted on request.\n", STRING(pev->targetname));
				m_iState = STATE_OFF;
				if ( IsClone() || pev->spawnflags & SF_MULTIMAN_ONLYONCE )
				{
					SetThink( SUB_Remove );
					SetNextThink( 0.1 );
					SetUse( NULL );
					if (pev->spawnflags & SF_MULTIMAN_DEBUG)
						ALERT(at_debug, "DEBUG: multi_manager \"%s\": loop halted (removing).\n", STRING(pev->targetname));
				}
				else
				{
					SetThink( NULL );
					if (pev->spawnflags & SF_MULTIMAN_DEBUG)
						ALERT(at_debug, "DEBUG: multi_manager \"%s\": loop halted.\n", STRING(pev->targetname));
				}
			}
			// else we're already on and being told to turn on, so do nothing.
			else if (pev->spawnflags & SF_MULTIMAN_DEBUG)
				ALERT(at_debug, "DEBUG: multi_manager \"%s\": Loop already active.\n", STRING(pev->targetname));
			return;
		}
		else if (useType == USE_OFF) // it's already off
		{
			if (pev->spawnflags & SF_MULTIMAN_DEBUG)
				ALERT(at_debug, "DEBUG: multi_manager \"%s\": Loop already inactive.\n", STRING(pev->targetname));
			return;
		}
		// otherwise, start firing targets as normal.
	}
//	ALERT(at_debug,"Manager used, targetting [");
//	for (int i = 0; i < m_cTargets; i++)
//	{
//		ALERT(at_debug," %s(%f)",STRING(m_iTargetName[i]),m_flTargetDelay[i]);
//	}
//	ALERT(at_debug," ]\n");

	//LRC- "master" support
	if (!UTIL_IsMasterTriggered(m_sMaster,pActivator))
	{
		if (pev->spawnflags & SF_MULTIMAN_DEBUG)
			ALERT(at_debug, "DEBUG: multi_manager \"%s\": Can't trigger, locked by master \"%s\".\n", STRING(pev->targetname), STRING(m_sMaster));
		return;
	}

	// In multiplayer games, clone the MM and execute in the clone (like a thread)
	// to allow multiple players to trigger the same multimanager
	if ( ShouldClone() )
	{
		CMultiManager *pClone = Clone();
		if (pev->spawnflags & SF_MULTIMAN_DEBUG)
			ALERT(at_debug, "DEBUG: multi_manager \"%s\": Creating clone.\n", STRING(pev->targetname));
		pClone->ManagerUse( pActivator, pCaller, useType, value );
		if (m_iszLocusThread)
			FireTargets( STRING(m_iszLocusThread), pClone, this, USE_TOGGLE, 0 );
		return;
	}

	m_hActivator = pActivator;
	m_index = 0;

	if (m_flMaxWait) //LRC- random time to wait?
	{
		m_startTime = RANDOM_FLOAT( m_flWait, m_flMaxWait );
		m_iState = STATE_TURN_ON; // while we're waiting, we're in state TURN_ON
	}
	else if (m_flWait) //LRC- constant time to wait?
	{
		m_startTime = m_flWait;
		m_iState = STATE_TURN_ON;
	}
	else //LRC- just start immediately.
	{
		m_startTime = 0;
		m_iState = STATE_ON;
	}

	if (m_cTargets > 0 && !m_iMode && m_flTargetDelay[0] < 0)
	{
		// negative wait on the first target?
		m_startTime += m_flTargetDelay[0];
	}

	if (pev->spawnflags & SF_MULTIMAN_SAMETRIG) //LRC
		m_triggerType = useType;

	if (pev->spawnflags & SF_MULTIMAN_LOOP)
		SetUse( ManagerUse ); // clones won't already have this set
	else
		SetUse( NULL );// disable use until all targets have fired

	if (m_startTime > 0)
	{
		if (pev->spawnflags & SF_MULTIMAN_DEBUG)
			ALERT(at_debug, "DEBUG: multi_manager \"%s\": Begin in %f seconds.\n", STRING(pev->targetname), m_startTime);
		SetThink ( ManagerThink );
		SetNextThink( m_startTime );
		m_startTime += gpGlobals->time;
	}
	else
	{
		m_startTime += gpGlobals->time;
		SetThink ( ManagerThink );
		ManagerThink();
	}
}

#if _DEBUG
void CMultiManager :: ManagerReport ( void )
{
	int	cIndex;

	for ( cIndex = 0 ; cIndex < m_cTargets ; cIndex++ )
	{
		ALERT ( at_debug, "%s %f\n", STRING(m_iTargetName[cIndex]), m_flTargetDelay[cIndex] );
	}
}
#endif

//***********************************************************
//LRC- multi_watcher entity: useful? Well, I think it is. And I'm worth it. :)
//***********************************************************



LINK_ENTITY_TO_CLASS( multi_watcher, CStateWatcher );
LINK_ENTITY_TO_CLASS( watcher, CStateWatcher );

TYPEDESCRIPTION	CStateWatcher::m_SaveData[] = 
{
	DEFINE_FIELD( CStateWatcher, m_fLogic, FIELD_INTEGER ),
	DEFINE_FIELD( CStateWatcher, m_cTargets, FIELD_INTEGER ),
	DEFINE_ARRAY( CStateWatcher, m_iTargetName, FIELD_STRING, MAX_MULTI_TARGETS ),
//	DEFINE_ARRAY( CStateWatcher, m_pTargetEnt, FIELD_CLASSPTR, MAX_MULTI_TARGETS ),
};

IMPLEMENT_SAVERESTORE(CStateWatcher,CBaseToggle);

void CStateWatcher :: KeyValue( KeyValueData *pkvd )
{
	char tmp[128];
    if (FStrEq(pkvd->szKeyName, "m_fLogic"))
	{
		m_fLogic = atof(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
    else if (FStrEq(pkvd->szKeyName, "m_iszWatch"))
	{
		if ( m_cTargets < MAX_MULTI_TARGETS )
		{
			m_iTargetName [ m_cTargets ] = ALLOC_STRING(pkvd->szValue);
			m_cTargets++;
			pkvd->fHandled = TRUE;
		}
		else
		{
			ALERT(at_debug,"%s: Too many targets for %s \"%s\" (limit is %d)\n",pkvd->szKeyName,STRING(pev->classname),STRING(pev->targetname), MAX_MULTI_TARGETS);
		}
	}
	else // add this field to the target list
	{
		// this assumes that additional fields are targetnames and their values are delay values.
		if ( m_cTargets < MAX_MULTI_TARGETS )
		{
			UTIL_StripToken( pkvd->szKeyName, tmp );
			m_iTargetName [ m_cTargets ] = ALLOC_STRING( tmp );
			m_cTargets++;
			pkvd->fHandled = TRUE;
		}
		else
		{
			ALERT(at_debug,"%s: Too many targets for %s \"%s\" (limit is %d)\n",pkvd->szKeyName,STRING(pev->classname),STRING(pev->targetname), MAX_MULTI_TARGETS);
		}
	}
}

void CStateWatcher :: Spawn ( void )
{
	pev->solid = SOLID_NOT;
	if (pev->target)
		SetNextThink( 0.5 );
}

STATE CStateWatcher :: GetState( void )
{
	if (EvalLogic( NULL ))
		return STATE_ON;
	else
		return STATE_OFF;
}

STATE CStateWatcher :: GetState( CBaseEntity *pActivator )
{
//	if (pActivator)
//		ALERT(at_debug, "GetState( %s \"%s\" )\n", STRING(pActivator->pev->classname), STRING(pActivator->pev->targetname));
//	else
//		ALERT(at_debug, "GetState( NULL )\n");
	if (EvalLogic( pActivator ))
		return STATE_ON;
	else
		return STATE_OFF;
}

void CStateWatcher :: Think ( void )
{
	SetNextThink( 0.1 );
	int oldflag = pev->spawnflags & SF_SWATCHER_VALID;

	if (EvalLogic(NULL))
		pev->spawnflags |= SF_SWATCHER_VALID;
	else
		pev->spawnflags &= ~SF_SWATCHER_VALID;

	if ((pev->spawnflags & SF_SWATCHER_VALID) != oldflag)
	{
		// the update changed my state...

		if (oldflag)
		{
			// ...to off. Send "off".
//			ALERT(at_debug,"%s turns off\n",STRING(pev->classname));
			if (!FBitSet(pev->spawnflags, SF_SWATCHER_DONTSEND_OFF))
			{
				if (pev->spawnflags & SF_SWATCHER_SENDTOGGLE)
					SUB_UseTargets(this, USE_TOGGLE, 0);
				else
					SUB_UseTargets(this, USE_OFF, 0);
			}
		}
		else
		{
			// ...to on. Send "on".
//			ALERT(at_debug,"%s turns on\n",STRING(pev->classname));
			if (!FBitSet(pev->spawnflags, SF_SWATCHER_DONTSEND_ON))
			{
				if (pev->spawnflags & SF_SWATCHER_SENDTOGGLE)
					SUB_UseTargets(this, USE_TOGGLE, 0);
				else
					SUB_UseTargets(this, USE_ON, 0);
			}
		}
	}
}

BOOL CStateWatcher :: EvalLogic ( CBaseEntity *pActivator )
{
	int i;
	BOOL b;
	BOOL xorgot = FALSE;

	CBaseEntity* pEntity;

	for (i = 0; i < m_cTargets; i++)
	{
//		if (m_pTargetEnt[i] == NULL)
//		{
//			pEntity = m_pTargetEnt[i];
//		}
//		else
//		{
			pEntity = UTIL_FindEntityByTargetname(NULL,STRING(m_iTargetName[i]), pActivator);
			if (pEntity != NULL)
			{
//				if ((STRING(m_iTargetName[i]))[0] != '*') // don't cache alias values
//				{
//					//ALERT(at_debug,"Watcher: entity %s cached\n",STRING(m_iTargetName[i]));
//					m_pTargetEnt[i] = pEntity;
//				}
				//else
					//ALERT(at_debug,"Watcher: aliased entity %s not cached\n",STRING(m_iTargetName[i]));
			}
			else
			{
				//ALERT(at_debug,"Watcher: missing entity %s\n",STRING(m_iTargetName[i]));
				continue; // couldn't find this entity; don't do the test.
			}
//		}

		b = FALSE;
		switch (pEntity->GetState())
		{
		case STATE_ON:		 if (!(pev->spawnflags & SF_SWATCHER_NOTON))	b = TRUE; break;
		case STATE_OFF:		 if (pev->spawnflags & SF_SWATCHER_OFF)			b = TRUE; break;
		case STATE_TURN_ON:	 if (pev->spawnflags & SF_SWATCHER_TURN_ON)		b = TRUE; break;
		case STATE_TURN_OFF: if (pev->spawnflags & SF_SWATCHER_TURN_ON)		b = TRUE; break;
		case STATE_IN_USE:	 if (pev->spawnflags & SF_SWATCHER_IN_USE)		b = TRUE; break;
		}
		// handle the states for this logic mode
		if (b)
		{
			switch (m_fLogic)
			{
			case SWATCHER_LOGIC_OR:
//				ALERT(at_debug,"b is TRUE, OR returns true\n");
				return TRUE;
			case SWATCHER_LOGIC_NOR:
//				ALERT(at_debug,"b is TRUE, NOR returns false\n");
				return FALSE;
			case SWATCHER_LOGIC_XOR:
//				ALERT(at_debug,"b is TRUE, XOR\n");
				if (xorgot) return FALSE;
				xorgot = TRUE;
				break;
			case SWATCHER_LOGIC_XNOR:
//				ALERT(at_debug,"b is TRUE, XNOR\n");
				if (xorgot) return TRUE;
				xorgot = TRUE;
				break;
			}
		}
		else // b is false
		{
			switch (m_fLogic)
			{
			case SWATCHER_LOGIC_AND:
//				ALERT(at_debug,"b is FALSE, AND returns false\n");
				return FALSE;
			case SWATCHER_LOGIC_NAND:
//				ALERT(at_debug,"b is FALSE, NAND returns true\n");
				return TRUE;
			}
		}
	}
// handle the default cases for each logic mode
	switch (m_fLogic)
	{
	case SWATCHER_LOGIC_AND:
	case SWATCHER_LOGIC_NOR:
//		ALERT(at_debug,"final, AND/NOR returns true\n");
		return TRUE;
	case SWATCHER_LOGIC_XOR:
//		ALERT(at_debug,"final, XOR\n");
		return xorgot;
	case SWATCHER_LOGIC_XNOR:
//		ALERT(at_debug,"final, XNOR\n");
		return !xorgot;
	default: // NAND, OR
//		ALERT(at_debug,"final, NAND/OR returns false\n");
		return FALSE;
	}
}

LINK_ENTITY_TO_CLASS( watcher_count, CWatcherCount );

void CWatcherCount :: Spawn ( void )
{
	pev->solid = SOLID_NOT;
	SetNextThink( 0.5 );
}

void CWatcherCount :: Think ( void )
{
	SetNextThink( 0.1 );
	int iCount = 0;
	CBaseEntity *pCurrent = NULL;

	pCurrent = UTIL_FindEntityByTargetname( NULL, STRING(pev->noise) );
	while (pCurrent != NULL)
	{
		iCount++;
		pCurrent = UTIL_FindEntityByTargetname( pCurrent, STRING(pev->noise) );
	}

	if (pev->spawnflags & SF_WRCOUNT_STARTED)
	{
		if (iCount > pev->frags)
		{
			if (iCount < pev->impulse && pev->frags >= pev->impulse)
				FireTargets( STRING(pev->netname), this, this, USE_TOGGLE, 0 );
			FireTargets( STRING(pev->noise1), this, this, USE_TOGGLE, 0 );
		}
		else if (iCount < pev->frags)
		{
			if (iCount >= pev->impulse && pev->frags < pev->impulse)
				FireTargets( STRING(pev->message), this, this, USE_TOGGLE, 0 );
			FireTargets( STRING(pev->noise2), this, this, USE_TOGGLE, 0 );
		}
	}
	else
	{
		pev->spawnflags |= SF_WRCOUNT_STARTED;
		if (pev->spawnflags & SF_WRCOUNT_FIRESTART)
		{
			if (iCount < pev->impulse)
				FireTargets( STRING(pev->netname), this, this, USE_TOGGLE, 0 );
			else
				FireTargets( STRING(pev->message), this, this, USE_TOGGLE, 0 );
		}
	}
	pev->frags = iCount;
}

//***********************************************************

//LRC-  RenderFxFader, a subsidiary entity for RenderFxManager

TYPEDESCRIPTION	CRenderFxFader::m_SaveData[] = 
{
	DEFINE_FIELD( CRenderFxFader, m_flStartTime, FIELD_FLOAT),
	DEFINE_FIELD( CRenderFxFader, m_flDuration, FIELD_FLOAT),
	DEFINE_FIELD( CRenderFxFader, m_flCoarseness, FIELD_FLOAT),
	DEFINE_FIELD( CRenderFxFader, m_iStartAmt, FIELD_INTEGER),
	DEFINE_FIELD( CRenderFxFader, m_iOffsetAmt, FIELD_INTEGER ),
	DEFINE_FIELD( CRenderFxFader, m_vecStartColor, FIELD_VECTOR ),
	DEFINE_FIELD( CRenderFxFader, m_vecOffsetColor, FIELD_VECTOR ),
	DEFINE_FIELD( CRenderFxFader, m_fStartScale, FIELD_FLOAT),
	DEFINE_FIELD( CRenderFxFader, m_fOffsetScale, FIELD_FLOAT ),
	DEFINE_FIELD( CRenderFxFader, m_hTarget, FIELD_EHANDLE ),
};

IMPLEMENT_SAVERESTORE(CRenderFxFader,CBaseEntity);

void CRenderFxFader :: Spawn( void )
{
	SetThink ( FadeThink );
}

void CRenderFxFader :: FadeThink( void )
{
	if (((CBaseEntity*)m_hTarget) == NULL)
	{
//		ALERT(at_debug, "render_fader removed\n");
		SUB_Remove();
		return;
	}

	float flDegree = (gpGlobals->time - m_flStartTime)/m_flDuration;

	if (flDegree >= 1)
	{
//		ALERT(at_debug, "render_fader removes self\n");

		m_hTarget->pev->renderamt = m_iStartAmt + m_iOffsetAmt;
		m_hTarget->pev->rendercolor = m_vecStartColor + m_vecOffsetColor;
		m_hTarget->pev->scale = m_fStartScale + m_fOffsetScale;

		SUB_UseTargets( m_hTarget, USE_TOGGLE, 0 );

		if (pev->spawnflags & SF_RENDER_KILLTARGET)
		{
			m_hTarget->SetThink(SUB_Remove);
			m_hTarget->SetNextThink(0.1);
		}

		m_hTarget = NULL;

		SetNextThink( 0.1 );
		SetThink(SUB_Remove);
	}
	else
	{
		m_hTarget->pev->renderamt = m_iStartAmt + m_iOffsetAmt * flDegree;

		m_hTarget->pev->rendercolor.x = m_vecStartColor.x + m_vecOffsetColor.x * flDegree;
		m_hTarget->pev->rendercolor.y = m_vecStartColor.y + m_vecOffsetColor.y * flDegree;
		m_hTarget->pev->rendercolor.z = m_vecStartColor.z + m_vecOffsetColor.z * flDegree;

		m_hTarget->pev->scale = m_fStartScale + m_fOffsetScale * flDegree;

		SetNextThink( m_flCoarseness ); //?
	}
}

//
// Render parameters trigger
//
// This entity will copy its render parameters (renderfx, rendermode, rendercolor, renderamt)
// to its targets when triggered.
//

LINK_ENTITY_TO_CLASS( env_render, CRenderFxManager );


void CRenderFxManager :: KeyValue( KeyValueData *pkvd )
{
	if (FStrEq(pkvd->szKeyName, "m_fScale"))
	{
		pev->scale = atof(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else
		CPointEntity::KeyValue( pkvd );
}

void CRenderFxManager :: Use ( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	if (!FStringNull(pev->target))
	{
		CBaseEntity* pTarget = UTIL_FindEntityByTargetname( NULL, STRING(pev->target), pActivator);
		BOOL first = TRUE;
		while ( pTarget != NULL )
		{
			Affect( pTarget, first, pActivator );
			first = FALSE;
			pTarget = UTIL_FindEntityByTargetname( pTarget, STRING(pev->target), pActivator );
		}
	}

	if (pev->spawnflags & SF_RENDER_ONLYONCE)
	{
		SetThink(SUB_Remove);
		SetNextThink(0.1);
	}
}

void CRenderFxManager::Affect( CBaseEntity *pTarget, BOOL bIsFirst, CBaseEntity *pActivator )
{
	entvars_t *pevTarget = pTarget->pev;

	float fAmtFactor = 1;
	if ( pev->message && !FBitSet( pev->spawnflags, SF_RENDER_MASKAMT ) )
		fAmtFactor = CalcLocus_Ratio(pActivator, STRING(pev->message));

	if ( !FBitSet( pev->spawnflags, SF_RENDER_MASKFX ) )
		pevTarget->renderfx = pev->renderfx;
	if ( !FBitSet( pev->spawnflags, SF_RENDER_MASKMODE ) )
	{
		//LRC - amt is often 0 when mode is normal. Set it to be fully visible, for fade purposes.
		if (pev->frags && pevTarget->renderamt == 0 && pevTarget->rendermode == kRenderNormal)
			pevTarget->renderamt = 255;
		pevTarget->rendermode = pev->rendermode;
	}
	if (pev->frags == 0) // not fading?
	{
		if ( !FBitSet( pev->spawnflags, SF_RENDER_MASKAMT ) )
			pevTarget->renderamt = pev->renderamt * fAmtFactor;
		if ( !FBitSet( pev->spawnflags, SF_RENDER_MASKCOLOR ) )
			pevTarget->rendercolor = pev->rendercolor;
		if ( pev->scale )
			pevTarget->scale = pev->scale;

		if (bIsFirst)
			FireTargets( STRING(pev->netname), pTarget, this, USE_TOGGLE, 0 );
	}
	else
	{
		//LRC - fade the entity in/out!
		// (We create seperate fader entities to do this, one for each entity that needs fading.)
		CRenderFxFader *pFader = GetClassPtr( (CRenderFxFader *)NULL );
		pFader->m_hTarget = pTarget;
		pFader->m_iStartAmt = pevTarget->renderamt;
		pFader->m_vecStartColor = pevTarget->rendercolor;
		pFader->m_fStartScale = pevTarget->scale;
		pFader->pev->spawnflags = pev->spawnflags;

		if (bIsFirst)
			pFader->pev->target = pev->netname;

		if ( !FBitSet( pev->spawnflags, SF_RENDER_MASKAMT ) )
			pFader->m_iOffsetAmt = (pev->renderamt * fAmtFactor) - pevTarget->renderamt;
		else
			pFader->m_iOffsetAmt = 0;

		if ( !FBitSet( pev->spawnflags, SF_RENDER_MASKCOLOR ) )
		{
			pFader->m_vecOffsetColor.x = pev->rendercolor.x - pevTarget->rendercolor.x;
			pFader->m_vecOffsetColor.y = pev->rendercolor.y - pevTarget->rendercolor.y;
			pFader->m_vecOffsetColor.z = pev->rendercolor.z - pevTarget->rendercolor.z;
		}
		else
		{
			pFader->m_vecOffsetColor = g_vecZero;
		}

		if ( pev->scale )
			pFader->m_fOffsetScale = pev->scale - pevTarget->scale;
		else
			pFader->m_fOffsetScale = 0;

		pFader->m_flStartTime = gpGlobals->time;
		pFader->m_flDuration = pev->frags;
		pFader->m_flCoarseness = pev->sanity;
		pFader->SetNextThink( 0 );
		pFader->Spawn();
	}
}

//***********************************************************
//
// EnvCustomize
//
// Changes various properties of an entity (some properties only apply to monsters.)
//

#define SF_CUSTOM_AFFECTDEAD	1
#define SF_CUSTOM_ONCE			2
#define SF_CUSTOM_DEBUG			4

#define CUSTOM_FLAG_NOCHANGE	0
#define CUSTOM_FLAG_ON			1
#define CUSTOM_FLAG_OFF			2
#define CUSTOM_FLAG_TOGGLE		3
#define CUSTOM_FLAG_USETYPE		4
#define CUSTOM_FLAG_INVUSETYPE	5

LINK_ENTITY_TO_CLASS( env_customize, CEnvCustomize );

TYPEDESCRIPTION	CEnvCustomize::m_SaveData[] = 
{
	DEFINE_FIELD( CEnvCustomize, m_flRadius, FIELD_FLOAT),
	DEFINE_FIELD( CEnvCustomize, m_iszModel, FIELD_STRING),
	DEFINE_FIELD( CEnvCustomize, m_iClass, FIELD_INTEGER),
	DEFINE_FIELD( CEnvCustomize, m_iPlayerReact, FIELD_INTEGER ),
	DEFINE_FIELD( CEnvCustomize, m_iPrisoner, FIELD_INTEGER ),
	DEFINE_FIELD( CEnvCustomize, m_iMonsterClip, FIELD_INTEGER ),
	DEFINE_FIELD( CEnvCustomize, m_iVisible, FIELD_INTEGER ),
	DEFINE_FIELD( CEnvCustomize, m_iSolid, FIELD_INTEGER ),
	DEFINE_FIELD( CEnvCustomize, m_iProvoked, FIELD_INTEGER ),
	DEFINE_FIELD( CEnvCustomize, m_voicePitch, FIELD_INTEGER ),
	DEFINE_FIELD( CEnvCustomize, m_iBloodColor, FIELD_INTEGER ),
	DEFINE_FIELD( CEnvCustomize, m_fFramerate, FIELD_FLOAT ),
	DEFINE_FIELD( CEnvCustomize, m_fController0, FIELD_FLOAT ),
	DEFINE_FIELD( CEnvCustomize, m_fController1, FIELD_FLOAT ),
	DEFINE_FIELD( CEnvCustomize, m_fController2, FIELD_FLOAT ),
	DEFINE_FIELD( CEnvCustomize, m_fController3, FIELD_FLOAT ),
};

IMPLEMENT_SAVERESTORE(CEnvCustomize,CBaseEntity);

void CEnvCustomize :: KeyValue( KeyValueData *pkvd )
{
	if (FStrEq(pkvd->szKeyName, "m_iVisible"))
	{
		m_iVisible = atoi(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "m_iSolid"))
	{
		m_iSolid = atoi(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "m_iszModel"))
	{
		m_iszModel = ALLOC_STRING(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "m_voicePitch"))
	{
		m_voicePitch = atoi(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "m_iPrisoner"))
	{
		m_iPrisoner = atoi(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "m_iMonsterClip"))
	{
		m_iMonsterClip = atoi(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "m_iClass"))
	{
		m_iClass = atoi(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "m_iPlayerReact"))
	{
		m_iPlayerReact = atoi(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "m_flRadius"))
	{
		m_flRadius = atof(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "m_iProvoked"))
	{
		m_iProvoked = atoi(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "m_iBloodColor"))
	{
		m_iBloodColor = atoi(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "m_fFramerate"))
	{
		m_fFramerate = atof(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "m_fController0"))
	{
		m_fController0 = atof(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "m_fController1"))
	{
		m_fController1 = atof(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "m_fController2"))
	{
		m_fController2 = atof(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "m_fController3"))
	{
		m_fController3 = atof(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else
		CBaseEntity::KeyValue( pkvd );
}

void CEnvCustomize :: Spawn ( void )
{
	pev->solid = SOLID_NOT;
	if (m_iszModel)
		PRECACHE_MODEL((char*)STRING(m_iszModel));
	if (!pev->targetname)
	{
		// no name, just take effect when everything's spawned.
		SetNextThink( 0.1 );
	}
}

void CEnvCustomize :: Think ( void )
{
	Use(this, this, USE_TOGGLE, 0);
}

void CEnvCustomize :: Use ( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	if ( FStringNull(pev->target) )
	{
		if ( pActivator )
			Affect(pActivator, useType);
		else if (pev->spawnflags & SF_CUSTOM_DEBUG)
			ALERT(at_debug, "DEBUG: env_customize \"%s\" was fired without a locus!\n", STRING(pev->targetname));
	}
	else
	{
		BOOL fail = TRUE;
		CBaseEntity *pTarget = UTIL_FindEntityByTargetname(NULL, STRING(pev->target), pActivator);
		while (pTarget)
		{
			Affect(pTarget, useType);
			fail = FALSE;
			pTarget = UTIL_FindEntityByTargetname(pTarget, STRING(pev->target), pActivator);
		}
		pTarget = UTIL_FindEntityByClassname(NULL, STRING(pev->target));
		while (pTarget)
		{
			Affect(pTarget, useType);
			fail = FALSE;
			pTarget = UTIL_FindEntityByClassname(pTarget, STRING(pev->target));
		}
		if (fail && pev->spawnflags & SF_CUSTOM_DEBUG)
			ALERT(at_debug, "DEBUG: env_customize \"%s\" does nothing; can't find any entity with name or class \"%s\".\n", STRING(pev->target));
	}

	if (pev->spawnflags & SF_CUSTOM_ONCE)
	{
		if (pev->spawnflags & SF_CUSTOM_DEBUG)
			ALERT(at_debug, "DEBUG: env_customize \"%s\" removes itself.\n", STRING(pev->targetname));
		UTIL_Remove(this);
	}
}

void CEnvCustomize :: Affect (CBaseEntity *pTarget, USE_TYPE useType)
{
	CBaseMonster* pMonster = pTarget->MyMonsterPointer();
	if (!FBitSet(pev->spawnflags, SF_CUSTOM_AFFECTDEAD) && pMonster && pMonster->m_MonsterState == MONSTERSTATE_DEAD)
	{
		if (pev->spawnflags & SF_CUSTOM_DEBUG)
			ALERT(at_debug, "DEBUG: env_customize %s does nothing; can't apply to a corpse.\n", STRING(pev->targetname));
		return;
	}

	if (pev->spawnflags & SF_CUSTOM_DEBUG)
		ALERT(at_debug, "DEBUG: env_customize \"%s\" affects %s \"%s\": [", STRING(pev->targetname), STRING(pTarget->pev->classname), STRING(pTarget->pev->targetname));

	if (m_iszModel)
	{
		Vector vecMins, vecMaxs;
		vecMins = pTarget->pev->mins;
		vecMaxs = pTarget->pev->maxs;
		SET_MODEL(pTarget->edict(),STRING(m_iszModel));
		UTIL_SetSize(pTarget->pev,vecMins,vecMaxs);
		if (pev->spawnflags & SF_CUSTOM_DEBUG)
			ALERT(at_debug, " model=%s", STRING(m_iszModel));
	}
	SetBoneController( m_fController0, 0, pTarget );
	SetBoneController( m_fController1, 1, pTarget );
	SetBoneController( m_fController2, 2, pTarget );
	SetBoneController( m_fController3, 3, pTarget );
	if (m_fFramerate != -1)
	{
		//FIXME: check for env_model, stop it from changing its own framerate
		pTarget->pev->framerate = m_fFramerate;
		if (pev->spawnflags & SF_CUSTOM_DEBUG)
			ALERT(at_debug, " framerate=%f", m_fFramerate);
	}
	if (pev->body != -1)
	{
		pTarget->pev->body = pev->body;
		if (pev->spawnflags & SF_CUSTOM_DEBUG)
			ALERT(at_debug, " body = %d", pev->body);
	}
	if (pev->skin != -1)
	{
		if (pev->skin == -2)
		{
			if (pTarget->pev->skin)
			{
				pTarget->pev->skin = 0;
				if (pev->spawnflags & SF_CUSTOM_DEBUG)
					ALERT(at_debug, " skin=0");
			}
			else
			{
				pTarget->pev->skin = 1;
				if (pev->spawnflags & SF_CUSTOM_DEBUG)
					ALERT(at_debug, " skin=1");
			}
		}
		else if (pev->skin == -99) // special option to set CONTENTS_EMPTY
		{
			pTarget->pev->skin = -1;
			if (pev->spawnflags & SF_CUSTOM_DEBUG)
				ALERT(at_debug, " skin=-1");
		}
		else
		{
			pTarget->pev->skin = pev->skin;
			if (pev->spawnflags & SF_CUSTOM_DEBUG)
				ALERT(at_debug, " skin=%d", pTarget->pev->skin);
		}
	}

	switch ( GetActionFor(m_iVisible, !(pTarget->pev->effects & EF_NODRAW), useType, "visible"))
	{
	case CUSTOM_FLAG_ON: pTarget->pev->effects &= ~EF_NODRAW; break;
	case CUSTOM_FLAG_OFF: pTarget->pev->effects |= EF_NODRAW; break;
	}

	switch ( GetActionFor(m_iSolid, pTarget->pev->solid != SOLID_NOT, useType, "solid"))
	{
	case CUSTOM_FLAG_ON:
		if (*(STRING(pTarget->pev->model)) == '*')
			pTarget->pev->solid = SOLID_BSP;
		else
			pTarget->pev->solid = SOLID_SLIDEBOX;
		break;
	case CUSTOM_FLAG_OFF:	pTarget->pev->solid = SOLID_NOT;	break;
	}
/*	if (m_iVisible != CUSTOM_FLAG_NOCHANGE)
	{
		if (pTarget->pev->effects & EF_NODRAW && (m_iVisible == CUSTOM_FLAG_TOGGLE || m_iVisible == CUSTOM_FLAG_ON))
		{
			if (pev->spawnflags & SF_CUSTOM_DEBUG)
				ALERT(at_debug, " visible=YES");
		}
		else if (m_iVisible != CUSTOM_FLAG_ON)
		{
			pTarget->pev->effects |= EF_NODRAW;
			if (pev->spawnflags & SF_CUSTOM_DEBUG)
				ALERT(at_debug, " visible=NO");
		}
	}

	if (m_iSolid != CUSTOM_FLAG_NOCHANGE)
	{
		if (pTarget->pev->solid == SOLID_NOT && (m_iSolid == CUSTOM_FLAG_TOGGLE || m_iSolid == CUSTOM_FLAG_ON))
		{
			if (*(STRING(pTarget->pev->model)) == '*')
			{
				pTarget->pev->solid = SOLID_BSP;
				if (pev->spawnflags & SF_CUSTOM_DEBUG)
					ALERT(at_debug, " solid=YES(bsp)");
			}
			else
			{
				pTarget->pev->solid = SOLID_SLIDEBOX;
				if (pev->spawnflags & SF_CUSTOM_DEBUG)
					ALERT(at_debug, " solid=YES(point)");
			}
		}
		else if (m_iSolid != CUSTOM_FLAG_ON)
		{
			pTarget->pev->solid = SOLID_NOT;
			if (pev->spawnflags & SF_CUSTOM_DEBUG)
				ALERT(at_debug, " solid=NO");
		}
		else if (pev->spawnflags & SF_CUSTOM_DEBUG)
			ALERT(at_debug, " solid=unchanged");
	}
*/
	if (!pMonster)
	{
		if (pev->spawnflags & SF_CUSTOM_DEBUG)
			ALERT(at_debug, " ]\n");
		return;
	}

	if (m_iBloodColor != 0)
	{
		pMonster->m_bloodColor = m_iBloodColor;
		if (pev->spawnflags & SF_CUSTOM_DEBUG)
			ALERT(at_debug, " bloodcolor=%d", m_iBloodColor);
	}
	if (m_voicePitch > 0)
	{
		if (FClassnameIs(pTarget->pev,"monster_barney") || FClassnameIs(pTarget->pev,"monster_scientist") || FClassnameIs(pTarget->pev,"monster_sitting_scientist"))
		{
			((CTalkMonster*)pTarget)->m_voicePitch = m_voicePitch;
			if (pev->spawnflags & SF_CUSTOM_DEBUG)
				ALERT(at_debug, " voicePitch(talk)=%d", m_voicePitch);
		}
//		else if (FClassnameIs(pTarget->pev,"monster_human_grunt") || FClassnameIs(pTarget->pev,"monster_human_grunt_repel"))
//			((CHGrunt*)pTarget)->m_voicePitch = m_voicePitch;
//		else if (FClassnameIs(pTarget->pev,"monster_alien_slave"))
//			((CISlave*)pTarget)->m_voicePitch = m_voicePitch;
		else if (pev->spawnflags & SF_CUSTOM_DEBUG)
			ALERT(at_debug, " voicePitch=unchanged");
	}

	if (m_iClass != 0)
	{
		pMonster->m_iClass = m_iClass;
		if (pev->spawnflags & SF_CUSTOM_DEBUG)
			ALERT(at_debug, " class=%d", m_iClass);
		if (pMonster->m_hEnemy)
		{
			pMonster->m_hEnemy = NULL;
			// make 'em stop attacking... might be better to use a different signal?
			pMonster->SetConditions( bits_COND_NEW_ENEMY );
		}
	}
	if (m_iPlayerReact != -1)
	{
		pMonster->m_iPlayerReact = m_iPlayerReact;
		if (pev->spawnflags & SF_CUSTOM_DEBUG)
			ALERT(at_debug, " playerreact=%d", m_iPlayerReact);
	}

//	SetCustomFlag( m_iPrisoner, pMonster->pev->spawnflags, SF_MONSTER_PRISONER, useType, "prisoner");
	switch (GetActionFor(m_iPrisoner, pMonster->pev->spawnflags & SF_MONSTER_PRISONER, useType, "prisoner") )
	{
	case CUSTOM_FLAG_ON:
		pMonster->pev->spawnflags |= SF_MONSTER_PRISONER;
		if (pMonster->m_hEnemy)
		{
			pMonster->m_hEnemy = NULL;
			// make 'em stop attacking... might be better to use a different signal?
			pMonster->SetConditions( bits_COND_NEW_ENEMY );
		}
		break;
	case CUSTOM_FLAG_OFF:
		pMonster->pev->spawnflags &= ~SF_MONSTER_PRISONER;
		break;
	}
/*	if (m_iPrisoner != CUSTOM_FLAG_NOCHANGE)
	{
		if (pMonster->pev->spawnflags & SF_MONSTER_PRISONER && (m_iPrisoner == CUSTOM_FLAG_TOGGLE || m_iPrisoner == CUSTOM_FLAG_OFF))
		{
			pMonster->pev->spawnflags &= ~SF_MONSTER_PRISONER;
			if (pev->spawnflags & SF_CUSTOM_DEBUG)
				ALERT(at_debug, " prisoner=NO");
		}
		else if (m_iPrisoner != CUSTOM_FLAG_OFF)
		{
			pMonster->pev->spawnflags |= SF_MONSTER_PRISONER;
			if (pev->spawnflags & SF_CUSTOM_DEBUG)
				ALERT(at_debug, " prisoner=YES");
			if (pMonster->m_hEnemy)
			{
				pMonster->m_hEnemy = NULL;
				// make 'em stop attacking... might be better to use a different signal?
				pMonster->SetConditions( bits_COND_NEW_ENEMY );
			}
		}
		else if (pev->spawnflags & SF_CUSTOM_DEBUG)
			ALERT(at_debug, " prisoner=unchanged");
	}
*/
	switch (GetActionFor(m_iMonsterClip, pMonster->pev->flags & FL_MONSTERCLIP, useType, "monsterclip") )
	{
	case CUSTOM_FLAG_ON: pMonster->pev->flags |= FL_MONSTERCLIP; break;
	case CUSTOM_FLAG_OFF: pMonster->pev->flags &= ~FL_MONSTERCLIP; break;
	}
/*	if (m_iMonsterClip != CUSTOM_FLAG_NOCHANGE)
	{
		if (pMonster->pev->flags & FL_MONSTERCLIP && (m_iMonsterClip == CUSTOM_FLAG_TOGGLE || m_iMonsterClip == CUSTOM_FLAG_OFF))
		{
			pMonster->pev->flags &= ~FL_MONSTERCLIP;
			if (pev->spawnflags & SF_CUSTOM_DEBUG)
				ALERT(at_debug, " monsterclip=NO");
		}
		else if (m_iMonsterClip != CUSTOM_FLAG_OFF)
		{
			pMonster->pev->flags |= FL_MONSTERCLIP;
			if (pev->spawnflags & SF_CUSTOM_DEBUG)
				ALERT(at_debug, " monsterclip=YES");
		}
		else if (pev->spawnflags & SF_CUSTOM_DEBUG)
			ALERT(at_debug, " monsterclip=unchanged");
	}
*/
	switch (GetActionFor(m_iProvoked, pMonster->m_afMemory & bits_MEMORY_PROVOKED, useType, "provoked") )
	{
	case CUSTOM_FLAG_ON: pMonster->Remember(bits_MEMORY_PROVOKED); break;
	case CUSTOM_FLAG_OFF: pMonster->Forget(bits_MEMORY_PROVOKED); break;
	}
/*	if (m_iProvoked != CUSTOM_FLAG_NOCHANGE)
	{
		if (pMonster->m_afMemory & bits_MEMORY_PROVOKED && (m_iProvoked == CUSTOM_FLAG_TOGGLE || m_iProvoked == CUSTOM_FLAG_OFF))
		{
			pMonster->Forget(bits_MEMORY_PROVOKED);
			if (pev->spawnflags & SF_CUSTOM_DEBUG)
				ALERT(at_debug, " provoked=NO");
		}
		else if (m_iProvoked != CUSTOM_FLAG_OFF)
		{
			pMonster->Remember(bits_MEMORY_PROVOKED);
			if (pev->spawnflags & SF_CUSTOM_DEBUG)
				ALERT(at_debug, " provoked=YES");
		}
		else if (pev->spawnflags & SF_CUSTOM_DEBUG)
			ALERT(at_debug, " provoked=unchanged");
	}
*/
	if (pev->spawnflags & SF_CUSTOM_DEBUG)
		ALERT(at_debug, " ]\n");
}

int CEnvCustomize::GetActionFor( int iField, int iActive, USE_TYPE useType, char* szDebug)
{
	int iAction = iField;

	if (iAction == CUSTOM_FLAG_USETYPE)
	{
		if (useType == USE_ON)
			iAction = CUSTOM_FLAG_ON;
		else if (useType == USE_OFF)
			iAction = CUSTOM_FLAG_OFF;
		else
			iAction = CUSTOM_FLAG_TOGGLE;
	}
	else if (iAction == CUSTOM_FLAG_INVUSETYPE)
	{
		if (useType == USE_ON)
			iAction = CUSTOM_FLAG_OFF;
		else if (useType == USE_OFF)
			iAction = CUSTOM_FLAG_ON;
		else
			iAction = CUSTOM_FLAG_TOGGLE;
	}

	if (iAction == CUSTOM_FLAG_TOGGLE)
	{
		if (iActive)
			iAction = CUSTOM_FLAG_OFF;
		else
			iAction = CUSTOM_FLAG_ON;
	}

	if (pev->spawnflags & SF_CUSTOM_DEBUG)
	{
		if (iAction == CUSTOM_FLAG_ON)
			ALERT(at_debug, " %s=YES", szDebug);
		else if (iAction == CUSTOM_FLAG_OFF)
			ALERT(at_debug, " %s=NO", szDebug);
	}
	return iAction;
}

void CEnvCustomize::SetBoneController( float fController, int cnum, CBaseEntity *pTarget)
{
	if (fController) //FIXME: the pTarget isn't necessarily a CBaseAnimating.
	{
		if (fController == 1024)
		{
			((CBaseAnimating*)pTarget)->SetBoneController( cnum, 0 );
			if (pev->spawnflags & SF_CUSTOM_DEBUG)
				ALERT(at_debug, " bone%d=0", cnum);
		}
		else
		{
			((CBaseAnimating*)pTarget)->SetBoneController( cnum, fController );
			if (pev->spawnflags & SF_CUSTOM_DEBUG)
				ALERT(at_debug, " bone%d=%f", cnum, fController);
		}
	}
}


LINK_ENTITY_TO_CLASS( trigger, CBaseTrigger );

/*
================
InitTrigger
================
*/

BOOL CBaseTrigger :: CanTouch( entvars_t *pevToucher )
{
	if ( !pev->netname )
	{
		// Only touch clients, monsters, or pushables (depending on flags)
		if (pevToucher->flags & FL_CLIENT)
			return !(pev->spawnflags & SF_TRIGGER_NOCLIENTS);
		else if (pevToucher->flags & FL_MONSTER)
			return pev->spawnflags & SF_TRIGGER_ALLOWMONSTERS;
		else if (FClassnameIs(pevToucher,"func_pushable"))
			return pev->spawnflags & SF_TRIGGER_PUSHABLES;
		else
			return pev->spawnflags & SF_TRIGGER_EVERYTHING;
	}
	else
	{
		// If netname is set, it's an entity-specific trigger; we ignore the spawnflags.
		if (!FClassnameIs(pevToucher, STRING(pev->netname)) &&
			(!pevToucher->targetname || !FStrEq(STRING(pevToucher->targetname), STRING(pev->netname))))
			return FALSE;
	}
	return TRUE;
}

//
// ToggleUse - If this is the USE function for a trigger, its state will toggle every time it's fired
//
void CBaseTrigger :: ToggleUse ( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	if (pev->solid == SOLID_NOT)
	{// if the trigger is off, turn it on
		pev->solid = SOLID_TRIGGER;
		
		// Force retouch
		gpGlobals->force_retouch++;
	}
	else
	{// turn the trigger off
		pev->solid = SOLID_NOT;
	}
	UTIL_SetOrigin( this, pev->origin );
}

/*
================
InitTrigger
================
*/
void CBaseTrigger::InitTrigger( )
{
	// trigger angles are used for one-way touches.  An angle of 0 is assumed
	// to mean no restrictions, so use a yaw of 360 instead.
	if (pev->angles != g_vecZero)
		SetMovedir(pev);
	pev->solid = SOLID_TRIGGER;
	pev->movetype = MOVETYPE_NONE;
	SET_MODEL(ENT(pev), STRING(pev->model));    // set size and link into world
	if ( CVAR_GET_FLOAT("showtriggers") == 0 )
		SetBits( pev->effects, EF_NODRAW );
}


LINK_ENTITY_TO_CLASS( trigger_hurt, CTriggerHurt );

//=====================================

//
// trigger_hurt - hurts anything that touches it. if the trigger has a targetname, firing it will toggle state
//

void CTriggerHurt :: KeyValue( KeyValueData *pkvd )
{
	if (FStrEq(pkvd->szKeyName, "damage"))
	{
		pev->dmg = atof(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "damagetype"))
	{
		m_bitsDamageInflict |= atoi(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "cangib"))
	{
		switch (atoi(pkvd->szValue))
		{
		case 1:
			m_bitsDamageInflict |= DMG_ALWAYSGIB;
		case 2:
			m_bitsDamageInflict |= DMG_NEVERGIB;
		}
		pkvd->fHandled = TRUE;
	}
	else
		CBaseToggle::KeyValue( pkvd );
}

void CTriggerHurt :: Spawn( void )
{
	InitTrigger();
	SetTouch ( HurtTouch );

	if ( !FStringNull ( pev->targetname ) )
	{
		SetUse ( ToggleUse );
	}
	else
	{
		SetUse ( NULL );
	}

	if (m_bitsDamageInflict & DMG_RADIATION)
	{
		SetThink ( RadiationThink );
		SetNextThink( RANDOM_FLOAT(0.0, 0.5) ); 
	}

	if ( FBitSet (pev->spawnflags, SF_TRIGGER_HURT_START_OFF) )// if flagged to Start Turned Off, make trigger nonsolid.
		pev->solid = SOLID_NOT;

	UTIL_SetOrigin( this, pev->origin );		// Link into the list
}

void CTriggerHurt :: ToggleUse ( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	// if we are turning on
	if (pev->solid == SOLID_NOT)
	{
		// set the think to thinkhurt
		SetThink(HurtThink);
		SetNextThink(0.0);
	}
	else
	{
		DontThink();
	}
	CBaseTrigger::ToggleUse(pActivator, pCaller, useType, value);
}

// When touched, a hurt trigger does DMG points of damage each half-second
void CTriggerHurt :: HurtTouch ( CBaseEntity *pOther )
{
	float fldmg;

	if (!UTIL_IsMasterTriggered(m_sMaster, pOther))
		return;

	if ( !pOther->pev->takedamage )
		return;

	if ( (pev->spawnflags & SF_TRIGGER_HURT_CLIENTONLYTOUCH) && !pOther->IsPlayer() )
	{
		// this trigger is only allowed to touch clients, and this ain't a client.
		return;
	}

	if ( (pev->spawnflags & SF_TRIGGER_HURT_NO_CLIENTS) && pOther->IsPlayer() )
		return;

	// HACKHACK -- In multiplayer, players touch this based on packet receipt.
	// So the players who send packets later aren't always hurt.  Keep track of
	// how much time has passed and whether or not you've touched that player
	if ( g_pGameRules->IsMultiplayer() )
	{
		if ( pev->dmgtime > gpGlobals->time )
		{
			if ( gpGlobals->time != pev->pain_finished )
			{// too early to hurt again, and not same frame with a different entity
				if ( pOther->IsPlayer() )
				{
					int playerMask = 1 << (pOther->entindex() - 1);

					// If I've already touched this player (this time), then bail out
					if ( pev->impulse & playerMask )
						return;

					// Mark this player as touched
					// BUGBUG - There can be only 32 players!
					pev->impulse |= playerMask;
				}
				else
				{
					return;
				}
			}
		}
		else
		{
			// New clock, "un-touch" all players
			pev->impulse = 0;
			if ( pOther->IsPlayer() )
			{
				int playerMask = 1 << (pOther->entindex() - 1);

				// Mark this player as touched
				// BUGBUG - There can be only 32 players!
				pev->impulse |= playerMask;
			}
		}
	}
	else if (!(pev->spawnflags & SF_TRIGGER_HURT_CLIENTONLYTOUCH)) // can other things be hurt...
	{
		if ( pev->dmgtime > gpGlobals->time )
		{
			if ( gpGlobals->time != pev->pain_finished )
			{// too early to hurt again, and not same frame with a different entity
				int otherMask = 1 << (pOther->entindex() - 1);

				// If I've already touched this entity (this time), then bail out
				if ( pev->impulse & otherMask )
					return;

				// Mark this entity as touched
				// BUGBUG - There can be only 32 other entities!
				pev->impulse |= otherMask;
			}
		}
		else
		{
			// New clock, "un-touch" all players
			pev->impulse = 0;

			int otherMask = 1 << (pOther->entindex() - 1);

			// Mark this entity as touched
			// BUGBUG - There can be only 32 other entities!
			pev->impulse |= otherMask;
		}
	}
	else	// Original code -- single player
	{
		if ( pev->dmgtime > gpGlobals->time && gpGlobals->time != pev->pain_finished )
		{// too early to hurt again, and not same frame with a different entity
			return;
		}
	}



	// If this is time_based damage (poison, radiation), override the pev->dmg with a 
	// default for the given damage type.  Monsters only take time-based damage
	// while touching the trigger.  Player continues taking damage for a while after
	// leaving the trigger

	fldmg = pev->dmg * 0.5;	// 0.5 seconds worth of damage, pev->dmg is damage/second


	// JAY: Cut this because it wasn't fully realized.  Damage is simpler now.
#if 0
	switch (m_bitsDamageInflict)
	{
	default: break;
	case DMG_POISON:		fldmg = POISON_DAMAGE/4; break;
	case DMG_NERVEGAS:		fldmg = NERVEGAS_DAMAGE/4; break;
	case DMG_RADIATION:		fldmg = RADIATION_DAMAGE/4; break;
	case DMG_PARALYZE:		fldmg = PARALYZE_DAMAGE/4; break; // UNDONE: cut this? should slow movement to 50%
	case DMG_ACID:			fldmg = ACID_DAMAGE/4; break;
	case DMG_SLOWBURN:		fldmg = SLOWBURN_DAMAGE/4; break;
	case DMG_SLOWFREEZE:	fldmg = SLOWFREEZE_DAMAGE/4; break;
	}
#endif

	if ( fldmg < 0 )
		pOther->TakeHealth( -fldmg, m_bitsDamageInflict );
	else
		pOther->TakeDamage( pev, pev, fldmg, m_bitsDamageInflict );

	// Store pain time so we can get all of the other entities on this frame
	pev->pain_finished = gpGlobals->time;

	// Apply damage every half second
	pev->dmgtime = gpGlobals->time + 0.5;// half second delay until this trigger can hurt toucher again

  
	
	if ( pev->target )
	{
		// trigger has a target it wants to fire. 
		if ( pev->spawnflags & SF_TRIGGER_HURT_CLIENTONLYFIRE )
		{
			// if the toucher isn't a client, don't fire the target!
			if ( !pOther->IsPlayer() )
			{
				return;
			}
		}

		SUB_UseTargets( pOther, USE_TOGGLE, 0 );
		if ( pev->spawnflags & SF_TRIGGER_HURT_TARGETONCE )
			pev->target = 0;
	}
}

void CTriggerHurt :: HurtThink( void )
{
	// get everything in the area
	const int MAX_ENTITIES = 1000;
	CBaseEntity* pList[MAX_ENTITIES];
	int iEntities = UTIL_EntitiesInBox( pList, 10, pev->mins, pev->maxs, (FL_CLIENT|FL_MONSTER) );

	float fldmg;

	for (int i = 0; i < iEntities; i++)
	{
		CBaseEntity* pOther = pList[i];

		if ( !pOther->pev->takedamage )
			break;

		if ( (pev->spawnflags & SF_TRIGGER_HURT_CLIENTONLYTOUCH) && !pOther->IsPlayer() )
		{
			// this trigger is only allowed to touch clients, and this ain't a client.
			break;
		}

		if ( (pev->spawnflags & SF_TRIGGER_HURT_NO_CLIENTS) && pOther->IsPlayer() )
			break;

		// HACKHACK -- In multiplayer, players touch this based on packet receipt.
		// So the players who send packets later aren't always hurt.  Keep track of
		// how much time has passed and whether or not you've touched that player
		if ( g_pGameRules->IsMultiplayer() )
		{
			if ( pev->dmgtime > gpGlobals->time )
			{
				if ( gpGlobals->time != pev->pain_finished )
				{// too early to hurt again, and not same frame with a different entity
					if ( pOther->IsPlayer() )
					{
						int playerMask = 1 << (pOther->entindex() - 1);

						// If I've already touched this player (this time), then bail out
						if ( pev->impulse & playerMask )
							break;

						// Mark this player as touched
						// BUGBUG - There can be only 32 players!
						pev->impulse |= playerMask;
					}
					else
					{
						break;
					}
				}
			}
			else
			{
				// New clock, "un-touch" all players
				pev->impulse = 0;
				if ( pOther->IsPlayer() )
				{
					int playerMask = 1 << (pOther->entindex() - 1);

					// Mark this player as touched
					// BUGBUG - There can be only 32 players!
					pev->impulse |= playerMask;
				}
			}
		}
		else if (!(pev->spawnflags & SF_TRIGGER_HURT_CLIENTONLYTOUCH)) // can other things be hurt...
		{
			if ( pev->dmgtime > gpGlobals->time )
			{
				if ( gpGlobals->time != pev->pain_finished )
				{// too early to hurt again, and not same frame with a different entity
					int otherMask = 1 << (pOther->entindex() - 1);

					// If I've already touched this entity (this time), then bail out
					if ( pev->impulse & otherMask )
						break;

					// Mark this entity as touched
					// BUGBUG - There can be only 32 other entities!
					pev->impulse |= otherMask;
				}
			}
			else
			{
				// New clock, "un-touch" all players
				pev->impulse = 0;

				int otherMask = 1 << (pOther->entindex() - 1);

				// Mark this entity as touched
				// BUGBUG - There can be only 32 other entities!
				pev->impulse |= otherMask;
			}
		}
		else	// Original code -- single player
		{
			if ( pev->dmgtime > gpGlobals->time && gpGlobals->time != pev->pain_finished )
			{// too early to hurt again, and not same frame with a different entity
				break;
			}
		}

		// If this is time_based damage (poison, radiation), override the pev->dmg with a 
		// default for the given damage type.  Monsters only take time-based damage
		// while touching the trigger.  Player continues taking damage for a while after
		// leaving the trigger

		fldmg = pev->dmg * 0.5;	// 0.5 seconds worth of damage, pev->dmg is damage/second

		// hurt it!!!
		if ( fldmg < 0 )
			pOther->TakeHealth( -fldmg, m_bitsDamageInflict );
		else
			pOther->TakeDamage( pev, pev, fldmg, m_bitsDamageInflict );

		// Store pain time so we can get all of the other entities on this frame
		pev->pain_finished = gpGlobals->time;

		// Apply damage every half second
		pev->dmgtime = gpGlobals->time + 0.5;// half second delay until this trigger can hurt toucher again

  
		
		if ( pev->target )
		{
			// trigger has a target it wants to fire. 
			if ( pev->spawnflags & SF_TRIGGER_HURT_CLIENTONLYFIRE )
			{
				// if the toucher isn't a client, don't fire the target!
				if ( !pOther->IsPlayer() )
				{
					break;
				}
			}

			SUB_UseTargets( pOther, USE_TOGGLE, 0 );
			if ( pev->spawnflags & SF_TRIGGER_HURT_TARGETONCE )
				pev->target = 0;
		}
	}

	// do we need to set the geiger counter off
	if (m_bitsDamageInflict & DMG_RADIATION)
	{
		RadiationThink();
	}
	SetNextThink( 0.25 ); 
}

// trigger hurt that causes radiation will do a radius
// check and set the player's geiger counter level
// according to distance from center of trigger

void CTriggerHurt :: RadiationThink( void )
{

	edict_t *pentPlayer;
	CBasePlayer *pPlayer = NULL;
	float flRange;
	entvars_t *pevTarget;
	Vector vecSpot1;
	Vector vecSpot2;
	Vector vecRange;
	Vector origin;
	Vector view_ofs;

	// check to see if a player is in pvs
	// if not, continue	

	// set origin to center of trigger so that this check works
	origin = pev->origin;
	view_ofs = pev->view_ofs;

	pev->origin = (pev->absmin + pev->absmax) * 0.5;
	pev->view_ofs = pev->view_ofs * 0.0;

	pentPlayer = FIND_CLIENT_IN_PVS(edict());

	pev->origin = origin;
	pev->view_ofs = view_ofs;

	// reset origin

	if (!FNullEnt(pentPlayer))
	{
 
		pPlayer = GetClassPtr( (CBasePlayer *)VARS(pentPlayer));

		pevTarget = VARS(pentPlayer);

		// get range to player;

		vecSpot1 = (pev->absmin + pev->absmax) * 0.5;
		vecSpot2 = (pevTarget->absmin + pevTarget->absmax) * 0.5;
		
		vecRange = vecSpot1 - vecSpot2;
		flRange = vecRange.Length();

		// if player's current geiger counter range is larger
		// than range to this trigger hurt, reset player's
		// geiger counter range 

		if (pPlayer->m_flgeigerRange >= flRange)
			pPlayer->m_flgeigerRange = flRange;
	}

	SetNextThink( 0.25 );
}

//
// trigger_monsterjump
//

LINK_ENTITY_TO_CLASS( trigger_monsterjump, CTriggerMonsterJump );


void CTriggerMonsterJump :: Spawn ( void )
{
	SetMovedir ( pev );
	
	InitTrigger ();

	DontThink();
	pev->speed = 200;
	m_flHeight = 150;

	if ( !FStringNull ( pev->targetname ) )
	{// if targetted, spawn turned off
		pev->solid = SOLID_NOT;
		UTIL_SetOrigin( this, pev->origin ); // Unlink from trigger list
		SetUse( ToggleUse );
	}
}


void CTriggerMonsterJump :: Think( void )
{
	pev->solid = SOLID_NOT;// kill the trigger for now !!!UNDONE
	UTIL_SetOrigin( this, pev->origin ); // Unlink from trigger list
	SetThink( NULL );
}

void CTriggerMonsterJump :: Touch( CBaseEntity *pOther )
{
	entvars_t *pevOther = pOther->pev;

	if ( !FBitSet ( pevOther->flags , FL_MONSTER ) ) 
	{// touched by a non-monster.
		return;
	}

	pevOther->origin.z += 1;
	
	if ( FBitSet ( pevOther->flags, FL_ONGROUND ) ) 
	{// clear the onground so physics don't bitch
		pevOther->flags &= ~FL_ONGROUND;
	}

	// toss the monster!
	pevOther->velocity = pev->movedir * pev->speed;
	pevOther->velocity.z += m_flHeight;
	SetNextThink( 0 );
}


//=====================================
//
// trigger_cdaudio - starts/stops cd audio tracks
//

LINK_ENTITY_TO_CLASS( trigger_cdaudio, CTriggerCDAudio );

//
// Changes tracks or stops CD when player touches
//
// !!!HACK - overloaded HEALTH to avoid adding new field
void CTriggerCDAudio :: Touch ( CBaseEntity *pOther )
{
	if ( !pOther->IsPlayer() )
	{// only clients may trigger these events
		return;
	}

	PlayTrack();
}

void CTriggerCDAudio :: Spawn( void )
{
	InitTrigger();
}

void CTriggerCDAudio::Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	PlayTrack();
}

void PlayCDTrack( int iTrack )
{
	edict_t *pClient;
	
	// manually find the single player. 
	pClient = g_engfuncs.pfnPEntityOfEntIndex( 1 );
	
	// Can't play if the client is not connected!
	if ( !pClient )
		return;

	if ( iTrack < -1 || iTrack > 30 )
	{
		ALERT ( at_debug, "TriggerCDAudio - Track %d out of range\n" );
		return;
	}

	if ( iTrack == -1 )
	{
		CLIENT_COMMAND ( pClient, "cd pause\n");
	}
	else
	{
		char string [ 64 ];

		sprintf( string, "cd play %3d\n", iTrack );
		CLIENT_COMMAND ( pClient, string);
	}
}


// only plays for ONE client, so only use in single play!
void CTriggerCDAudio :: PlayTrack( void )
{
	PlayCDTrack( (int)pev->health );
	
	SetTouch( NULL );
	UTIL_Remove( this );
}


// This plays a CD track when fired or when the player enters it's radius
class CTargetCDAudio : public CPointEntity
{
public:
	void			Spawn( void );
	void			KeyValue( KeyValueData *pkvd );

	virtual void	Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value );
	void			Think( void );
	void			Play( void );
};

LINK_ENTITY_TO_CLASS( target_cdaudio, CTargetCDAudio );

void CTargetCDAudio :: KeyValue( KeyValueData *pkvd )
{
	if (FStrEq(pkvd->szKeyName, "radius"))
	{
		pev->scale = atof(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else
		CPointEntity::KeyValue( pkvd );
}

void CTargetCDAudio :: Spawn( void )
{
	pev->solid = SOLID_NOT;
	pev->movetype = MOVETYPE_NONE;

	if ( pev->scale > 0 )
		SetNextThink( 1.0 );
}

void CTargetCDAudio::Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	Play();
}

// only plays for ONE client, so only use in single play!
void CTargetCDAudio::Think( void )
{
	edict_t *pClient;
	
	// manually find the single player. 
	pClient = g_engfuncs.pfnPEntityOfEntIndex( 1 );
	
	// Can't play if the client is not connected!
	if ( !pClient )
		return;
	
	SetNextThink( 0.5 );

	if ( (pClient->v.origin - pev->origin).Length() <= pev->scale )
		Play();

}

void CTargetCDAudio::Play( void ) 
{ 
	PlayCDTrack( (int)pev->health );
	UTIL_Remove(this); 
}


LINK_ENTITY_TO_CLASS( trigger_multiple, CTriggerMultiple );


void CTriggerMultiple :: Spawn( void )
{
	if (m_flWait == 0)
		m_flWait = 0.2;

	InitTrigger();

	ASSERTSZ(pev->health == 0, "trigger_multiple with health");
	SetTouch( MultiTouch );
	Precache();
}

void CTriggerMultiple :: MultiTouch( CBaseEntity *pOther )
{
	entvars_t	*pevToucher;

	pevToucher = pOther->pev;

	if (!CanTouch(pevToucher)) return;

#if 0
		// if the trigger has an angles field, check player's facing direction
		if (pev->movedir != g_vecZero)
		{
			UTIL_MakeVectors( pevToucher->angles );
			if ( DotProduct( gpGlobals->v_forward, pev->movedir ) < 0 )
				return;         // not facing the right way
		}
#endif
		
	ActivateMultiTrigger( pOther );
}


//
// the trigger was just touched/killed/used
// m_hActivator gets set to the activator so it can be held through a delay
// so wait for the delay time before firing
//
void CTriggerMultiple :: ActivateMultiTrigger( CBaseEntity *pActivator )
{
	if (m_fNextThink > gpGlobals->time)
		return;         // still waiting for reset time

	if (!UTIL_IsMasterTriggered(m_sMaster,pActivator))
		return;

	if (FClassnameIs(pev, "trigger_secret"))
	{
		if ( pev->enemy == NULL || !FClassnameIs(pev->enemy, "player"))
			return;
		gpGlobals->found_secrets++;
	}

	if (!FStringNull(pev->noise))
		EMIT_SOUND(ENT(pev), CHAN_VOICE, (char*)STRING(pev->noise), 1, ATTN_NORM);

// don't trigger again until reset
// pev->takedamage = DAMAGE_NO;

	m_hActivator = pActivator;
	SUB_UseTargets( m_hActivator, USE_TOGGLE, 0 );

	if ( pev->message && pActivator->IsPlayer() )
	{
		UTIL_ShowMessage( STRING(pev->message), pActivator );
//		CLIENT_PRINTF( ENT( pActivator->pev ), print_center, STRING(pev->message) );
	}

	if (m_flWait > 0)
	{
		SetThink( MultiWaitOver );
		SetNextThink( m_flWait );
	}
	else
	{
		// we can't just remove (self) here, because this is a touch function
		// called while C code is looping through area links...
		SetTouch( NULL );
		SetNextThink( 0.1 );
		SetThink(  SUB_Remove );
	}
}

// the wait time has passed, so set back up for another activation
void CTriggerMultiple :: MultiWaitOver( void )
{
//	if (pev->max_health)
//		{
//		pev->health		= pev->max_health;
//		pev->takedamage	= DAMAGE_YES;
//		pev->solid		= SOLID_BBOX;
//		}
	//SetThink( NULL );
	DontThink();
}


LINK_ENTITY_TO_CLASS( trigger_once, CTriggerOnce );
void CTriggerOnce::Spawn( void )
{
	m_flWait = -1;
	
	CTriggerMultiple :: Spawn();
}

//===========================================================
//LRC - trigger_inout, a trigger which fires _only_ when
// the player enters or leaves it.
//   If there's more than one entity it can trigger off, then
// it will trigger for each one that enters and leaves.
//===========================================================

TYPEDESCRIPTION	CInOutRegister::m_SaveData[] = 
{
	DEFINE_FIELD( CInOutRegister, m_pField, FIELD_CLASSPTR ),
	DEFINE_FIELD( CInOutRegister, m_pNext, FIELD_CLASSPTR ),
	DEFINE_FIELD( CInOutRegister, m_hValue, FIELD_EHANDLE ),
};

// Cthulhu
LINK_ENTITY_TO_CLASS( inout_register, CInOutRegister );

IMPLEMENT_SAVERESTORE(CInOutRegister,CPointEntity);

BOOL CInOutRegister::IsRegistered ( CBaseEntity *pValue )
{
	if (m_hValue == pValue)
		return TRUE;
	else if (m_pNext)
		return m_pNext->IsRegistered( pValue );
	else
		return FALSE;
}

CInOutRegister *CInOutRegister::Prune( void )
{
	if ( m_hValue )
	{
		ASSERTSZ(m_pNext != NULL, "invalid InOut registry terminator\n");
		if ( m_pField->Intersects(m_hValue) )
		{
			// this entity is still inside the field, do nothing
			m_pNext = m_pNext->Prune();
			return this;
		}
		else
		{
			// this entity has just left the field, trigger
			m_pField->FireOnLeaving( m_hValue );
			SetThink( SUB_Remove );
			SetNextThink( 0.1 );
			return m_pNext->Prune();
		}
	}
	else
	{	// this register has a missing or null value
		if (m_pNext)
		{
			// this is an invalid list entry, remove it
			SetThink( SUB_Remove );
			SetNextThink( 0.1 );
			return m_pNext->Prune();
		}
		else
		{
			// this is the list terminator, leave it.
			return this;
		}
	}
}



LINK_ENTITY_TO_CLASS( trigger_inout, CTriggerInOut );

TYPEDESCRIPTION	CTriggerInOut::m_SaveData[] = 
{
	DEFINE_FIELD( CTriggerInOut, m_iszBothTarget, FIELD_STRING ),
	DEFINE_FIELD( CTriggerInOut, m_pRegister, FIELD_CLASSPTR ),
	DEFINE_FIELD( CTriggerInOut, m_iszAltTarget, FIELD_STRING ),
};

// Cthulhu : a hack to get the registers to work properly
//IMPLEMENT_SAVERESTORE(CTriggerInOut,CBaseTrigger);
int CTriggerInOut::Save( CSave &save )
{
	if ( !CBaseTrigger::Save(save) )
		return 0;

	if ( pev->targetname )
		return save.WriteFields( STRING(pev->targetname), "CTriggerInOut", this, m_SaveData, ARRAYSIZE(m_SaveData) );
	else
		return save.WriteFields( STRING(pev->classname), "CTriggerInOut", this, m_SaveData, ARRAYSIZE(m_SaveData) );
}

int CTriggerInOut::Restore( CRestore &restore )
{
	if ( !CBaseTrigger::Restore(restore) )
		return 0;
	int status = restore.ReadFields( "CTriggerInOut", this, m_SaveData, ARRAYSIZE(m_SaveData) );

	/*
	// Cthulhu
	// create a null-terminator for the registry
	m_pRegister = GetClassPtr( (CInOutRegister*)NULL );
	m_pRegister->m_hValue = NULL;
	m_pRegister->m_pNext = NULL;
	m_pRegister->m_pField = this;
	m_pRegister->pev->classname = MAKE_STRING("inout_register");
	// and set a flag
	mbRestored = TRUE;
	*/
	// and schedule a think
	SetNextThink(0.1);

	return status;
}

void CTriggerInOut::KeyValue( KeyValueData *pkvd )
{
	if (FStrEq(pkvd->szKeyName, "m_iszAltTarget"))
	{
		m_iszAltTarget = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "m_iszBothTarget"))
	{
		m_iszBothTarget = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else
		CBaseTrigger::KeyValue( pkvd );
}

void CTriggerInOut :: Spawn( void )
{
	InitTrigger();
	// create a null-terminator for the registry
	m_pRegister = GetClassPtr( (CInOutRegister*)NULL );
	m_pRegister->m_hValue = NULL;
	m_pRegister->m_pNext = NULL;
	m_pRegister->m_pField = this;
	m_pRegister->pev->classname = MAKE_STRING("inout_register");

	// Cthulhu
	//mbRestored = FALSE;
}

void CTriggerInOut :: Touch( CBaseEntity *pOther )
{
	if (!CanTouch(pOther->pev))
		return;

	if (!(m_pRegister->IsRegistered( pOther )))
	{
		// a new entity has entered the field! Trigger stuff, and register it
		// Cthulhu : if we have restored, we do not fire stuff that is already in the area...I hope
		// we do this for everything before we think
		//if (UTIL_IsMasterTriggered(m_sMaster,pOther) && !mbRestored)
		if (UTIL_IsMasterTriggered(m_sMaster,pOther))
		{
			FireTargets(STRING(m_iszBothTarget), pOther, this, USE_ON, 0);
			FireTargets(STRING(pev->target), pOther, this, USE_TOGGLE, 0);
		}

		CInOutRegister *pTemp = GetClassPtr( (CInOutRegister*)NULL );
		pTemp->m_hValue = pOther;
		pTemp->m_pNext = m_pRegister;
		pTemp->m_pField = this;
		pTemp->pev->classname = MAKE_STRING("inout_register");
		m_pRegister = pTemp;

		if (m_fNextThink <= 0)
			SetNextThink( 0.1 );
	}
}

void CTriggerInOut :: Think( void )
{
	// Prune handles all Intersects tests and fires targets as appropriate
	m_pRegister = m_pRegister->Prune();

	if (m_pRegister->IsEmpty())
		DontThink();
	else
		SetNextThink( 0.1 );

	// Cthulhu
	// we have thought, so everything should now be ok....
	//mbRestored = FALSE;
}

void CTriggerInOut :: FireOnLeaving( CBaseEntity *pEnt )
{
	if ( UTIL_IsMasterTriggered(m_sMaster, pEnt) )
	{
		FireTargets(STRING(m_iszBothTarget), pEnt, this, USE_OFF, 0);
		FireTargets(STRING(m_iszAltTarget), pEnt, this, USE_TOGGLE, 0);
	}
}


// ========================= COUNTING TRIGGER =====================================

LINK_ENTITY_TO_CLASS( trigger_counter, CTriggerCounter );

void CTriggerCounter :: KeyValue( KeyValueData *pkvd )
{
	if (FStrEq(pkvd->szKeyName, "count"))
	{
		m_cTriggersLeft = (int) atof(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else
		CTriggerMultiple::KeyValue( pkvd );
}

void CTriggerCounter :: Spawn( void )
{
	// By making the flWait be -1, this counter-trigger will disappear after it's activated
	// (but of course it needs cTriggersLeft "uses" before that happens).
	m_flWait = -1;

	if (m_cTriggersLeft == 0)
		m_cTriggersLeft = 2;
	SetUse( CounterUse );
}

void CTriggerCounter::CounterUse( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	m_cTriggersLeft--;
	m_hActivator = pActivator;

	if (m_cTriggersLeft < 0)
		return;
	
	BOOL fTellActivator =
		(FClassnameIs(m_hActivator->pev, "player") &&
		!FBitSet(pev->spawnflags, SPAWNFLAG_NOMESSAGE));
	if (m_cTriggersLeft != 0)
	{
		if (fTellActivator)
		{
			// UNDONE: I don't think we want these Quakesque messages
			switch (m_cTriggersLeft)
			{
			case 1:		ALERT(at_debug, "Only 1 more to go...");		break;
			case 2:		ALERT(at_debug, "Only 2 more to go...");		break;
			case 3:		ALERT(at_debug, "Only 3 more to go...");		break;
			default:	ALERT(at_debug, "There are more to go...");	break;
			}
		}
		return;
	}

	// !!!UNDONE: I don't think we want these Quakesque messages
	if (fTellActivator)
		ALERT(at_debug, "Sequence completed!");
	
	ActivateMultiTrigger( m_hActivator );
}

// ====================== TRIGGER_CHANGELEVEL ================================


LINK_ENTITY_TO_CLASS( trigger_transition, CTriggerVolume );

// Define space that travels across a level transition
void CTriggerVolume :: Spawn( void )
{
	pev->solid = SOLID_NOT;
	pev->movetype = MOVETYPE_NONE;
	SET_MODEL(ENT(pev), STRING(pev->model));    // set size and link into world
	pev->model = NULL;
	pev->modelindex = 0;
}


// Fires a target after level transition and then dies

LINK_ENTITY_TO_CLASS( fireanddie, CFireAndDie );

void CFireAndDie::Spawn( void )
{
	pev->classname = MAKE_STRING("fireanddie");
	// Don't call Precache() - it should be called on restore
}


void CFireAndDie::Precache( void )
{
	// This gets called on restore
	SetNextThink( m_flDelay );
}


void CFireAndDie::Think( void )
{
	SUB_UseTargets( this, USE_TOGGLE, 0 );
	UTIL_Remove( this );
}


#define SF_CHANGELEVEL_USEONLY		0x0002

LINK_ENTITY_TO_CLASS( trigger_changelevel, CChangeLevel );

// Global Savedata for changelevel trigger
TYPEDESCRIPTION	CChangeLevel::m_SaveData[] = 
{
	DEFINE_ARRAY( CChangeLevel, m_szMapName, FIELD_CHARACTER, cchMapNameMost ),
	DEFINE_ARRAY( CChangeLevel, m_szLandmarkName, FIELD_CHARACTER, cchMapNameMost ),
	DEFINE_FIELD( CChangeLevel, m_changeTarget, FIELD_STRING ),
	DEFINE_FIELD( CChangeLevel, m_changeTargetDelay, FIELD_FLOAT ),
};

IMPLEMENT_SAVERESTORE(CChangeLevel,CBaseTrigger);

//
// Cache user-entity-field values until spawn is called.
//

void CChangeLevel :: KeyValue( KeyValueData *pkvd )
{
	if (FStrEq(pkvd->szKeyName, "map"))
	{
		if (strlen(pkvd->szValue) >= cchMapNameMost)
			ALERT( at_error, "Map name '%s' too long (32 chars)\n", pkvd->szValue );
		strcpy(m_szMapName, pkvd->szValue);
		//LRC -- don't allow changelevels to contain capital letters; it causes problems
//		ALERT(at_console, "MapName %s ", m_szMapName);
		// Cthulhu - this line causes a bug!!!
//		for (int i = 0; m_szMapName[i]; i++) { m_szMapName[i] = tolower(m_szMapName[i]); }
//		ALERT(at_console, "changed to %s\n", m_szMapName);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "landmark"))
	{
		if (strlen(pkvd->szValue) >= cchMapNameMost)
			ALERT( at_error, "Landmark name '%s' too long (32 chars)\n", pkvd->szValue );
		strcpy(m_szLandmarkName, pkvd->szValue);

		// Cthulhu: is this the insane map landmark
		if (FStrEq(m_szLandmarkName, "lm_insane"))
		{
			strcpy(m_szMapName, st_szPreInsaneMap);
		}

		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "changetarget"))
	{
		m_changeTarget = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "changedelay"))
	{
		m_changeTargetDelay = atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else
		CBaseTrigger::KeyValue( pkvd );
}


/*QUAKED trigger_changelevel (0.5 0.5 0.5) ? NO_INTERMISSION
When the player touches this, he gets sent to the map listed in the "map" variable.  Unless the NO_INTERMISSION flag is set, the view will go to the info_intermission spot and display stats.
*/

void CChangeLevel :: Spawn( void )
{
	if ( FStrEq( m_szMapName, "" ) )
		ALERT( at_debug, "a trigger_changelevel doesn't have a map" );

	if ( FStrEq( m_szLandmarkName, "" ) )
		ALERT( at_debug, "trigger_changelevel to %s doesn't have a landmark", m_szMapName );

	if (!FStringNull ( pev->targetname ) )
	{
		SetUse ( UseChangeLevel );
	}
	InitTrigger();
	if ( !(pev->spawnflags & SF_CHANGELEVEL_USEONLY) )
		SetTouch( TouchChangeLevel );
//	ALERT( at_debug, "TRANSITION: %s (%s)\n", m_szMapName, m_szLandmarkName );
}


void CChangeLevel :: ExecuteChangeLevel( void )
{
	MESSAGE_BEGIN( MSG_ALL, SVC_CDTRACK );
		WRITE_BYTE( 3 );
		WRITE_BYTE( 3 );
	MESSAGE_END();

	MESSAGE_BEGIN(MSG_ALL, SVC_INTERMISSION);
	MESSAGE_END();
}


FILE_GLOBAL char st_szNextMap[cchMapNameMost];
FILE_GLOBAL char st_szNextSpot[cchMapNameMost];

edict_t *CChangeLevel :: FindLandmark( const char *pLandmarkName )
{
	CBaseEntity	*pLandmark;

	pLandmark = UTIL_FindEntityByTargetname( NULL, pLandmarkName );
	while ( pLandmark )
	{
		// Found the landmark
		if ( FClassnameIs( pLandmark->pev, "info_landmark" ) )
			return ENT(pLandmark->pev);
		else
			pLandmark = UTIL_FindEntityByTargetname( pLandmark, pLandmarkName );
	}
	ALERT( at_error, "Can't find landmark %s\n", pLandmarkName );
	return NULL;
}


//=========================================================
// CChangeLevel :: Use - allows level transitions to be 
// triggered by buttons, etc.
//
//=========================================================
void CChangeLevel :: UseChangeLevel ( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	ChangeLevelNow( pActivator );
}

void CChangeLevel :: ChangeLevelNow( CBaseEntity *pActivator )
{
	edict_t	*pentLandmark;
	LEVELLIST	levels[16];

	ASSERT(!FStrEq(m_szMapName, ""));

	// Don't work in deathmatch
	if ( g_pGameRules->IsDeathmatch() )
		return;

	// Some people are firing these multiple times in a frame, disable
	if ( gpGlobals->time == pev->dmgtime )
		return;

	pev->dmgtime = gpGlobals->time;


	CBaseEntity *pPlayer = CBaseEntity::Instance( g_engfuncs.pfnPEntityOfEntIndex( 1 ) );
	if ( !InTransitionVolume( pPlayer, m_szLandmarkName ) )
	{
		ALERT( at_aiconsole, "Player isn't in the transition volume %s, aborting\n", m_szLandmarkName );
		return;
	}

	// Cthulhu: shut down any active cameras.
	CTriggerCamera* pCamera = NULL;
	do
	{
		// find all camera entities
		pCamera = (CTriggerCamera*)UTIL_FindEntityByClassname(pCamera, "trigger_camera");
		if (!pCamera) break;
		// is it active
		if (pCamera->m_state)
		{
			// tell the camera that it's time is up, and to hand control back to the player
			pCamera->m_flReturnTime = gpGlobals->time - 1.0;
			pCamera->FollowTarget();
		}
	} while (true);

	// Cthulhu: shut down any book pages
	UTIL_ReadBook("");


	// Create an entity to fire the changetarget
	if ( m_changeTarget )
	{
		CFireAndDie *pFireAndDie = GetClassPtr( (CFireAndDie *)NULL );
		if ( pFireAndDie )
		{
			// Set target and delay
			pFireAndDie->pev->target = m_changeTarget;
			pFireAndDie->m_flDelay = m_changeTargetDelay;
			pFireAndDie->pev->origin = pPlayer->pev->origin;
			// Call spawn
			DispatchSpawn( pFireAndDie->edict() );
		}
	}
	// This object will get removed in the call to CHANGE_LEVEL, copy the params into "safe" memory
	strcpy(st_szNextMap, m_szMapName);

	m_hActivator = pActivator;
	SUB_UseTargets( pActivator, USE_TOGGLE, 0 );
	st_szNextSpot[0] = 0;	// Init landmark to NULL

	// look for a landmark entity		
	pentLandmark = FindLandmark( m_szLandmarkName );
	if ( !FNullEnt( pentLandmark ) )
	{
		strcpy(st_szNextSpot, m_szLandmarkName);
		gpGlobals->vecLandmarkOffset = VARS(pentLandmark)->origin;
	}
//	ALERT( at_debug, "Level touches %d levels\n", ChangeList( levels, 16 ) );
	ALERT( at_debug, "CHANGE LEVEL: %s %s\n", st_szNextMap, st_szNextSpot );
	CHANGE_LEVEL( st_szNextMap, st_szNextSpot );
}

//
// GLOBALS ASSUMED SET:  st_szNextMap
//
void CChangeLevel :: TouchChangeLevel( CBaseEntity *pOther )
{
	if (!FClassnameIs(pOther->pev, "player"))
		return;

	ChangeLevelNow( pOther );
}


// Add a transition to the list, but ignore duplicates 
// (a designer may have placed multiple trigger_changelevels with the same landmark)
int CChangeLevel::AddTransitionToList( LEVELLIST *pLevelList, int listCount, const char *pMapName, const char *pLandmarkName, edict_t *pentLandmark )
{
	int i;

	if ( !pLevelList || !pMapName || !pLandmarkName || !pentLandmark )
		return 0;

	for ( i = 0; i < listCount; i++ )
	{
		if ( pLevelList[i].pentLandmark == pentLandmark && strcmp( pLevelList[i].mapName, pMapName ) == 0 )
			return 0;
	}
	strcpy( pLevelList[listCount].mapName, pMapName );
	strcpy( pLevelList[listCount].landmarkName, pLandmarkName );
	pLevelList[listCount].pentLandmark = pentLandmark;
	pLevelList[listCount].vecLandmarkOrigin = VARS(pentLandmark)->origin;

	return 1;
}

int BuildChangeList( LEVELLIST *pLevelList, int maxList )
{
	return CChangeLevel::ChangeList( pLevelList, maxList );
}


int CChangeLevel::InTransitionVolume( CBaseEntity *pEntity, char *pVolumeName )
{
	CBaseEntity	*pVolume;


	if ( pEntity->ObjectCaps() & FCAP_FORCE_TRANSITION )
		return 1;

	// If you're following another entity, follow it through the transition (weapons follow the player)
	if ( pEntity->pev->movetype == MOVETYPE_FOLLOW )
	{
		if ( pEntity->pev->aiment != NULL )
			pEntity = CBaseEntity::Instance( pEntity->pev->aiment );
	}

	int inVolume = 1;	// Unless we find a trigger_transition, everything is in the volume

	pVolume = UTIL_FindEntityByTargetname( NULL, pVolumeName );
	while ( pVolume )
	{
		if ( FClassnameIs( pVolume->pev, "trigger_transition" ) )
		{
			if ( pVolume->Intersects( pEntity ) )	// It touches one, it's in the volume
				return 1;
			else
				inVolume = 0;	// Found a trigger_transition, but I don't intersect it -- if I don't find another, don't go!
		}
		pVolume = UTIL_FindEntityByTargetname( pVolume, pVolumeName );
	}

	return inVolume;
}


// We can only ever move 512 entities across a transition
#define MAX_ENTITY 512

// This has grown into a complicated beast
// Can we make this more elegant?
// This builds the list of all transitions on this level and which entities are in their PVS's and
// can / should be moved across.
int CChangeLevel::ChangeList( LEVELLIST *pLevelList, int maxList )
{
	edict_t *pentLandmark;
	int			i, count;

	count = 0;

	// Find all of the possible level changes on this BSP
	CBaseEntity *pChangelevel = UTIL_FindEntityByClassname( NULL, "trigger_changelevel" );

	if ( !pChangelevel )
		return NULL;

	while ( pChangelevel )
	{
		CChangeLevel *pTrigger;
		
		pTrigger = GetClassPtr((CChangeLevel *)pChangelevel->pev);
		if ( pTrigger )
		{
			// Find the corresponding landmark
			pentLandmark = FindLandmark( pTrigger->m_szLandmarkName );
			if ( pentLandmark )
			{
				// Build a list of unique transitions
				if ( AddTransitionToList( pLevelList, count, pTrigger->m_szMapName, pTrigger->m_szLandmarkName, pentLandmark ) )
				{
					count++;
					if ( count >= maxList )		// FULL!!
						break;
				}
			}
		}
		pChangelevel = UTIL_FindEntityByClassname( pChangelevel, "trigger_changelevel" );
	}

	if ( gpGlobals->pSaveData && ((SAVERESTOREDATA *)gpGlobals->pSaveData)->pTable )
	{
		CSave saveHelper( (SAVERESTOREDATA *)gpGlobals->pSaveData );

		for ( i = 0; i < count; i++ )
		{
			int j, entityCount = 0;
			CBaseEntity *pEntList[ MAX_ENTITY ];
			int			 entityFlags[ MAX_ENTITY ];

			// Follow the linked list of entities in the PVS of the transition landmark
			edict_t *pent = UTIL_EntitiesInPVS( pLevelList[i].pentLandmark );

			// Build a list of valid entities in this linked list (we're going to use pent->v.chain again)
			while ( !FNullEnt( pent ) )
			{
				CBaseEntity *pEntity = CBaseEntity::Instance(pent);
				if ( pEntity )
				{
//					ALERT( at_debug, "Trying %s\n", STRING(pEntity->pev->classname) );
					int caps = pEntity->ObjectCaps();
					if ( !(caps & FCAP_DONT_SAVE) )
					{
						int flags = 0;

						// If this entity can be moved or is global, mark it
						if ( caps & FCAP_ACROSS_TRANSITION )
							flags |= FENTTABLE_MOVEABLE;
						if ( pEntity->pev->globalname && !pEntity->IsDormant() )
							flags |= FENTTABLE_GLOBAL;
						if ( flags )
						{
							pEntList[ entityCount ] = pEntity;
							entityFlags[ entityCount ] = flags;
							entityCount++;
							if ( entityCount > MAX_ENTITY )
								ALERT( at_error, "Too many entities across a transition!" );
						}
//						else
//							ALERT( at_debug, "Failed %s\n", STRING(pEntity->pev->classname) );
					}
//					else
//						ALERT( at_debug, "DON'T SAVE %s\n", STRING(pEntity->pev->classname) );
				}
				pent = pent->v.chain;
			}

			for ( j = 0; j < entityCount; j++ )
			{
				// Check to make sure the entity isn't screened out by a trigger_transition
				if ( entityFlags[j] && InTransitionVolume( pEntList[j], pLevelList[i].landmarkName ) )
				{
					// Mark entity table with 1<<i
					int index = saveHelper.EntityIndex( pEntList[j] );
					// Flag it with the level number
					saveHelper.EntityFlagsSet( index, entityFlags[j] | (1<<i) );
				}
//				else
//					ALERT( at_debug, "Screened out %s\n", STRING(pEntList[j]->pev->classname) );

			}
		}
	}

	return count;
}

/*
go to the next level for deathmatch
only called if a time or frag limit has expired
*/
void NextLevel( void )
{
	CBaseEntity* pEnt;
	CChangeLevel *pChange;
	
	// find a trigger_changelevel
	pEnt = UTIL_FindEntityByClassname(NULL, "trigger_changelevel");
	
	// go back to start if no trigger_changelevel
	if ( !pEnt )
	{
		gpGlobals->mapname = MAKE_STRING("start");
		pChange = GetClassPtr( (CChangeLevel *)NULL );
		strcpy(pChange->m_szMapName, "start");
	}
	else
		pChange = GetClassPtr( (CChangeLevel *)pEnt->pev );
	
	strcpy(st_szNextMap, pChange->m_szMapName);
	g_fGameOver = TRUE;
	
	pChange->SetNextThink( 0 );
	if (pChange->m_fNextThink)
	{
		pChange->SetThink( CChangeLevel::ExecuteChangeLevel );
		pChange->SetNextThink( 0.1 );
	}
}


// ============================== LADDER =======================================

#define SF_LADDER_VISIBLE	1

LINK_ENTITY_TO_CLASS( func_ladder, CLadder );


void CLadder :: KeyValue( KeyValueData *pkvd )
{
	CBaseTrigger::KeyValue( pkvd );
}


//=========================================================
// func_ladder - makes an area vertically negotiable
//=========================================================
void CLadder :: Precache( void )
{
	// Do all of this in here because we need to 'convert' old saved games
	pev->solid = SOLID_NOT;
	pev->skin = CONTENTS_LADDER;
	if ( CVAR_GET_FLOAT("showtriggers") == 0 && !(pev->spawnflags & SF_LADDER_VISIBLE))
	{
		pev->effects |= EF_NODRAW;
		//LRC- NODRAW is a better-performance way to stop things being drawn.
		// (unless... would it prevent client-side movement algorithms from working?)
//		pev->rendermode = kRenderTransTexture;
//		pev->renderamt = 0;
	}
	else
		pev->effects &= ~EF_NODRAW;
}


void CLadder :: Spawn( void )
{
	Precache();

	SET_MODEL(ENT(pev), STRING(pev->model));    // set size and link into world
	pev->movetype = MOVETYPE_PUSH;
}


// ========================== A TRIGGER THAT PUSHES YOU ===============================

LINK_ENTITY_TO_CLASS( trigger_push, CTriggerPush );


void CTriggerPush :: KeyValue( KeyValueData *pkvd )
{
	CBaseTrigger::KeyValue( pkvd );
}


/*QUAKED trigger_push (.5 .5 .5) ? TRIG_PUSH_ONCE
Pushes the player
*/

void CTriggerPush :: Spawn( )
{
	if ( pev->angles == g_vecZero )
		pev->angles.y = 360;
	InitTrigger();

	if (pev->speed == 0)
		pev->speed = 100;

	if ( FBitSet (pev->spawnflags, SF_TRIGGER_PUSH_START_OFF) )// if flagged to Start Turned Off, make trigger nonsolid.
		pev->solid = SOLID_NOT;

	SetUse( ToggleUse );

	UTIL_SetOrigin( this, pev->origin );		// Link into the list
}


void CTriggerPush :: Touch( CBaseEntity *pOther )
{
	entvars_t* pevToucher = pOther->pev;

	// UNDONE: Is there a better way than health to detect things that have physics? (clients/monsters)
	switch( pevToucher->movetype )
	{
	case MOVETYPE_NONE:
	case MOVETYPE_PUSH:
	case MOVETYPE_NOCLIP:
	case MOVETYPE_FOLLOW:
		return;
	}

	// are we turned on?
	if (pev->solid == SOLID_NOT) return;

	if ( pevToucher->solid != SOLID_NOT && pevToucher->solid != SOLID_BSP )
	{
		// Instant trigger, just transfer velocity and remove
		if (FBitSet(pev->spawnflags, SF_TRIG_PUSH_ONCE))
		{
			pevToucher->velocity = pevToucher->velocity + (pev->speed * pev->movedir);
			if ( pevToucher->velocity.z > 0 )
				pevToucher->flags &= ~FL_ONGROUND;
			UTIL_Remove( this );
		}
		else
		{	// Push field, transfer to base velocity
			Vector vecPush = (pev->speed * pev->movedir);
			if ( pevToucher->flags & FL_BASEVELOCITY )
				vecPush = vecPush +  pevToucher->basevelocity;

			pevToucher->basevelocity = vecPush;

			pevToucher->flags |= FL_BASEVELOCITY;
//			ALERT( at_debug, "Vel %f, base %f\n", pevToucher->velocity.z, pevToucher->basevelocity.z );
		}
	}
}


//===========================================================
//LRC- trigger_bounce
//===========================================================
#define SF_BOUNCE_CUTOFF 16

class CTriggerBounce : public CBaseTrigger
{
public:
	void Spawn( void );
	void Touch( CBaseEntity *pOther );
};

LINK_ENTITY_TO_CLASS( trigger_bounce, CTriggerBounce );


void CTriggerBounce :: Spawn( void )
{
	SetMovedir(pev);
	InitTrigger();
}

void CTriggerBounce :: Touch( CBaseEntity *pOther )
{
	if (!UTIL_IsMasterTriggered(m_sMaster, pOther))
		return;
	if (!CanTouch(pOther->pev))
		return;

	float dot = DotProduct(pev->movedir, pOther->pev->velocity);
	if (dot < -pev->sanity)
	{
		if (pev->spawnflags & SF_BOUNCE_CUTOFF)
			pOther->pev->velocity = pOther->pev->velocity - (dot + pev->frags*(dot+pev->sanity))*pev->movedir;
		else
			pOther->pev->velocity = pOther->pev->velocity - (dot + pev->frags*dot)*pev->movedir;
		SUB_UseTargets( pOther, USE_TOGGLE, 0 );
	}
}


//===========================================================
//LRC- trigger_onsight
//===========================================================
#define SF_ONSIGHT_NOLOS   0x00001
#define SF_ONSIGHT_NOGLASS 0x00002
#define SF_ONSIGHT_ACTIVE  0x08000
#define SF_ONSIGHT_DEMAND  0x10000

class CTriggerOnSight : public CBaseDelay
{
public:
	void Spawn( void );
	void Think( void );
	BOOL VisionCheck( void );
	BOOL CanSee(CBaseEntity *pLooker, CBaseEntity *pSeen);
	virtual int	ObjectCaps( void ) { return CBaseEntity :: ObjectCaps() & ~FCAP_ACROSS_TRANSITION; }

	STATE GetState();
};

LINK_ENTITY_TO_CLASS( trigger_onsight, CTriggerOnSight );

void CTriggerOnSight :: Spawn( void )
{
	if (pev->target || pev->noise)
		// if we're going to have to trigger stuff, start thinking
		SetNextThink( 1 );
	else
		// otherwise, just check whenever someone asks about our state.
		pev->spawnflags |= SF_ONSIGHT_DEMAND;

	if (pev->max_health > 0)
	{
		pev->health = cos(pev->max_health/2 * M_PI/180.0);
//		ALERT(at_debug, "Cosine is %f\n", pev->health);
	}
}

STATE CTriggerOnSight :: GetState( void )
{
	if (pev->spawnflags & SF_ONSIGHT_DEMAND)
		return VisionCheck()?STATE_ON:STATE_OFF;
	else
		return (pev->spawnflags & SF_ONSIGHT_ACTIVE)?STATE_ON:STATE_OFF;
}

void CTriggerOnSight :: Think( void )
{
	// is this a sensible rate?
	SetNextThink( 0.1 );

//	if (!UTIL_IsMasterTriggered(m_sMaster, NULL))
//	{
//		pev->spawnflags &= ~SF_ONSIGHT_ACTIVE;
//		return;
//	}

	if (VisionCheck())
	{
		if (!FBitSet(pev->spawnflags, SF_ONSIGHT_ACTIVE))
		{
			FireTargets(STRING(pev->target), this, this, USE_TOGGLE, 0);
			FireTargets(STRING(pev->noise1), this, this, USE_ON, 0);
			pev->spawnflags |= SF_ONSIGHT_ACTIVE;
		}
	}
	else
	{
		if (pev->spawnflags & SF_ONSIGHT_ACTIVE)
		{
			FireTargets(STRING(pev->noise), this, this, USE_TOGGLE, 0);
			FireTargets(STRING(pev->noise1), this, this, USE_OFF, 0);
			pev->spawnflags &= ~SF_ONSIGHT_ACTIVE;
		}
	}
}

BOOL CTriggerOnSight :: VisionCheck( void )
{
	CBaseEntity *pLooker;
	if (pev->netname)
	{
		pLooker = UTIL_FindEntityByTargetname(NULL, STRING(pev->netname));
		if (!pLooker)
			return FALSE; // if we can't find the eye entity, give up
	}
	else
	{
		pLooker = UTIL_FindEntityByClassname(NULL, "player");
		if (!pLooker)
		{
			ALERT(at_error, "trigger_onsight can't find player!?\n");
			return FALSE;
		}
	}

	CBaseEntity *pSeen;
	if (pev->message)
		pSeen = UTIL_FindEntityByTargetname(NULL, STRING(pev->message));
	else
		return CanSee(pLooker, this);

	if (!pSeen)
	{
		// must be a classname.
		pSeen = UTIL_FindEntityByClassname(pSeen, STRING(pev->message));
		while (pSeen != NULL)
		{
			if (CanSee(pLooker, pSeen))
				return TRUE;
			pSeen = UTIL_FindEntityByClassname(pSeen, STRING(pev->message));
		}
		return FALSE;
	}
	else
	{
		while (pSeen != NULL)
		{
			if (CanSee(pLooker, pSeen))
				return TRUE;
			pSeen = UTIL_FindEntityByTargetname(pSeen, STRING(pev->message));
		}
		return FALSE;
	}
}

// by the criteria we're using, can the Looker see the Seen entity?
BOOL CTriggerOnSight :: CanSee(CBaseEntity *pLooker, CBaseEntity *pSeen)
{
	// out of range?
	if (pev->frags && (pLooker->pev->origin - pSeen->pev->origin).Length() > pev->frags)
		return FALSE;

	// check FOV if appropriate
	if (pev->max_health < 360)
	{
		// copied from CBaseMonster's FInViewCone function
		Vector2D	vec2LOS;
		float	flDot;
		float flComp = pev->health;
		UTIL_MakeVectors ( pLooker->pev->angles );
		vec2LOS = ( pSeen->pev->origin - pLooker->pev->origin ).Make2D();
		vec2LOS = vec2LOS.Normalize();
		flDot = DotProduct (vec2LOS , gpGlobals->v_forward.Make2D() );

//		ALERT(at_debug, "flDot is %f\n", flDot);

		if ( pev->max_health == -1 )
		{
			CBaseMonster *pMonst = pLooker->MyMonsterPointer();
			if (pMonst)
				flComp = pMonst->m_flFieldOfView;
			else
				return FALSE; // not a monster, can't use M-M-M-MonsterVision
		}

		// outside field of view
		if (flDot <= flComp)
			return FALSE;
	}

	// check LOS if appropriate
	if (!FBitSet(pev->spawnflags, SF_ONSIGHT_NOLOS))
	{
		TraceResult tr;
		if (SF_ONSIGHT_NOGLASS)
			UTIL_TraceLine( pLooker->EyePosition(), pSeen->pev->origin, ignore_monsters, ignore_glass, pLooker->edict(), &tr );
		else
			UTIL_TraceLine( pLooker->EyePosition(), pSeen->pev->origin, ignore_monsters, dont_ignore_glass, pLooker->edict(), &tr );
		if (tr.flFraction < 1.0 && tr.pHit != pSeen->edict())
			return FALSE;
	}

	return TRUE;
}

//======================================
// teleport trigger
//
//


LINK_ENTITY_TO_CLASS( trigger_teleport, CTriggerTeleport );

void CTriggerTeleport :: Spawn( void )
{
	InitTrigger();

	SetTouch( TeleportTouch );
}

void CTriggerTeleport :: TeleportTouch( CBaseEntity *pOther )
{
	entvars_t* pevToucher = pOther->pev;
	CBaseEntity *pTarget = NULL;

	// Only teleport monsters or clients
	if ( !FBitSet( pevToucher->flags, FL_CLIENT|FL_MONSTER ) )
		return;
    
	if (!UTIL_IsMasterTriggered(m_sMaster, pOther))
		return;
 	
	if (!CanTouch(pevToucher))
		return;
	
	pTarget = UTIL_FindEntityByTargetname( pTarget, STRING(pev->target) );
	if ( !pTarget )
	   return;

	//LRC - landmark based teleports
	CBaseEntity *pLandmark = UTIL_FindEntityByTargetname( NULL, STRING(pev->message) );
	if ( pLandmark )
	{
		Vector vecOriginOffs = pTarget->pev->origin - pLandmark->pev->origin;
		//ALERT(at_console, "Offs initially: %f %f %f\n", vecOriginOffs.x, vecOriginOffs.y, vecOriginOffs.z);

		// do we need to rotate the entity?
		if ( pLandmark->pev->angles != pTarget->pev->angles )
		{
			Vector vecVA;
			float ydiff = pTarget->pev->angles.y - pLandmark->pev->angles.y;

			// set new angle to face
			ALERT(at_debug, "angles = %f %f %f\n", pOther->pev->angles.x, pOther->pev->angles.y, pOther->pev->angles.z);
			pOther->pev->angles.y += ydiff;
			if (pOther->IsPlayer())
			{
				ALERT(at_debug, "v_angle = %f %f %f\n", pOther->pev->v_angle.x, pOther->pev->v_angle.y, pOther->pev->v_angle.z);
				pOther->pev->angles.x = pOther->pev->v_angle.x;
//				pOther->pev->v_angle.y += ydiff;
			}

			// set new velocity
			vecVA = UTIL_VecToAngles(pOther->pev->velocity);
			vecVA.y += ydiff;
			UTIL_MakeVectors(vecVA);
			pOther->pev->velocity = gpGlobals->v_forward * pOther->pev->velocity.Length();
			// fix the ugly "angle to vector" behaviour - a legacy from Quake
			pOther->pev->velocity.z = -pOther->pev->velocity.z;
			
			// set new origin
			Vector vecPlayerOffs = pOther->pev->origin - pLandmark->pev->origin;
			//ALERT(at_console, "PlayerOffs: %f %f %f\n", vecPlayerOffs.x, vecPlayerOffs.y, vecPlayerOffs.z);
			vecVA = UTIL_VecToAngles(vecPlayerOffs);
			UTIL_MakeVectors(vecVA);
			vecVA.y += ydiff;
			UTIL_MakeVectors(vecVA);
			Vector vecPlayerOffsNew = gpGlobals->v_forward * vecPlayerOffs.Length();
			vecPlayerOffsNew.z = -vecPlayerOffsNew.z;
			//ALERT(at_console, "PlayerOffsNew: %f %f %f\n", vecPlayerOffsNew.x, vecPlayerOffsNew.y, vecPlayerOffsNew.z);

			vecOriginOffs = vecOriginOffs + vecPlayerOffsNew - vecPlayerOffs;
			//ALERT(at_console, "vecOriginOffs: %f %f %f\n", vecOriginOffs.x, vecOriginOffs.y, vecOriginOffs.z);
//			vecOriginOffs.y++;
		}

		UTIL_SetOrigin( pOther, pOther->pev->origin + vecOriginOffs );
	}
	else
	{
		Vector tmp = pTarget->pev->origin;

		if ( pOther->IsPlayer() )
		{
			tmp.z -= pOther->pev->mins.z;// make origin adjustments in case the teleportee is a player. (origin in center, not at feet)
		}
		tmp.z++;
		UTIL_SetOrigin( pOther, tmp );

		pOther->pev->angles = pTarget->pev->angles;
		pOther->pev->velocity = pOther->pev->basevelocity = g_vecZero;
		if ( pOther->IsPlayer() )
		{
			pOther->pev->v_angle = pTarget->pev->angles; //LRC
		}

		pevToucher->fixangle = TRUE;
	}

	pevToucher->flags &= ~FL_ONGROUND;
	pevToucher->fixangle = TRUE;

	FireTargets(STRING(pev->noise), pOther, this, USE_TOGGLE, 0);
}


LINK_ENTITY_TO_CLASS( info_teleport_destination, CPointEntity );



LINK_ENTITY_TO_CLASS( trigger_autosave, CTriggerSave );

void CTriggerSave::Spawn( void )
{
	if ( g_pGameRules->IsDeathmatch() )
	{
		REMOVE_ENTITY( ENT(pev) );
		return;
	}

	InitTrigger();
	SetTouch( SaveTouch );
}

void CTriggerSave::SaveTouch( CBaseEntity *pOther )
{
	if ( !UTIL_IsMasterTriggered( m_sMaster, pOther ) )
		return;

	// Only save on clients
	if ( !pOther->IsPlayer() )
		return;
    
	SetTouch( NULL );
	UTIL_Remove( this );
	SERVER_COMMAND( "autosave\n" );
}

#define SF_ENDSECTION_USEONLY		0x0001

LINK_ENTITY_TO_CLASS( trigger_endsection, CTriggerEndSection );


void CTriggerEndSection::EndSectionUse( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	// Only save on clients
	if ( pActivator && !pActivator->IsNetClient() )
		return;
    
	SetUse( NULL );

	if ( pev->message )
	{
		g_engfuncs.pfnEndSection(STRING(pev->message));
	}
	UTIL_Remove( this );
}

void CTriggerEndSection::Spawn( void )
{
	if ( g_pGameRules->IsDeathmatch() )
	{
		REMOVE_ENTITY( ENT(pev) );
		return;
	}

	InitTrigger();

	SetUse ( EndSectionUse );
	// If it is a "use only" trigger, then don't set the touch function.
	if ( ! (pev->spawnflags & SF_ENDSECTION_USEONLY) )
		SetTouch( EndSectionTouch );
}

void CTriggerEndSection::EndSectionTouch( CBaseEntity *pOther )
{
	// Only save on clients
	if ( !pOther->IsNetClient() )
		return;
    
	SetTouch( NULL );

	if (pev->message)
	{
		g_engfuncs.pfnEndSection(STRING(pev->message));
	}
	UTIL_Remove( this );
}

void CTriggerEndSection :: KeyValue( KeyValueData *pkvd )
{
	if (FStrEq(pkvd->szKeyName, "section"))
	{
//		m_iszSectionName = ALLOC_STRING( pkvd->szValue );
		// Store this in message so we don't have to write save/restore for this ent
		pev->message = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else
		CBaseTrigger::KeyValue( pkvd );
}


LINK_ENTITY_TO_CLASS( trigger_gravity, CTriggerGravity );

void CTriggerGravity::Spawn( void )
{
	InitTrigger();
	SetTouch( GravityTouch );
}

void CTriggerGravity::GravityTouch( CBaseEntity *pOther )
{
	// Only save on clients
	if ( !pOther->IsPlayer() )
		return;

	pOther->pev->gravity = pev->gravity;
}




// new class for Spirit
LINK_ENTITY_TO_CLASS( trigger_startpatrol, CTriggerSetPatrol );

TYPEDESCRIPTION	CTriggerSetPatrol::m_SaveData[] = 
{
	DEFINE_FIELD( CTriggerSetPatrol, m_iszPath, FIELD_STRING ),
};

IMPLEMENT_SAVERESTORE(CTriggerSetPatrol,CBaseDelay);

void CTriggerSetPatrol::KeyValue( KeyValueData *pkvd )
{
	if (FStrEq(pkvd->szKeyName, "m_iszPath"))
	{
		m_iszPath = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else
		CBaseDelay::KeyValue( pkvd );
}

void CTriggerSetPatrol::Spawn( void )
{
}


void CTriggerSetPatrol::Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	CBaseEntity *pTarget = UTIL_FindEntityByTargetname( NULL, STRING( pev->target ), pActivator );
	CBaseEntity *pPath = UTIL_FindEntityByTargetname( NULL, STRING( m_iszPath ), pActivator );

	if (pTarget && pPath)
	{
		CBaseMonster *pMonster = pTarget->MyMonsterPointer();
		if (pMonster) pMonster->StartPatrol(pPath);
	}
}


//===========================================================
//LRC- trigger_motion
//===========================================================
#define SF_MOTION_DEBUG 1
LINK_ENTITY_TO_CLASS( trigger_motion, CTriggerMotion );

TYPEDESCRIPTION	CTriggerMotion::m_SaveData[] = 
{
	DEFINE_FIELD( CTriggerMotion, m_iszPosition, FIELD_STRING ),
	DEFINE_FIELD( CTriggerMotion, m_iPosMode, FIELD_INTEGER ),
	DEFINE_FIELD( CTriggerMotion, m_iszAngles, FIELD_STRING ),
	DEFINE_FIELD( CTriggerMotion, m_iAngMode, FIELD_INTEGER ),
	DEFINE_FIELD( CTriggerMotion, m_iszVelocity, FIELD_STRING ),
	DEFINE_FIELD( CTriggerMotion, m_iVelMode, FIELD_INTEGER ),
	DEFINE_FIELD( CTriggerMotion, m_iszAVelocity, FIELD_STRING ),
	DEFINE_FIELD( CTriggerMotion, m_iAVelMode, FIELD_INTEGER ),
};

IMPLEMENT_SAVERESTORE(CTriggerMotion,CPointEntity);

void CTriggerMotion::KeyValue( KeyValueData *pkvd )
{
	if (FStrEq(pkvd->szKeyName, "m_iszPosition"))
	{
		m_iszPosition = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "m_iPosMode"))
	{
		m_iPosMode = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "m_iszAngles"))
	{
		m_iszAngles = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "m_iAngMode"))
	{
		m_iAngMode = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "m_iszVelocity"))
	{
		m_iszVelocity = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "m_iVelMode"))
	{
		m_iVelMode = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "m_iszAVelocity"))
	{
		m_iszAVelocity = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "m_iAVelMode"))
	{
		m_iAVelMode = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else
		CPointEntity::KeyValue( pkvd );
}

void CTriggerMotion::Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	CBaseEntity *pTarget = UTIL_FindEntityByTargetname( NULL, STRING(pev->target), pActivator );
	if (pTarget == NULL || pActivator == NULL) return;

	if (pev->spawnflags & SF_MOTION_DEBUG)
		ALERT(at_debug, "DEBUG: trigger_motion affects %s \"%s\":\n", STRING(pTarget->pev->classname), STRING(pTarget->pev->targetname));

	if (m_iszPosition)
	{
		switch (m_iPosMode)
		{
		case 0:
			if (pev->spawnflags & SF_MOTION_DEBUG)
				ALERT(at_debug, "DEBUG: Set origin from %f %f %f ", pTarget->pev->origin.x, pTarget->pev->origin.y, pTarget->pev->origin.z);
			pTarget->pev->origin = CalcLocus_Position( this, pActivator, STRING(m_iszPosition) );
			if (pev->spawnflags & SF_MOTION_DEBUG)
				ALERT(at_debug, "to %f %f %f\n", pTarget->pev->origin.x, pTarget->pev->origin.y, pTarget->pev->origin.z);
			pTarget->pev->flags &= ~FL_ONGROUND;
			break;
		case 1:
			if (pev->spawnflags & SF_MOTION_DEBUG)
				ALERT(at_debug, "DEBUG: Set origin from %f %f %f ", pTarget->pev->origin.x, pTarget->pev->origin.y, pTarget->pev->origin.z);
			pTarget->pev->origin = pTarget->pev->origin + CalcLocus_Velocity( this, pActivator, STRING(m_iszPosition) );
			if (pev->spawnflags & SF_MOTION_DEBUG)
				ALERT(at_debug, "to %f %f %f\n", pTarget->pev->origin.x, pTarget->pev->origin.y, pTarget->pev->origin.z);
			pTarget->pev->flags &= ~FL_ONGROUND;
			break;
		}
	}

	Vector vecTemp;
	Vector vecVelAngles;
	if (m_iszAngles)
	{
		switch (m_iAngMode)
		{
		case 0:
			vecTemp = CalcLocus_Velocity( this, pActivator, STRING(m_iszAngles) );
			if (pev->spawnflags & SF_MOTION_DEBUG)
				ALERT(at_debug, "DEBUG: Set angles from %f %f %f ", pTarget->pev->angles.x, pTarget->pev->angles.y, pTarget->pev->angles.z);
			pTarget->pev->angles = UTIL_VecToAngles( vecTemp );
			if (pev->spawnflags & SF_MOTION_DEBUG)
				ALERT(at_debug, "to %f %f %f\n", pTarget->pev->angles.x, pTarget->pev->angles.y, pTarget->pev->angles.z);
			break;
		case 1:
			vecTemp = CalcLocus_Velocity( this, pActivator, STRING(m_iszVelocity) );
			if (pev->spawnflags & SF_MOTION_DEBUG)
				ALERT(at_debug, "DEBUG: Rotate angles from %f %f %f ", pTarget->pev->angles.x, pTarget->pev->angles.y, pTarget->pev->angles.z);
			pTarget->pev->angles = pTarget->pev->angles + UTIL_VecToAngles( vecTemp );
			if (pev->spawnflags & SF_MOTION_DEBUG)
				ALERT(at_debug, "to %f %f %f\n", pTarget->pev->angles.x, pTarget->pev->angles.y, pTarget->pev->angles.z);
			break;
		case 2:
			UTIL_StringToRandomVector( vecTemp, STRING(m_iszAngles) );
			if (pev->spawnflags & SF_MOTION_DEBUG)
				ALERT(at_debug, "DEBUG: Rotate angles from %f %f %f ", pTarget->pev->angles.x, pTarget->pev->angles.y, pTarget->pev->angles.z);
			pTarget->pev->angles = pTarget->pev->angles + vecTemp;
			if (pev->spawnflags & SF_MOTION_DEBUG)
				ALERT(at_debug, "to %f %f %f\n", pTarget->pev->angles.x, pTarget->pev->angles.y, pTarget->pev->angles.z);
			break;
		}
	}

	if (m_iszVelocity)
	{
		switch (m_iVelMode)
		{
		case 0:
			if (pev->spawnflags & SF_MOTION_DEBUG)
				ALERT(at_debug, "DEBUG: Set velocity from %f %f %f ", pTarget->pev->velocity.x, pTarget->pev->velocity.y, pTarget->pev->velocity.z);
			pTarget->pev->velocity = CalcLocus_Velocity( this, pActivator, STRING(m_iszVelocity) );
			if (pev->spawnflags & SF_MOTION_DEBUG)
				ALERT(at_debug, "to %f %f %f\n", pTarget->pev->velocity.x, pTarget->pev->velocity.y, pTarget->pev->velocity.z);
			break;
		case 1:
			if (pev->spawnflags & SF_MOTION_DEBUG)
				ALERT(at_debug, "DEBUG: Set velocity from %f %f %f ", pTarget->pev->velocity.x, pTarget->pev->velocity.y, pTarget->pev->velocity.z);
			pTarget->pev->velocity = pTarget->pev->velocity + CalcLocus_Velocity( this, pActivator, STRING(m_iszVelocity) );
			if (pev->spawnflags & SF_MOTION_DEBUG)
				ALERT(at_debug, "to %f %f %f\n", pTarget->pev->velocity.x, pTarget->pev->velocity.y, pTarget->pev->velocity.z);
			break;
		case 2:
			vecTemp = CalcLocus_Velocity( this, pActivator, STRING(m_iszVelocity) );
			vecVelAngles = UTIL_VecToAngles( vecTemp ) + UTIL_VecToAngles( pTarget->pev->velocity );
			UTIL_MakeVectors( vecVelAngles );
			if (pev->spawnflags & SF_MOTION_DEBUG)
				ALERT(at_debug, "DEBUG: Rotate velocity from %f %f %f ", pTarget->pev->velocity.x, pTarget->pev->velocity.y, pTarget->pev->velocity.z);
			pTarget->pev->velocity = pTarget->pev->velocity.Length() * gpGlobals->v_forward;
			pTarget->pev->velocity.z = -pTarget->pev->velocity.z; //vecToAngles reverses the z angle
			if (pev->spawnflags & SF_MOTION_DEBUG)
				ALERT(at_debug, "to %f %f %f\n", pTarget->pev->velocity.x, pTarget->pev->velocity.y, pTarget->pev->velocity.z);
			break;
		case 3:
			UTIL_StringToRandomVector( vecTemp, STRING(m_iszVelocity) );
			vecVelAngles = vecTemp + UTIL_VecToAngles( pTarget->pev->velocity );
			UTIL_MakeVectors( vecVelAngles );
			if (pev->spawnflags & SF_MOTION_DEBUG)
				ALERT(at_debug, "DEBUG: Rotate velocity from %f %f %f ", pTarget->pev->velocity.x, pTarget->pev->velocity.y, pTarget->pev->velocity.z);
			pTarget->pev->velocity = pTarget->pev->velocity.Length() * gpGlobals->v_forward;
			pTarget->pev->velocity.z = -pTarget->pev->velocity.z; //vecToAngles reverses the z angle
			if (pev->spawnflags & SF_MOTION_DEBUG)
				ALERT(at_debug, "to %f %f %f\n", pTarget->pev->velocity.x, pTarget->pev->velocity.y, pTarget->pev->velocity.z);
			break;
		}
	}

	switch (m_iAVelMode)
	{
	case 0:
		UTIL_StringToRandomVector( vecTemp, STRING(m_iszAVelocity) );
		if (pev->spawnflags & SF_MOTION_DEBUG)
			ALERT(at_debug, "DEBUG: Set avelocity from %f %f %f ", pTarget->pev->avelocity.x, pTarget->pev->avelocity.y, pTarget->pev->avelocity.z);
		pTarget->pev->avelocity = vecTemp;
		if (pev->spawnflags & SF_MOTION_DEBUG)
			ALERT(at_debug, "to %f %f %f\n", pTarget->pev->avelocity.x, pTarget->pev->avelocity.y, pTarget->pev->avelocity.z);
		break;
	case 1:
		UTIL_StringToRandomVector( vecTemp, STRING(m_iszAVelocity) );
		if (pev->spawnflags & SF_MOTION_DEBUG)
			ALERT(at_debug, "DEBUG: Set avelocity from %f %f %f ", pTarget->pev->avelocity.x, pTarget->pev->avelocity.y, pTarget->pev->avelocity.z);
		pTarget->pev->avelocity = pTarget->pev->avelocity + vecTemp;
		if (pev->spawnflags & SF_MOTION_DEBUG)
			ALERT(at_debug, "to %f %f %f\n", pTarget->pev->avelocity.x, pTarget->pev->avelocity.y, pTarget->pev->avelocity.z);
		break;
	}
}


//===========================================================
//LRC- motion_manager
//===========================================================
LINK_ENTITY_TO_CLASS( motion_thread, CPointEntity );

TYPEDESCRIPTION	CMotionThread::m_SaveData[] = 
{
	DEFINE_FIELD( CMotionThread, m_iszPosition, FIELD_STRING ),
	DEFINE_FIELD( CMotionThread, m_iPosMode, FIELD_INTEGER ),
	DEFINE_FIELD( CMotionThread, m_iszFacing, FIELD_STRING ),
	DEFINE_FIELD( CMotionThread, m_iFaceMode, FIELD_INTEGER ),
	DEFINE_FIELD( CMotionThread, m_hLocus, FIELD_EHANDLE ),
	DEFINE_FIELD( CMotionThread, m_hTarget, FIELD_EHANDLE ),
};

IMPLEMENT_SAVERESTORE(CMotionThread,CPointEntity);

void CMotionThread::Think( void )
{
	if (m_hLocus == NULL || m_hTarget == NULL)
	{
		if (pev->spawnflags & SF_MOTION_DEBUG)
			ALERT(at_debug, "motion_thread expires\n");
		SetThink( SUB_Remove );
		SetNextThink( 0.1 );
		return;
	}
	else
	{
		SetNextThink( 0 ); // think every frame
	}

	if (pev->spawnflags & SF_MOTION_DEBUG)
		ALERT(at_debug, "motion_thread affects %s \"%s\":\n", STRING(m_hTarget->pev->classname), STRING(m_hTarget->pev->targetname));

	Vector vecTemp;

	if (m_iszPosition)
	{
		switch (m_iPosMode)
		{
		case 0: // set position
			if (pev->spawnflags & SF_MOTION_DEBUG)
				ALERT(at_debug, "DEBUG: Set origin from %f %f %f ", m_hTarget->pev->origin.x, m_hTarget->pev->origin.y, m_hTarget->pev->origin.z);
			m_hTarget->pev->origin = CalcLocus_Position( this, m_hLocus, STRING(m_iszPosition) );
			if (pev->spawnflags & SF_MOTION_DEBUG)
				ALERT(at_debug, "to %f %f %f\n", m_hTarget->pev->origin.x, m_hTarget->pev->origin.y, m_hTarget->pev->origin.z);
			m_hTarget->pev->flags &= ~FL_ONGROUND;
			break;
		case 1: // offset position (= fake velocity)
			if (pev->spawnflags & SF_MOTION_DEBUG)
				ALERT(at_debug, "DEBUG: Offset origin from %f %f %f ", m_hTarget->pev->origin.x, m_hTarget->pev->origin.y, m_hTarget->pev->origin.z);
			m_hTarget->pev->origin = m_hTarget->pev->origin + gpGlobals->frametime * CalcLocus_Velocity( this, m_hLocus, STRING(m_iszPosition) );
			if (pev->spawnflags & SF_MOTION_DEBUG)
				ALERT(at_debug, "to %f %f %f\n", m_hTarget->pev->origin.x, m_hTarget->pev->origin.y, m_hTarget->pev->origin.z);
			m_hTarget->pev->flags &= ~FL_ONGROUND;
			break;
		case 2: // set velocity
			if (pev->spawnflags & SF_MOTION_DEBUG)
				ALERT(at_debug, "DEBUG: Set velocity from %f %f %f ", m_hTarget->pev->velocity.x, m_hTarget->pev->velocity.y, m_hTarget->pev->velocity.z);
			m_hTarget->pev->velocity = CalcLocus_Velocity( this, m_hLocus, STRING(m_iszPosition) );
			if (pev->spawnflags & SF_MOTION_DEBUG)
				ALERT(at_debug, "to %f %f %f\n", m_hTarget->pev->velocity.x, m_hTarget->pev->velocity.y, m_hTarget->pev->velocity.z);
			break;
		case 3: // accelerate
			if (pev->spawnflags & SF_MOTION_DEBUG)
				ALERT(at_debug, "DEBUG: Accelerate from %f %f %f ", m_hTarget->pev->velocity.x, m_hTarget->pev->velocity.y, m_hTarget->pev->velocity.z);
			m_hTarget->pev->velocity = m_hTarget->pev->velocity + gpGlobals->frametime * CalcLocus_Velocity( this, m_hLocus, STRING(m_iszPosition) );
			if (pev->spawnflags & SF_MOTION_DEBUG)
				ALERT(at_debug, "to %f %f %f\n", m_hTarget->pev->velocity.x, m_hTarget->pev->velocity.y, m_hTarget->pev->velocity.z);
			break;
		case 4: // follow position
			if (pev->spawnflags & SF_MOTION_DEBUG)
				ALERT(at_debug, "DEBUG: Set velocity (path) from %f %f %f ", m_hTarget->pev->velocity.x, m_hTarget->pev->velocity.y, m_hTarget->pev->velocity.z);
			m_hTarget->pev->velocity = CalcLocus_Position( this, m_hLocus, STRING(m_iszPosition) ) - m_hTarget->pev->origin;
			if (pev->spawnflags & SF_MOTION_DEBUG)
				ALERT(at_debug, "to %f %f %f\n", m_hTarget->pev->velocity.x, m_hTarget->pev->velocity.y, m_hTarget->pev->velocity.z);
			break;
		}
	}

	Vector vecVelAngles;

	if (m_iszFacing)
	{
		switch (m_iFaceMode)
		{
		case 0: // set angles
			vecTemp = CalcLocus_Velocity( this, m_hLocus, STRING(m_iszFacing) );
			if (vecTemp != g_vecZero) // if the vector is 0 0 0, don't use it
			{
				if (pev->spawnflags & SF_MOTION_DEBUG)
					ALERT(at_debug, "DEBUG: Set angles from %f %f %f ", m_hTarget->pev->angles.x, m_hTarget->pev->angles.y, m_hTarget->pev->angles.z);
				m_hTarget->pev->angles = UTIL_VecToAngles( vecTemp );
				if (pev->spawnflags & SF_MOTION_DEBUG)
					ALERT(at_debug, "to %f %f %f\n", m_hTarget->pev->angles.x, m_hTarget->pev->angles.y, m_hTarget->pev->angles.z);
			}
			else if (pev->spawnflags & SF_MOTION_DEBUG)
			{
				ALERT(at_debug, "Zero velocity, don't change angles\n");
			}
			break;
		case 1: // offset angles (= fake avelocity)
			vecTemp = CalcLocus_Velocity( this, m_hLocus, STRING(m_iszFacing) );
			if (vecTemp != g_vecZero) // if the vector is 0 0 0, don't use it
			{
				if (pev->spawnflags & SF_MOTION_DEBUG)
					ALERT(at_debug, "DEBUG: Offset angles from %f %f %f ", m_hTarget->pev->angles.x, m_hTarget->pev->angles.y, m_hTarget->pev->angles.z);
				m_hTarget->pev->angles = m_hTarget->pev->angles + gpGlobals->frametime * UTIL_VecToAngles( vecTemp );
				if (pev->spawnflags & SF_MOTION_DEBUG)
					ALERT(at_debug, "to %f %f %f\n", m_hTarget->pev->angles.x, m_hTarget->pev->angles.y, m_hTarget->pev->angles.z);
			}
			else if (pev->spawnflags & SF_MOTION_DEBUG)
			{
				ALERT(at_debug, "Zero velocity, don't change angles\n");
			}
			break;
		case 2: // offset angles (= fake avelocity)
			UTIL_StringToRandomVector( vecVelAngles, STRING(m_iszFacing) );
			if (pev->spawnflags & SF_MOTION_DEBUG)
				ALERT(at_debug, "DEBUG: Rotate angles from %f %f %f ", m_hTarget->pev->angles.x, m_hTarget->pev->angles.y, m_hTarget->pev->angles.z);
			m_hTarget->pev->angles = m_hTarget->pev->angles + gpGlobals->frametime * vecVelAngles;
			if (pev->spawnflags & SF_MOTION_DEBUG)
				ALERT(at_debug, "to %f %f %f\n", m_hTarget->pev->angles.x, m_hTarget->pev->angles.y, m_hTarget->pev->angles.z);
			break;
		case 3: // set avelocity
			UTIL_StringToRandomVector( vecTemp, STRING(m_iszFacing) );
			if (pev->spawnflags & SF_MOTION_DEBUG)
				ALERT(at_debug, "DEBUG: Set avelocity from %f %f %f ", m_hTarget->pev->avelocity.x, m_hTarget->pev->avelocity.y, m_hTarget->pev->avelocity.z);
			m_hTarget->pev->avelocity = vecTemp;
			if (pev->spawnflags & SF_MOTION_DEBUG)
				ALERT(at_debug, "to %f %f %f\n", m_hTarget->pev->avelocity.x, m_hTarget->pev->avelocity.y, m_hTarget->pev->avelocity.z);
			break;
		}
	}
}


LINK_ENTITY_TO_CLASS( motion_manager, CMotionManager );

TYPEDESCRIPTION	CMotionManager::m_SaveData[] = 
{
	DEFINE_FIELD( CMotionManager, m_iszPosition, FIELD_STRING ),
	DEFINE_FIELD( CMotionManager, m_iPosMode, FIELD_INTEGER ),
	DEFINE_FIELD( CMotionManager, m_iszFacing, FIELD_STRING ),
	DEFINE_FIELD( CMotionManager, m_iFaceMode, FIELD_INTEGER ),
};

IMPLEMENT_SAVERESTORE(CMotionManager,CPointEntity);

void CMotionManager::KeyValue( KeyValueData *pkvd )
{
	if (FStrEq(pkvd->szKeyName, "m_iszPosition"))
	{
		m_iszPosition = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "m_iPosMode"))
	{
		m_iPosMode = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "m_iszFacing"))
	{
		m_iszFacing = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "m_iFaceMode"))
	{
		m_iFaceMode = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else
		CPointEntity::KeyValue( pkvd );
}

void CMotionManager::Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	CBaseEntity *pTarget = pActivator;
	if (pev->target)
	{
		pTarget = UTIL_FindEntityByTargetname(NULL, STRING(pev->target), pActivator);
		if (pTarget == NULL)
			ALERT(at_error, "motion_manager \"%s\" can't find entity \"%s\" to affect\n", STRING(pev->targetname), STRING(pev->target));
		else
		{
			do
			{
				Affect( pTarget, pActivator );
				pTarget = UTIL_FindEntityByTargetname(pTarget, STRING(pev->target), pActivator);
			} while ( pTarget );
		}
	}
}

void CMotionManager::Affect( CBaseEntity *pTarget, CBaseEntity *pActivator )
{
	if (pev->spawnflags & SF_MOTION_DEBUG)
		ALERT(at_debug, "DEBUG: Creating MotionThread for %s \"%s\"\n", STRING(pTarget->pev->classname), STRING(pTarget->pev->targetname));

	CMotionThread *pThread = GetClassPtr( (CMotionThread*)NULL );
	if (pThread == NULL) return; //error?
	pThread->m_hLocus = pActivator;
	pThread->m_hTarget = pTarget;
	pThread->m_iszPosition = m_iszPosition;
	pThread->m_iPosMode = m_iPosMode;
	pThread->m_iszFacing = m_iszFacing;
	pThread->m_iFaceMode = m_iFaceMode;
	pThread->pev->spawnflags = pev->spawnflags;
	pThread->SetNextThink( 0 );
}



// this is a really bad idea.
LINK_ENTITY_TO_CLASS( trigger_changetarget, CTriggerChangeTarget );

TYPEDESCRIPTION	CTriggerChangeTarget::m_SaveData[] = 
{
	DEFINE_FIELD( CTriggerChangeTarget, m_iszNewTarget, FIELD_STRING ),
};

IMPLEMENT_SAVERESTORE(CTriggerChangeTarget,CBaseDelay);

void CTriggerChangeTarget::KeyValue( KeyValueData *pkvd )
{
	if (FStrEq(pkvd->szKeyName, "m_iszNewTarget"))
	{
		m_iszNewTarget = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else
		CBaseDelay::KeyValue( pkvd );
}

void CTriggerChangeTarget::Spawn( void )
{
}


void CTriggerChangeTarget::Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	CBaseEntity *pTarget = UTIL_FindEntityByTargetname( NULL, STRING( pev->target ), pActivator );

	if (pTarget)
	{
		if (FStrEq(STRING(m_iszNewTarget), "*locus"))
		{
			if (pActivator)
				pTarget->pev->target = pActivator->pev->targetname;
			else
				ALERT(at_error, "trigger_changetarget \"%s\" requires a locus!\n", STRING(pev->targetname));
		}
		else
			pTarget->pev->target = m_iszNewTarget;
		CBaseMonster *pMonster = pTarget->MyMonsterPointer( );
		if (pMonster)
		{
			pMonster->m_pGoalEnt = NULL;
		}
	}
}


//=====================================================
// trigger_command: activate a console command
//=====================================================
LINK_ENTITY_TO_CLASS( trigger_command, CTriggerCommand );

void CTriggerCommand::Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	char szCommand[256];

	if (pev->netname)
	{
		sprintf( szCommand, "%s\n", STRING(pev->netname) );
		SERVER_COMMAND( szCommand );
	}
}

//=========================================================
// trigger_changecvar: temporarily set a console variable
//=========================================================
#define SF_CVAR_ACTIVE  0x80000

LINK_ENTITY_TO_CLASS( trigger_changecvar, CTriggerChangeCVar );

TYPEDESCRIPTION	CTriggerChangeCVar::m_SaveData[] = 
{
	DEFINE_ARRAY( CTriggerChangeCVar, m_szStoredString, FIELD_CHARACTER, 256 ),
};

IMPLEMENT_SAVERESTORE(CTriggerChangeCVar,CBaseEntity);

void CTriggerChangeCVar::Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	char szCommand[256];

	if (!(pev->netname)) return;

	if (ShouldToggle(useType, pev->spawnflags & SF_CVAR_ACTIVE))
	{
		if (pev->spawnflags & SF_CVAR_ACTIVE)
		{
			sprintf( szCommand, "%s %s\n",  STRING(pev->netname), m_szStoredString );
			pev->spawnflags &= ~SF_CVAR_ACTIVE;
		}
		else
		{
			strncpy(m_szStoredString, CVAR_GET_STRING(STRING(pev->netname)), 256);
			sprintf( szCommand, "%s %s\n", STRING(pev->netname), STRING(pev->message) );
			pev->spawnflags |= SF_CVAR_ACTIVE;

			if (pev->sanity >= 0)
			{
				SetNextThink( pev->sanity );
			}
		}
		SERVER_COMMAND( szCommand );
	}
}

void CTriggerChangeCVar::Think( void )
{
	char szCommand[256];

	if (pev->spawnflags & SF_CVAR_ACTIVE)
	{
		sprintf( szCommand, "%s %s\n", STRING(pev->netname), m_szStoredString );
		SERVER_COMMAND( szCommand );
		pev->spawnflags &= ~SF_CVAR_ACTIVE;
	}	
}



#define SF_CAMERA_PLAYER_POSITION	1
#define SF_CAMERA_PLAYER_TARGET		2
#define SF_CAMERA_PLAYER_TAKECONTROL 4

LINK_ENTITY_TO_CLASS( trigger_camera, CTriggerCamera );

// Global Savedata for changelevel friction modifier
TYPEDESCRIPTION	CTriggerCamera::m_SaveData[] = 
{
	DEFINE_FIELD( CTriggerCamera, m_hPlayer, FIELD_EHANDLE ),
	DEFINE_FIELD( CTriggerCamera, m_hTarget, FIELD_EHANDLE ),
	DEFINE_FIELD( CTriggerCamera, m_pentPath, FIELD_CLASSPTR ),
	DEFINE_FIELD( CTriggerCamera, m_sPath, FIELD_STRING ),
	DEFINE_FIELD( CTriggerCamera, m_flWait, FIELD_FLOAT ),
	DEFINE_FIELD( CTriggerCamera, m_flReturnTime, FIELD_TIME ),
	DEFINE_FIELD( CTriggerCamera, m_flStopTime, FIELD_TIME ),
	DEFINE_FIELD( CTriggerCamera, m_moveDistance, FIELD_FLOAT ),
	DEFINE_FIELD( CTriggerCamera, m_targetSpeed, FIELD_FLOAT ),
	DEFINE_FIELD( CTriggerCamera, m_initialSpeed, FIELD_FLOAT ),
	DEFINE_FIELD( CTriggerCamera, m_acceleration, FIELD_FLOAT ),
	DEFINE_FIELD( CTriggerCamera, m_deceleration, FIELD_FLOAT ),
	DEFINE_FIELD( CTriggerCamera, m_state, FIELD_INTEGER ),
};

IMPLEMENT_SAVERESTORE(CTriggerCamera,CBaseDelay);

void CTriggerCamera::Spawn( void )
{
	pev->movetype = MOVETYPE_NOCLIP;
	pev->solid = SOLID_NOT;							// Remove model & collisions
	pev->renderamt = 0;								// The engine won't draw this model if this is set to 0 and blending is on
	pev->rendermode = kRenderTransTexture;

	m_initialSpeed = pev->speed;
	if ( m_acceleration == 0 )
		m_acceleration = 500;
	if ( m_deceleration == 0 )
		m_deceleration = 500;
}


void CTriggerCamera :: KeyValue( KeyValueData *pkvd )
{
	if (FStrEq(pkvd->szKeyName, "wait"))
	{
		m_flWait = atof(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "moveto"))
	{
		m_sPath = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "acceleration"))
	{
		m_acceleration = atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "deceleration"))
	{
		m_deceleration = atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else
		CBaseDelay::KeyValue( pkvd );
}

extern int gmsgHideWeapon;


void CTriggerCamera::Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	if ( !ShouldToggle( useType, m_state ) )
		return;

	// Toggle state
	m_state = !m_state;
	if (m_state == 0)
	{
		m_flReturnTime = gpGlobals->time;
		return;
	}
	if ( !pActivator || !pActivator->IsPlayer() )
	{
		pActivator = CBaseEntity::Instance(g_engfuncs.pfnPEntityOfEntIndex( 1 ));
	}
		
	m_hPlayer = pActivator;

	m_flReturnTime = gpGlobals->time + m_flWait;
	pev->speed = m_initialSpeed;
	m_targetSpeed = m_initialSpeed;

	if (FBitSet (pev->spawnflags, SF_CAMERA_PLAYER_TARGET ) )
	{
		m_hTarget = m_hPlayer;
	}
	else
	{
		m_hTarget = GetNextTarget();
	}

	// Nothing to look at!
	if ( m_hTarget == NULL )
	{
		return;
	}


	if (FBitSet (pev->spawnflags, SF_CAMERA_PLAYER_TAKECONTROL ) )
	{
		((CBasePlayer *)pActivator)->EnableControl(FALSE);
	}

	if ( m_sPath )
	{
		m_pentPath = UTIL_FindEntityByTargetname( NULL, STRING(m_sPath) );
	}
	else
	{
		m_pentPath = NULL;
	}

	m_flStopTime = gpGlobals->time;
	if ( m_pentPath )
	{
		if ( m_pentPath->pev->speed != 0 )
			m_targetSpeed = m_pentPath->pev->speed;
		
		m_flStopTime += m_pentPath->GetDelay();
	}

	// copy over player information
	if (FBitSet (pev->spawnflags, SF_CAMERA_PLAYER_POSITION ) )
	{
		UTIL_SetOrigin( this, pActivator->pev->origin + pActivator->pev->view_ofs );
		pev->angles.x = -pActivator->pev->angles.x;
		pev->angles.y = pActivator->pev->angles.y;
		pev->angles.z = 0;
		pev->velocity = pActivator->pev->velocity;
	}
	else
	{
		pev->velocity = Vector( 0, 0, 0 );
	}

	SET_VIEW( pActivator->edict(), edict() );

	MESSAGE_BEGIN( MSG_ONE, gmsgHideWeapon, NULL, ((CBasePlayer *)pActivator)->pev );
		WRITE_BYTE( HIDEHUD_WEAPONS );
	MESSAGE_END();

	SET_MODEL(ENT(pev), STRING(pActivator->pev->model) );

	// follow the player down
	SetThink( FollowTarget );
	SetNextThink( 0 );

	m_moveDistance = 0;
	Move();
}


void CTriggerCamera::FollowTarget( )
{
	if (m_hPlayer == NULL)
		return;

	if (m_hTarget == NULL || m_flReturnTime < gpGlobals->time)
	{
		if (m_hPlayer->IsAlive( ))
		{
			SET_VIEW( m_hPlayer->edict(), m_hPlayer->edict() );
			((CBasePlayer *)((CBaseEntity *)m_hPlayer))->EnableControl(TRUE);
		}
		SUB_UseTargets( this, USE_TOGGLE, 0 );
		pev->avelocity = Vector( 0, 0, 0 );

		MESSAGE_BEGIN( MSG_ONE, gmsgHideWeapon, NULL, ((CBasePlayer *)((CBaseEntity *)m_hPlayer))->pev );
			WRITE_BYTE( 0 );
		MESSAGE_END();
		
		m_state = 0;
		return;
	}

	Vector vecGoal = UTIL_VecToAngles( m_hTarget->pev->origin - pev->origin );
	vecGoal.x = -vecGoal.x;

	if (pev->angles.y > 360)
		pev->angles.y -= 360;

	if (pev->angles.y < 0)
		pev->angles.y += 360;

	float dx = vecGoal.x - pev->angles.x;
	float dy = vecGoal.y - pev->angles.y;

	if (dx < -180) 
		dx += 360;
	if (dx > 180) 
		dx = dx - 360;
	
	if (dy < -180) 
		dy += 360;
	if (dy > 180) 
		dy = dy - 360;

	pev->avelocity.x = dx * 40 * gpGlobals->frametime;
	pev->avelocity.y = dy * 40 * gpGlobals->frametime;


	if (!(FBitSet (pev->spawnflags, SF_CAMERA_PLAYER_TAKECONTROL)))	
	{
		pev->velocity = pev->velocity * 0.8;
		if (pev->velocity.Length( ) < 10.0) //LRC- whyyyyyy???
			pev->velocity = g_vecZero;
	}

	SetNextThink( 0 );

	Move();
}

void CTriggerCamera::Move()
{
	// Not moving on a path, return
	if (!m_pentPath)
		return;

	// Subtract movement from the previous frame
	m_moveDistance -= pev->speed * gpGlobals->frametime;

	// Have we moved enough to reach the target?
	if ( m_moveDistance <= 0 )
	{
		// Fire the passtarget if there is one
		if ( m_pentPath->pev->message )
		{
			FireTargets( STRING(m_pentPath->pev->message), this, this, USE_TOGGLE, 0 );
			if ( FBitSet( m_pentPath->pev->spawnflags, SF_CORNER_FIREONCE ) )
				m_pentPath->pev->message = 0;
		}
		// Time to go to the next target
		m_pentPath = m_pentPath->GetNextTarget();

		// Set up next corner
		if ( !m_pentPath )
		{
			pev->velocity = g_vecZero;
		}
		else 
		{
			if ( m_pentPath->pev->speed != 0 )
				m_targetSpeed = m_pentPath->pev->speed;

			Vector delta = m_pentPath->pev->origin - pev->origin;
			m_moveDistance = delta.Length();
			pev->movedir = delta.Normalize();
			// I think there is a bug here.
			float fStop = gpGlobals->time + m_pentPath->GetDelay();
			if (gpGlobals->time >= m_flStopTime) m_flStopTime = fStop;
			//m_flStopTime = gpGlobals->time + m_pentPath->GetDelay();
		}
	}

	if ( m_flStopTime > gpGlobals->time )
		pev->speed = UTIL_Approach( 0, pev->speed, m_deceleration * gpGlobals->frametime );
	else
		pev->speed = UTIL_Approach( m_targetSpeed, pev->speed, m_acceleration * gpGlobals->frametime );

	float fraction = 2 * gpGlobals->frametime;
	pev->velocity = ((pev->movedir * pev->speed) * fraction) + (pev->velocity * (1-fraction));
}

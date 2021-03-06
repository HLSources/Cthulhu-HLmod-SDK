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
//=========================================================
// GameRules.cpp
//=========================================================

#include	"extdll.h"
#include	"util.h"
#include	"cbase.h"
#include	"player.h"
#include	"weapons.h"
#include	"gamerules.h"
#include	"teamplay_gamerules.h"
#include	"skill.h"
#include	"game.h"

extern edict_t *EntSelectSpawnPoint( CBaseEntity *pPlayer );

DLL_GLOBAL CGameRules*	g_pGameRules = NULL;
extern DLL_GLOBAL BOOL	g_fGameOver;
extern int gmsgDeathMsg;	// client dll messages
extern int gmsgMOTD;

int g_teamplay = 0;

//=========================================================
//=========================================================
BOOL CGameRules::CanHaveAmmo( CBasePlayer *pPlayer, const char *pszAmmoName, int iMaxCarry )
{
	int iAmmoIndex;

	if ( pszAmmoName )
	{
		iAmmoIndex = pPlayer->GetAmmoIndex( pszAmmoName );

		if ( iAmmoIndex > -1 )
		{
			if ( pPlayer->AmmoInventory( iAmmoIndex ) < iMaxCarry )
			{
				// player has room for more of this type of ammo
				return TRUE;
			}
		}
	}

	return FALSE;
}

//=========================================================
//=========================================================
edict_t *CGameRules :: GetPlayerSpawnSpot( CBasePlayer *pPlayer )
{
	edict_t *pentSpawnSpot = EntSelectSpawnPoint( pPlayer );

	pPlayer->pev->origin = VARS(pentSpawnSpot)->origin + Vector(0,0,1);
	pPlayer->pev->v_angle  = g_vecZero;
	pPlayer->pev->velocity = g_vecZero;
	pPlayer->pev->angles = VARS(pentSpawnSpot)->angles;
	pPlayer->pev->punchangle = g_vecZero;
	pPlayer->pev->fixangle = TRUE;
	
	return pentSpawnSpot;
}

//=========================================================
//=========================================================
BOOL CGameRules::CanHavePlayerItem( CBasePlayer *pPlayer, CBasePlayerItem *pWeapon )
{
	// only living players can have items
	if ( pPlayer->pev->deadflag != DEAD_NO )
		return FALSE;

	if ( pWeapon->pszAmmo1() )
	{
		if ( !CanHaveAmmo( pPlayer, pWeapon->pszAmmo1(), pWeapon->iMaxAmmo1() ) )
		{
			// we can't carry anymore ammo for this gun. We can only 
			// have the gun if we aren't already carrying one of this type
			if ( pPlayer->HasPlayerItem( pWeapon ) )
			{
				return FALSE;
			}
		}
	}
	else
	{
		// weapon doesn't use ammo, don't take another if you already have it.
		if ( pPlayer->HasPlayerItem( pWeapon ) )
		{
			return FALSE;
		}
	}

	// note: will fall through to here if GetItemInfo doesn't fill the struct!
	return TRUE;
}

//=========================================================
// load the SkillData struct with the proper values based on the skill level.
//=========================================================
void CGameRules::RefreshSkillData ( void )
{
	int	iSkill;

	iSkill = (int)CVAR_GET_FLOAT("skill");
	g_iSkillLevel = iSkill;

	if ( iSkill < 1 )
	{
		iSkill = 1;
	}
	else if ( iSkill > 3 )
	{
		iSkill = 3; 
	}

	gSkillData.iSkillLevel = iSkill;

	ALERT ( at_debug, "\nGAME SKILL LEVEL:%d\n",iSkill );

	// how long monsters flee from the Dread Name
	gSkillData.panicDuration = GetSkillCvar("sk_panic_duration");

	//Agrunt		
	gSkillData.agruntHealth = GetSkillCvar( "sk_agrunt_health" );
	gSkillData.agruntDmgPunch = GetSkillCvar( "sk_agrunt_dmg_punch");

	// Apache 
	gSkillData.apacheHealth = GetSkillCvar( "sk_apache_health");

	// Barney
	gSkillData.barneyHealth = GetSkillCvar( "sk_barney_health");

	// Big Momma
	gSkillData.bigmommaHealthFactor = GetSkillCvar( "sk_bigmomma_health_factor" );
	gSkillData.bigmommaDmgSlash = GetSkillCvar( "sk_bigmomma_dmg_slash" );
	gSkillData.bigmommaDmgBlast = GetSkillCvar( "sk_bigmomma_dmg_blast" );
	gSkillData.bigmommaRadiusBlast = GetSkillCvar( "sk_bigmomma_radius_blast" );

	// Cthonian
	gSkillData.cthonianHealth = GetSkillCvar( "sk_cthonian_health");
	gSkillData.cthonianDmgBite = GetSkillCvar( "sk_cthonian_dmg_bite");
	gSkillData.cthonianDmgWhip = GetSkillCvar( "sk_cthonian_dmg_whip");
	gSkillData.cthonianDmgSpit = GetSkillCvar( "sk_cthonian_dmg_spit");

	// Bullsquid
	gSkillData.bullsquidHealth = GetSkillCvar( "sk_bullsquid_health");
	gSkillData.bullsquidDmgBite = GetSkillCvar( "sk_bullsquid_dmg_bite");
	gSkillData.bullsquidDmgWhip = GetSkillCvar( "sk_bullsquid_dmg_whip");
	gSkillData.bullsquidDmgSpit = GetSkillCvar( "sk_bullsquid_dmg_spit");

	// Gargantua
	gSkillData.gargantuaHealth = GetSkillCvar( "sk_gargantua_health");
	gSkillData.gargantuaDmgSlash = GetSkillCvar( "sk_gargantua_dmg_slash");
	gSkillData.gargantuaDmgFire = GetSkillCvar( "sk_gargantua_dmg_fire");
	gSkillData.gargantuaDmgStomp = GetSkillCvar( "sk_gargantua_dmg_stomp");

	// Hassassin
	gSkillData.hassassinHealth = GetSkillCvar( "sk_hassassin_health");

	// Headcrab
	gSkillData.headcrabHealth = GetSkillCvar( "sk_headcrab_health");
	gSkillData.headcrabDmgBite = GetSkillCvar( "sk_headcrab_dmg_bite");

	// Gangster 
	gSkillData.gangsterHealth = GetSkillCvar( "sk_gangster_health");
	gSkillData.gangsterDmgKick = GetSkillCvar( "sk_gangster_kick");
	gSkillData.gangsterShotgunPellets = GetSkillCvar( "sk_gangster_pellets");

	// Cultist 
	gSkillData.cultistHealth = GetSkillCvar( "sk_cultist_health");
	gSkillData.cultistDmgKick = GetSkillCvar( "sk_cultist_kick");
	gSkillData.cultistShotgunPellets = GetSkillCvar( "sk_cultist_pellets");

	// Hgrunt 
	gSkillData.hgruntHealth = GetSkillCvar( "sk_hgrunt_health");
	gSkillData.hgruntDmgKick = GetSkillCvar( "sk_hgrunt_kick");
	gSkillData.hgruntShotgunPellets = GetSkillCvar( "sk_hgrunt_pellets");
	gSkillData.hgruntGrenadeSpeed = GetSkillCvar( "sk_hgrunt_gspeed");

	// Houndeye
	gSkillData.houndeyeHealth = GetSkillCvar( "sk_houndeye_health");
	gSkillData.houndeyeDmgBlast = GetSkillCvar( "sk_houndeye_dmg_blast");

	// Great Race
	gSkillData.greatraceHealth = GetSkillCvar( "sk_great_race_health");
	gSkillData.greatraceDmgClaw = GetSkillCvar( "sk_great_race_dmg_claw");
	gSkillData.greatraceDmgClawrake = GetSkillCvar( "sk_great_race_dmg_clawrake");
	gSkillData.greatraceDmgZap = GetSkillCvar( "sk_great_race_dmg_zap");

	// Yodan
	gSkillData.yodanHealth = GetSkillCvar( "sk_yodan_health");
	gSkillData.yodanDmgClaw = GetSkillCvar( "sk_yodan_dmg_claw");
	gSkillData.yodanDmgClawrake = GetSkillCvar( "sk_yodan_dmg_clawrake");
	gSkillData.yodanDmgZap = GetSkillCvar( "sk_yodan_dmg_zap");

	// Serpent Men
	gSkillData.serpentmanHealth = GetSkillCvar( "sk_serpent_man_health");
	gSkillData.serpentmanDmgStaff = GetSkillCvar( "sk_serpent_man_dmg_staff");

	// Priest
	gSkillData.priestHealth = GetSkillCvar( "sk_priest_health");
	gSkillData.priestDmgKnife = GetSkillCvar( "sk_priest_dmg_knife");

	// ISlave
	gSkillData.slaveHealth = GetSkillCvar( "sk_islave_health");
	gSkillData.slaveDmgClaw = GetSkillCvar( "sk_islave_dmg_claw");
	gSkillData.slaveDmgClawrake = GetSkillCvar( "sk_islave_dmg_clawrake");
	gSkillData.slaveDmgZap = GetSkillCvar( "sk_islave_dmg_zap");

	// Icthyosaur
	gSkillData.ichthyosaurHealth = GetSkillCvar( "sk_ichthyosaur_health");
	gSkillData.ichthyosaurDmgShake = GetSkillCvar( "sk_ichthyosaur_shake");

	// Hunting Horror
	gSkillData.huntinghorrorHealth = GetSkillCvar( "sk_huntinghorror_health");
	gSkillData.huntinghorrorDmgBite = GetSkillCvar( "sk_huntinghorror_bite");

	// Leech
	gSkillData.leechHealth = GetSkillCvar( "sk_leech_health");

	gSkillData.leechDmgBite = GetSkillCvar( "sk_leech_dmg_bite");

	// Controller
	gSkillData.controllerHealth = GetSkillCvar( "sk_controller_health");
	gSkillData.controllerDmgZap = GetSkillCvar( "sk_controller_dmgzap");
	gSkillData.controllerSpeedBall = GetSkillCvar( "sk_controller_speedball");
	gSkillData.controllerDmgBall = GetSkillCvar( "sk_controller_dmgball");

	// Nihilanth
	gSkillData.nihilanthHealth = GetSkillCvar( "sk_nihilanth_health");
	gSkillData.nihilanthZap = GetSkillCvar( "sk_nihilanth_zap");

	// Scientist
	gSkillData.scientistHealth = GetSkillCvar( "sk_scientist_health");

	// Scientist
	gSkillData.butlerHealth = GetSkillCvar( "sk_butler_health");

	// Sir Henry
	gSkillData.sirhenryHealth = GetSkillCvar( "sk_sirhenry_health");
	gSkillData.sirhenryDmgZap = GetSkillCvar( "sk_sirhenry_dmg_zap");
	gSkillData.sirhenryDmgKnife = GetSkillCvar( "sk_sirhenry_dmg_knife");

	// Snark
	gSkillData.snarkHealth = GetSkillCvar( "sk_snark_health");
	gSkillData.snarkDmgBite = GetSkillCvar( "sk_snark_dmg_bite");
	gSkillData.snarkDmgPop = GetSkillCvar( "sk_snark_dmg_pop");

	// Formless Spawn
	gSkillData.formless_spawnHealth = GetSkillCvar( "sk_formless_spawn_health");
	gSkillData.formless_spawnDmgAttack = GetSkillCvar( "sk_formless_spawn_dmg_attack");

	// Ghoul
	gSkillData.ghoulHealth = GetSkillCvar( "sk_ghoul_health");
	gSkillData.ghoulDmgOneSlash = GetSkillCvar( "sk_ghoul_dmg_one_slash");
	gSkillData.ghoulDmgBothSlash = GetSkillCvar( "sk_ghoul_dmg_both_slash");

	// Night Gaunt
	gSkillData.nightgauntHealth = GetSkillCvar( "sk_nightgaunt_health");
	gSkillData.nightgauntDmgSlash = GetSkillCvar( "sk_nightgaunt_dmg_slash");

	// Snake
	gSkillData.snakeHealth = GetSkillCvar( "sk_snake_health");
	gSkillData.snakeDmgBite = GetSkillCvar( "sk_snake_dmg_bite");

	// DeepOne
	gSkillData.deeponeHealth = GetSkillCvar( "sk_deep_one_health");
	gSkillData.deeponeDmgOneSlash = GetSkillCvar( "sk_deep_one_dmg_one_slash");
	gSkillData.deeponeDmgBothSlash = GetSkillCvar( "sk_deep_one_dmg_both_slash");

	// Dimensional Shambler
	gSkillData.shamblerHealth = GetSkillCvar( "sk_shambler_health");
	gSkillData.shamblerDmgOneSlash = GetSkillCvar( "sk_shambler_dmg_one_slash");
	gSkillData.shamblerDmgBothSlash = GetSkillCvar( "sk_shambler_dmg_both_slash");

	// Zombie
	gSkillData.zombieHealth = GetSkillCvar( "sk_zombie_health");
	gSkillData.zombieDmgOneSlash = GetSkillCvar( "sk_zombie_dmg_one_slash");
	gSkillData.zombieDmgBothSlash = GetSkillCvar( "sk_zombie_dmg_both_slash");

	//Turret
	gSkillData.turretHealth = GetSkillCvar( "sk_turret_health");

	// MiniTurret
	gSkillData.miniturretHealth = GetSkillCvar( "sk_miniturret_health");
	
	// Sentry Turret
	gSkillData.sentryHealth = GetSkillCvar( "sk_sentry_health");

// PLAYER WEAPONS

	gSkillData.plrDmgSwordCane = GetSkillCvar( "sk_plr_swordcane");
	gSkillData.plrDmgKnife = GetSkillCvar( "sk_plr_knife");
	gSkillData.plrDmgRevolver = GetSkillCvar( "sk_plr_revolver");
	gSkillData.plrDmgShotgun = GetSkillCvar( "sk_plr_shotgun");
	gSkillData.plrDmgTommyGun = GetSkillCvar( "sk_plr_tommygun");
	gSkillData.plrDmgRifle = GetSkillCvar( "sk_plr_rifle");
	gSkillData.plrDmgDynamite = GetSkillCvar( "sk_plr_dynamite");
	gSkillData.plrDmgMolotov = GetSkillCvar( "sk_plr_molotov");
	gSkillData.plrDmgLightningGun = GetSkillCvar( "sk_plr_lightning");
	gSkillData.plrDmgShrivellingNarrow = GetSkillCvar( "sk_plr_shrivelling_narrow");
	gSkillData.plrDmgShrivellingWide = GetSkillCvar( "sk_plr_shrivelling_wide");
	gSkillData.plrDmgDrainLife = GetSkillCvar( "sk_plr_drainlife");

	// OLD WEAPONS

	// Crowbar whack
	//gSkillData.plrDmgCrowbar = GetSkillCvar( "sk_plr_crowbar");

	// Glock Round
	//gSkillData.plrDmg9MM = GetSkillCvar( "sk_plr_9mm_bullet");

	// 357 Round
	//gSkillData.plrDmg357 = GetSkillCvar( "sk_plr_357_bullet");

	// MP5 Round
	//gSkillData.plrDmgMP5 = GetSkillCvar( "sk_plr_9mmAR_bullet");

	// M203 grenade
	//gSkillData.plrDmgM203Grenade = GetSkillCvar( "sk_plr_9mmAR_grenade");

	// Crossbow
	//gSkillData.plrDmgCrossbowClient = GetSkillCvar( "sk_plr_xbow_bolt_client");
	//gSkillData.plrDmgCrossbowMonster = GetSkillCvar( "sk_plr_xbow_bolt_monster");

	// RPG
	//gSkillData.plrDmgRPG = GetSkillCvar( "sk_plr_rpg");

	// Gauss gun
	//gSkillData.plrDmgGauss = GetSkillCvar( "sk_plr_gauss");

	// Egon Gun
	//gSkillData.plrDmgEgonNarrow = GetSkillCvar( "sk_plr_egon_narrow");
	//gSkillData.plrDmgEgonWide = GetSkillCvar( "sk_plr_egon_wide");

	// Hand Grendade
	//gSkillData.plrDmgHandGrenade = GetSkillCvar( "sk_plr_hand_grenade");

	// Satchel Charge
	//gSkillData.plrDmgSatchel = GetSkillCvar( "sk_plr_satchel");

	// Tripmine
	//gSkillData.plrDmgTripmine = GetSkillCvar( "sk_plr_tripmine");

	// MONSTER WEAPONS
	gSkillData.monDmg12MM = GetSkillCvar( "sk_12mm_bullet");
	gSkillData.monDmgMP5 = GetSkillCvar ("sk_9mmAR_bullet" );
	gSkillData.monDmg9MM = GetSkillCvar( "sk_9mm_bullet");

	// MONSTER HORNET
	gSkillData.monDmgHornet = GetSkillCvar( "sk_hornet_dmg");

	// PLAYER HORNET
// Up to this point, player hornet damage and monster hornet damage were both using
// monDmgHornet to determine how much damage to do. In tuning the hivehand, we now need
// to separate player damage and monster hivehand damage. Since it's so late in the project, we've
// added plrDmgHornet to the SKILLDATA struct, but not to the engine CVar list, so it's inaccesible
// via SKILLS.CFG. Any player hivehand tuning must take place in the code. (sjb)
	gSkillData.plrDmgHornet = 7;

	gSkillData.plrDmgMolotov = GetSkillCvar( "sk_plr_molotov");


	// HEALTH/CHARGE
//	gSkillData.suitchargerCapacity = GetSkillCvar( "sk_suitcharger" );
//	gSkillData.batteryCapacity = GetSkillCvar( "sk_battery" );
	gSkillData.healthchargerCapacity = GetSkillCvar ( "sk_healthcharger" );
	gSkillData.healthkitCapacity = GetSkillCvar ( "sk_healthkit" );
	gSkillData.scientistHeal = GetSkillCvar ( "sk_scientist_heal" );

	// monster damage adj
	gSkillData.monHead = GetSkillCvar( "sk_monster_head" );
	gSkillData.monChest = GetSkillCvar( "sk_monster_chest" );
	gSkillData.monStomach = GetSkillCvar( "sk_monster_stomach" );
	gSkillData.monLeg = GetSkillCvar( "sk_monster_leg" );
	gSkillData.monArm = GetSkillCvar( "sk_monster_arm" );

	// player damage adj
	gSkillData.plrHead = GetSkillCvar( "sk_player_head" );
	gSkillData.plrChest = GetSkillCvar( "sk_player_chest" );
	gSkillData.plrStomach = GetSkillCvar( "sk_player_stomach" );
	gSkillData.plrLeg = GetSkillCvar( "sk_player_leg" );
	gSkillData.plrArm = GetSkillCvar( "sk_player_arm" );
}

//=========================================================
// instantiate the proper game rules object
//=========================================================

CGameRules *InstallGameRules( void )
{
	SERVER_COMMAND( "exec game.cfg\n" );
	SERVER_EXECUTE( );

	if ( !gpGlobals->deathmatch )
	{
		// generic half-life
		g_teamplay = 0;
		return new CHalfLifeRules;
	}
	else
	{
		if ( teamplay.value > 0 )
		{
			// teamplay

			g_teamplay = 1;
			return new CHalfLifeTeamplay;
		}
		if ((int)gpGlobals->deathmatch == 1)
		{
			// vanilla deathmatch
			g_teamplay = 0;
			return new CHalfLifeMultiplay;
		}
		else
		{
			// vanilla deathmatch??
			g_teamplay = 0;
			return new CHalfLifeMultiplay;
		}
	}
}




/*
*
*    This program is free software; you can redistribute it and/or modify it
*    under the terms of the GNU General Public License as published by the
*    Free Software Foundation; either version 2 of the License, or (at
*    your option) any later version.
*
*    This program is distributed in the hope that it will be useful, but
*    WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
*    General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program; if not, write to the Free Software Foundation,
*    Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*
*    In addition, as a special exception, the author gives permission to
*    link the code of this program with the Half-Life Game Engine ("HL
*    Engine") and Modified Game Libraries ("MODs") developed by Valve,
*    L.L.C ("Valve").  You must obey the GNU General Public License in all
*    respects for all of the code used other than the HL Engine and MODs
*    from Valve.  If you modify this file, you may extend this exception
*    to your version of the file, but you are not obligated to do so.  If
*    you do not wish to do so, delete this exception statement from your
*    version.
*
*/

#include "precompiled.h"
#include "weapon_accuracy.h"

// Master switch. Disabled by default so vanilla behaviour (and the demo
// determinism tests) stay untouched.
cvar_t mp_accuracy_mode = { "mp_accuracy_mode", "0", FCVAR_SERVER, 0.0f, nullptr };

// The first bullet of a burst flies exactly where the crosshair points when
// the player is standing, crouching or walk-moving (like CS2).
cvar_t mp_accuracy_first_shot = { "mp_accuracy_first_shot", "1", FCVAR_SERVER, 0.0f, nullptr };

// Spread multipliers per stance.
cvar_t mp_accuracy_still      = { "mp_accuracy_still", "0.02", FCVAR_SERVER, 0.0f, nullptr };
cvar_t mp_accuracy_walk       = { "mp_accuracy_walk", "0.0", FCVAR_SERVER, 0.0f, nullptr };	// crouch / +speed
cvar_t mp_accuracy_move       = { "mp_accuracy_move", "0.15", FCVAR_SERVER, 0.0f, nullptr };
cvar_t mp_accuracy_air        = { "mp_accuracy_air", "1.0", FCVAR_SERVER, 0.0f, nullptr };
cvar_t mp_accuracy_still_speed = { "mp_accuracy_still_speed", "30", FCVAR_SERVER, 0.0f, nullptr };

// Recoil (punchangle) multipliers per stance.
cvar_t mp_recoil_still        = { "mp_recoil_still", "0.05", FCVAR_SERVER, 0.0f, nullptr };
cvar_t mp_recoil_walk         = { "mp_recoil_walk", "0.0", FCVAR_SERVER, 0.0f, nullptr };	// crouch / +speed
cvar_t mp_recoil_move         = { "mp_recoil_move", "0.1", FCVAR_SERVER, 0.0f, nullptr };
cvar_t mp_recoil_air          = { "mp_recoil_air", "1.0", FCVAR_SERVER, 0.0f, nullptr };	// keeps the vanilla vertical kick
cvar_t mp_recoil_air_lateral  = { "mp_recoil_air_lateral", "0.0", FCVAR_SERVER, 0.0f, nullptr };	// no sideways screen shake

// Extra ground friction when movement keys are pressed against the current
// velocity (counter-strafe). 1.0 disables the assist.
cvar_t mp_counterstrafe_friction = { "mp_counterstrafe_friction", "2.0", FCVAR_SERVER, 0.0f, nullptr };

enum AccuracyStance
{
	STANCE_STILL,
	STANCE_MOVE,
	STANCE_CROUCH_WALK,
	STANCE_AIR,
};

void WeaponAccuracy_RegisterCvars()
{
	CVAR_REGISTER(&mp_accuracy_mode);
	CVAR_REGISTER(&mp_accuracy_first_shot);
	CVAR_REGISTER(&mp_accuracy_still);
	CVAR_REGISTER(&mp_accuracy_walk);
	CVAR_REGISTER(&mp_accuracy_move);
	CVAR_REGISTER(&mp_accuracy_air);
	CVAR_REGISTER(&mp_accuracy_still_speed);
	CVAR_REGISTER(&mp_recoil_still);
	CVAR_REGISTER(&mp_recoil_walk);
	CVAR_REGISTER(&mp_recoil_move);
	CVAR_REGISTER(&mp_recoil_air);
	CVAR_REGISTER(&mp_recoil_air_lateral);
	CVAR_REGISTER(&mp_counterstrafe_friction);
}

bool WeaponAccuracy_IsEnabled()
{
	return mp_accuracy_mode.value != 0.0f;
}

float WeaponAccuracy_GetStillSpeed()
{
	return mp_accuracy_still_speed.value;
}

float WeaponAccuracy_GetCounterStrafeFriction()
{
	if (!WeaponAccuracy_IsEnabled())
		return 1.0f;

	return (mp_counterstrafe_friction.value > 1.0f) ? mp_counterstrafe_friction.value : 1.0f;
}

static AccuracyStance GetStance(CBasePlayer *pPlayer, float &flSpeed)
{
	flSpeed = pPlayer->pev->velocity.Length2D();

	if (!(pPlayer->pev->flags & FL_ONGROUND))
		return STANCE_AIR;

	// +speed (walk) is the same as crouching for accuracy purposes.
	if ((pPlayer->pev->flags & FL_DUCKING) || (pPlayer->pev->button & (IN_DUCK | IN_RUN)))
		return STANCE_CROUCH_WALK;

	if (flSpeed > mp_accuracy_still_speed.value)
		return STANCE_MOVE;

	return STANCE_STILL;
}

// The sniper rifles below never update m_iShotsFired, so they cannot use the
// "first shot" rule. Their standing spread is already negligible anyway.
static bool IsShotCountingWeapon(WeaponIdType weaponId)
{
	switch (weaponId)
	{
	case WEAPON_AWP:
	case WEAPON_SCOUT:
	case WEAPON_G3SG1:
	case WEAPON_SG550:
		return false;
	default:
		return true;
	}
}

float WeaponAccuracy_GetSpreadScale(CBasePlayer *pPlayer, CBasePlayerWeapon *pWeapon, bool bAllowFirstShotPerfect)
{
	if (!WeaponAccuracy_IsEnabled() || !pPlayer || !pWeapon)
		return 1.0f;

	float flSpeed;
	AccuracyStance stance = GetStance(pPlayer, flSpeed);

	float flScale;

	switch (stance)
	{
	case STANCE_AIR:
		flScale = mp_accuracy_air.value;
		break;
	case STANCE_CROUCH_WALK:
		flScale = mp_accuracy_walk.value;
		break;
	case STANCE_MOVE:
		flScale = mp_accuracy_move.value;
		break;
	default:
		flScale = mp_accuracy_still.value;
		break;
	}

	if (bAllowFirstShotPerfect
		&& mp_accuracy_first_shot.value != 0.0f
		&& stance != STANCE_AIR
		&& stance != STANCE_MOVE
		&& IsShotCountingWeapon((WeaponIdType)pWeapon->m_iId)
		&& pWeapon->m_iShotsFired <= 1)
	{
		flScale = 0.0f;

#ifdef REGAMEDLL_ADD
		ALERT(at_console, "WeaponAccuracy: first shot perfect (stance=%d, shotsFired=%d, speed=%.1f)\n",
			stance, pWeapon->m_iShotsFired, flSpeed);
#endif
	}

	return Q_max(flScale, 0.0f);
}

void WeaponAccuracy_GetRecoilScale(CBasePlayer *pPlayer, CBasePlayerWeapon *pWeapon, float &flVerticalScale, float &flLateralScale)
{
	flVerticalScale = 1.0f;
	flLateralScale = 1.0f;

	if (!WeaponAccuracy_IsEnabled() || !pPlayer || !pWeapon)
		return;

	float flSpeed;
	AccuracyStance stance = GetStance(pPlayer, flSpeed);

	switch (stance)
	{
	case STANCE_AIR:
		flVerticalScale = mp_recoil_air.value;
		flLateralScale = mp_recoil_air_lateral.value;
		break;
	case STANCE_CROUCH_WALK:
		flVerticalScale = mp_recoil_walk.value;
		flLateralScale = mp_recoil_walk.value;
		break;
	case STANCE_MOVE:
		flVerticalScale = mp_recoil_move.value;
		flLateralScale = mp_recoil_move.value;
		break;
	default:
		flVerticalScale = mp_recoil_still.value;
		flLateralScale = mp_recoil_still.value;
		break;
	}

	flVerticalScale = Q_max(flVerticalScale, 0.0f);
	flLateralScale = Q_max(flLateralScale, 0.0f);
}

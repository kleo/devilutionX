/**
 * @file missiles.cpp
 *
 * Implementation of missile functionality.
 */
#include "missiles.h"

#include <climits>

#include "control.h"
#include "controls/plrctrls.h"
#include "cursor.h"
#include "dead.h"
#ifdef _DEBUG
#include "debug.h"
#endif
#include "engine/cel_header.hpp"
#include "engine/load_file.hpp"
#include "engine/random.hpp"
#include "init.h"
#include "inv.h"
#include "lighting.h"
#include "monster.h"
#include "spells.h"
#include "trigs.h"

namespace devilution {

std::list<Missile> Missiles;
bool MissilePreFlag;

namespace {

int AddClassHealingBonus(int hp, HeroClass heroClass)
{
	switch (heroClass) {
	case HeroClass::Warrior:
	case HeroClass::Monk:
	case HeroClass::Barbarian:
		return hp * 2;
	case HeroClass::Rogue:
	case HeroClass::Bard:
		return hp + hp / 2;
	default:
		return hp;
	}
}

int ScaleSpellEffect(int base, int spellLevel)
{
	for (int i = 0; i < spellLevel; i++) {
		base += base / 8;
	}

	return base;
}

int GenerateRndSum(int range, int iterations)
{
	int value = 0;
	for (int i = 0; i < iterations; i++) {
		value += GenerateRnd(range);
	}

	return value;
}

bool CheckBlock(Point from, Point to)
{
	while (from != to) {
		from += GetDirection(from, to);
		if (nSolidTable[dPiece[from.x][from.y]])
			return true;
	}

	return false;
}

Monster *FindClosest(Point source, int rad)
{
	std::optional<Point> monsterPosition = FindClosestValidPosition(
	    [&source](Point target) {
		    // search for a monster with clear line of sight
		    return InDungeonBounds(target) && dMonster[target.x][target.y] > 0 && !CheckBlock(source, target);
	    },
	    source, 1, rad);

	if (monsterPosition) {
		int mid = dMonster[monsterPosition->x][monsterPosition->y];
		return &Monsters[mid - 1];
	}

	return nullptr;
}

constexpr Direction16 Direction16Flip(Direction16 x, Direction16 pivot)
{
	std::underlying_type_t<Direction16> ret = (2 * static_cast<std::underlying_type_t<Direction16>>(pivot) + 16 - static_cast<std::underlying_type_t<Direction16>>(x)) % 16;

	return static_cast<Direction16>(ret);
}

void UpdateMissileVelocity(Missile &missile, Point destination, int velocityInPixels)
{
	missile.position.velocity = { 0, 0 };

	if (missile.position.tile == destination)
		return;

	// Get the normalized vector in isometric projection
	Displacement fixed16NormalVector = (missile.position.tile - destination).worldToNormalScreen();

	// Multiplying by the target velocity gives us a scaled velocity vector.
	missile.position.velocity = fixed16NormalVector * velocityInPixels;
}

/**
 * @brief Add the missile to the lookup tables
 * @param i Missiles index
 */
void PutMissile(Missile &missile)
{
	Point position = missile.position.tile;

	if (!InDungeonBounds(position))
		missile._miDelFlag = true;

	if (missile._miDelFlag) {
		return;
	}

	dFlags[position.x][position.y] |= DungeonFlag::Missile;

	if (missile._miPreFlag)
		MissilePreFlag = true;
}

void UpdateMissilePos(Missile &missile)
{
	Displacement pixelsTravelled = missile.position.traveled >> 16;

	Displacement tileOffset = pixelsTravelled.screenToMissile();
	missile.position.tile = missile.position.start + tileOffset;

	missile.position.offset = pixelsTravelled + tileOffset.worldToScreen();

	Displacement absoluteLightOffset = pixelsTravelled.screenToLight();
	ChangeLightOffset(missile._mlid, absoluteLightOffset - tileOffset * 8);
}

/**
 * @brief Dodgy hack used to correct the position for charging monsters.
 *
 * If the monster represented by this missile is *not* facing north in some way it gets shifted to the south.
 * This appears to compensate for some visual oddity or invalid calculation earlier in the MI_Rhino logic.
 * @param missile MissileStruct representing a charging monster.
 */
void MoveMissilePos(Missile &missile)
{
	Direction moveDirection;

	switch (static_cast<Direction>(missile._mimfnum)) {
	case Direction::East:
		moveDirection = Direction::SouthEast;
		break;
	case Direction::West:
		moveDirection = Direction::SouthWest;
		break;
	case Direction::South:
	case Direction::SouthWest:
	case Direction::SouthEast:
		moveDirection = Direction::South;
		break;
	default:
		return;
	}

	auto target = missile.position.tile + moveDirection;
	if (IsTileAvailable(Monsters[missile._misource], target)) {
		missile.position.tile = target;
		missile.position.offset += Displacement(moveDirection).worldToScreen();
	}
}

bool MonsterMHit(int pnum, int m, int mindam, int maxdam, int dist, missile_id t, bool shift)
{
	auto &monster = Monsters[m];

	if (!monster.IsPossibleToHit() || monster.IsImmune(t))
		return false;

	int hit = GenerateRnd(100);
	int hper = 0;
	if (pnum != -1) {
		const Player &player = Players[pnum];
		if (MissilesData[t].mType == 0) {
			hper = player.GetRangedPiercingToHit();
			hper -= player.CalculateArmorPierce(monster.mArmorClass, false);
			hper -= (dist * dist) / 2;
		} else {
			hper = player.GetMagicToHit() - (monster.mLevel * 2) - dist;
		}
	} else {
		hper = GenerateRnd(75) - monster.mLevel * 2;
	}

	hper = clamp(hper, 5, 95);

	if (monster._mmode == MonsterMode::Petrified)
		hit = 0;

	if (monster.TryLiftGargoyle())
		return true;

	if (hit >= hper) {
#ifdef _DEBUG
		if (!DebugGodMode)
#endif
			return false;
	}

	int dam;
	if (t == MIS_BONESPIRIT) {
		dam = monster._mhitpoints / 3 >> 6;
	} else {
		dam = mindam + GenerateRnd(maxdam - mindam + 1);
	}

	const Player &player = Players[pnum];

	if (MissilesData[t].mType == 0 && MissilesData[t].mResist == MISR_NONE) {
		dam = player._pIBonusDamMod + dam * player._pIBonusDam / 100 + dam;
		if (player._pClass == HeroClass::Rogue)
			dam += player._pDamageMod;
		else
			dam += player._pDamageMod / 2;
		if (monster.MData->mMonstClass == MonsterClass::Demon && HasAnyOf(player._pIFlags, ItemSpecialEffect::TripleDemonDamage))
			dam *= 3;
	}
	bool resist = monster.IsResistant(t);
	if (!shift)
		dam <<= 6;
	if (resist)
		dam >>= 2;

	if (pnum == MyPlayerId)
		monster._mhitpoints -= dam;

	if ((gbIsHellfire && HasAnyOf(player._pIFlags, ItemSpecialEffect::NoHealOnMonsters)) || (!gbIsHellfire && HasAnyOf(player._pIFlags, ItemSpecialEffect::FireArrows)))
		monster._mFlags |= MFLAG_NOHEAL;

	if (monster._mhitpoints >> 6 <= 0) {
		M_StartKill(m, pnum);
	} else if (resist) {
		PlayEffect(monster, 1);
	} else {
		if (monster._mmode != MonsterMode::Petrified && MissilesData[t].mType == 0 && HasAnyOf(player._pIFlags, ItemSpecialEffect::Knockback))
			M_GetKnockback(m);
		if (monster.MType->mtype != MT_GOLEM)
			M_StartHit(m, pnum, dam);
	}

	if (monster._msquelch == 0) {
		monster._msquelch = UINT8_MAX;
		monster.position.last = player.position.tile;
	}

	return true;
}

bool Plr2PlrMHit(int pnum, int p, int mindam, int maxdam, int dist, missile_id mtype, bool shift, bool *blocked)
{
	Player &player = Players[pnum];
	auto &target = Players[p];

	if (sgGameInitInfo.bFriendlyFire == 0 && player.friendlyMode)
		return false;

	*blocked = false;

	if (target._pInvincible) {
		return false;
	}

	if (mtype == MIS_HBOLT) {
		return false;
	}

	if (HasAnyOf(target._pSpellFlags, SpellFlag::Etherealize) && MissilesData[mtype].mType == 0) {
		return false;
	}

	int8_t resper;
	switch (MissilesData[mtype].mResist) {
	case MISR_FIRE:
		resper = target._pFireResist;
		break;
	case MISR_LIGHTNING:
		resper = target._pLghtResist;
		break;
	case MISR_MAGIC:
	case MISR_ACID:
		resper = target._pMagResist;
		break;
	default:
		resper = 0;
		break;
	}

	int hper = GenerateRnd(100);

	int hit;
	if (MissilesData[mtype].mType == 0) {
		hit = player.GetRangedToHit()
		    - (dist * dist / 2)
		    - target.GetArmor();
	} else {
		hit = player.GetMagicToHit()
		    - (target._pLevel * 2)
		    - dist;
	}

	hit = clamp(hit, 5, 95);

	if (hper >= hit) {
		return false;
	}

	int blkper = 100;
	if (!shift && (target._pmode == PM_STAND || target._pmode == PM_ATTACK) && target._pBlockFlag) {
		blkper = GenerateRnd(100);
	}

	int blk = target.GetBlockChance() - (player._pLevel * 2);
	blk = clamp(blk, 0, 100);

	int dam;
	if (mtype == MIS_BONESPIRIT) {
		dam = target._pHitPoints / 3;
	} else {
		dam = mindam + GenerateRnd(maxdam - mindam + 1);
		if (MissilesData[mtype].mType == 0 && MissilesData[mtype].mResist == MISR_NONE)
			dam += player._pIBonusDamMod + player._pDamageMod + dam * player._pIBonusDam / 100;
		if (!shift)
			dam <<= 6;
	}
	if (MissilesData[mtype].mType != 0)
		dam /= 2;
	if (resper > 0) {
		dam -= (dam * resper) / 100;
		if (pnum == MyPlayerId)
			NetSendCmdDamage(true, p, dam);
		target.Say(HeroSpeech::ArghClang);
		return true;
	}

	if (blkper < blk) {
		StartPlrBlock(p, GetDirection(target.position.tile, player.position.tile));
		*blocked = true;
	} else {
		if (pnum == MyPlayerId)
			NetSendCmdDamage(true, p, dam);
		StartPlrHit(p, dam, false);
	}

	return true;
}

void RotateBlockedMissile(Missile &missile)
{
	int rotation = GenerateRnd(2) != 0 ? 1 : -1;

	if (missile._miAnimType == MFILE_ARROWS) {
		int dir = missile._miAnimFrame + rotation;
		missile._miAnimFrame = (dir + 15) % 16 + 1;
		return;
	}

	int dir = missile._mimfnum + rotation;
	int mAnimFAmt = MissileSpriteData[missile._miAnimType].animFAmt;
	if (dir < 0)
		dir = mAnimFAmt - 1;
	else if (dir >= mAnimFAmt)
		dir = 0;

	SetMissDir(missile, dir);
}

void CheckMissileCol(Missile &missile, int minDamage, int maxDamage, bool isDamageShifted, Point position, bool dontDeleteOnCollision)
{
	if (!InDungeonBounds(position))
		return;

	int mx = position.x;
	int my = position.y;

	bool isMonsterHit = false;
	const int mid = dMonster[mx][my];
	if (missile._micaster != TARGET_BOTH && !missile.IsTrap()) {
		if (missile._micaster == TARGET_MONSTERS) {
			if (mid != 0 && (mid > 0 || Monsters[abs(mid) - 1]._mmode == MonsterMode::Petrified)) {
				isMonsterHit = MonsterMHit(missile._misource, abs(mid) - 1, minDamage, maxDamage, missile._midist, missile._mitype, isDamageShifted);
			}
		} else {
			Monster &attackingMonster = Monsters[missile._misource];
			if ((attackingMonster._mFlags & MFLAG_TARGETS_MONSTER) != 0
			    && mid > 0
			    && (Monsters[mid - 1]._mFlags & MFLAG_GOLEM) != 0)
				isMonsterHit = MonsterTrapHit(mid - 1, minDamage, maxDamage, missile._midist, missile._mitype, isDamageShifted);
		}
	} else {
		if (mid > 0) {
			if (missile._micaster == TARGET_BOTH)
				isMonsterHit = MonsterMHit(missile._misource, mid - 1, minDamage, maxDamage, missile._midist, missile._mitype, isDamageShifted);
			else
				isMonsterHit = MonsterTrapHit(mid - 1, minDamage, maxDamage, missile._midist, missile._mitype, isDamageShifted);
		}
	}

	if (isMonsterHit) {
		if (!dontDeleteOnCollision)
			missile._mirange = 0;
		missile._miHitFlag = true;
	}

	bool isPlayerHit = false;
	bool blocked = false;
	const int8_t pid = dPlayer[mx][my];
	if (pid > 0) {
		if (missile._micaster != TARGET_BOTH && !missile.IsTrap()) {
			if (missile._micaster == TARGET_MONSTERS) {
				if ((pid - 1) != missile._misource)
					isPlayerHit = Plr2PlrMHit(missile._misource, pid - 1, minDamage, maxDamage, missile._midist, missile._mitype, isDamageShifted, &blocked);
			} else {
				Monster &monster = Monsters[missile._misource];
				isPlayerHit = PlayerMHit(pid - 1, &monster, missile._midist, minDamage, maxDamage, missile._mitype, isDamageShifted, 0, &blocked);
			}
		} else {
			int earflag = (missile._miAnimType == MFILE_FIREWAL || missile._miAnimType == MFILE_LGHNING) ? 1 : 0;
			isPlayerHit = PlayerMHit(pid - 1, nullptr, missile._midist, minDamage, maxDamage, missile._mitype, isDamageShifted, earflag, &blocked);
		}
	}

	if (isPlayerHit) {
		if (gbIsHellfire && blocked) {
			RotateBlockedMissile(missile);
		} else if (!dontDeleteOnCollision) {
			missile._mirange = 0;
		}
		missile._miHitFlag = true;
	}

	if (IsMissileBlockedByTile({ mx, my })) {
		Object *object = ObjectAtPosition({ mx, my });
		if (object != nullptr && object->IsBreakable()) {
			BreakObject(-1, *object);
		}

		if (!dontDeleteOnCollision)
			missile._mirange = 0;
		missile._miHitFlag = false;
	}

	if (missile._mirange == 0 && MissilesData[missile._mitype].miSFX != -1)
		PlaySfxLoc(MissilesData[missile._mitype].miSFX, missile.position.tile);
}

void MoveMissileAndCheckMissileCol(Missile &missile, int mindam, int maxdam, bool ignoreStart, bool ifCollidesDontMoveToHitTile)
{
	Point prevTile = missile.position.tile;
	missile.position.traveled += missile.position.velocity;
	UpdateMissilePos(missile);

	int possibleVisitTiles;
	if (missile.position.velocity.deltaX == 0 || missile.position.velocity.deltaY == 0)
		possibleVisitTiles = prevTile.WalkingDistance(missile.position.tile);
	else
		possibleVisitTiles = prevTile.ManhattanDistance(missile.position.tile);

	int16_t tileTargetHash = dMonster[missile.position.tile.x][missile.position.tile.y] ^ dPlayer[missile.position.tile.x][missile.position.tile.y];

	if (possibleVisitTiles == 0) {
		// missile didn't change the tile... check that we perform CheckMissileCol only once for any monster/player to avoid multiple hits for slow missiles
		if (missile.lastCollisionTargetHash == tileTargetHash)
			return;
	}
	// remember what target CheckMissileCol was checked against
	missile.lastCollisionTargetHash = tileTargetHash;
	// Did the missile skipped a tile?
	if (possibleVisitTiles > 1) {
		// Implementation note: If someone knows the correct math to calculate this without this step for step increase loop, I would really appreciate it.
		auto incVelocity = missile.position.velocity * (0.01f / (float)(possibleVisitTiles - 1));
		auto traveled = missile.position.traveled - missile.position.velocity;
		do {
			traveled += incVelocity;

			// calculate in-between tile
			int mx = traveled.deltaX >> 16;
			int my = traveled.deltaY >> 16;
			int dx = (mx + 2 * my) / 64;
			int dy = (2 * my - mx) / 64;

			auto tile = missile.position.start + Displacement { dx, dy };

			// we are at the orginal calculated position => resume with normal logic
			if (tile == missile.position.tile)
				break;

			// don't call CheckMissileCol more than once for a tile
			if (prevTile == tile)
				continue;
			prevTile = tile;

			CheckMissileCol(missile, mindam, maxdam, false, tile, false);

			// Did missile hit anything?
			if (missile._mirange != 0)
				continue;

			if ((missile._miHitFlag && MissilesData[missile._mitype].MovementDistribution == MissileMovementDistrubution::Blockable) || IsMissileBlockedByTile(tile)) {
				missile.position.traveled = traveled;
				if (ifCollidesDontMoveToHitTile && missile._mirange == 0) {
					missile.position.traveled -= incVelocity;
					UpdateMissilePos(missile);
					missile.position.StopMissile();
				} else {
					UpdateMissilePos(missile);
				}
				return;
			}
		} while (true);
	}
	if (ignoreStart && missile.position.start == missile.position.tile)
		return;
	CheckMissileCol(missile, mindam, maxdam, false, missile.position.tile, false);
	if (ifCollidesDontMoveToHitTile && missile._mirange == 0) {
		missile.position.traveled -= missile.position.velocity;
		UpdateMissilePos(missile);
		missile.position.StopMissile();
	}
}

void SetMissAnim(Missile &missile, int animtype)
{
	int dir = missile._mimfnum;

	if (animtype > MFILE_NONE) {
		animtype = MFILE_NONE;
	}

	missile._miAnimType = animtype;
	missile._miAnimFlags = MissileSpriteData[animtype].flags;
	missile._miAnimData = MissileSpriteData[animtype].GetFrame(static_cast<size_t>(dir));
	missile._miAnimDelay = MissileSpriteData[animtype].animDelay[dir];
	missile._miAnimLen = MissileSpriteData[animtype].animLen[dir];
	missile._miAnimWidth = MissileSpriteData[animtype].animWidth;
	missile._miAnimWidth2 = MissileSpriteData[animtype].animWidth2;
	missile._miAnimCnt = 0;
	missile._miAnimFrame = 1;
}

void AddRune(Missile &missile, Point dst, missile_id missileID)
{
	if (LineClearMissile(missile.position.start, dst)) {
		std::optional<Point> runePosition = FindClosestValidPosition(
		    [](Point target) {
			    if (!InDungeonBounds(target)) {
				    return false;
			    }
			    if (IsObjectAtPosition(target)) {
				    return false;
			    }
			    if (TileContainsMissile(target)) {
				    return false;
			    }
			    if (nSolidTable[dPiece[target.x][target.y]]) {
				    return false;
			    }
			    return true;
		    },
		    dst, 0, 9);

		if (runePosition) {
			missile.position.tile = *runePosition;
			missile.var1 = missileID;
			missile._mlid = AddLight(missile.position.tile, 8);
			return;
		}
	}

	missile._miDelFlag = true;
}

bool CheckIfTrig(Point position)
{
	for (int i = 0; i < numtrigs; i++) {
		if (trigs[i].position.WalkingDistance(position) < 2)
			return true;
	}
	return false;
}

bool GuardianTryFireAt(Missile &missile, Point target)
{
	Point position = missile.position.tile;

	if (!LineClearMissile(position, target))
		return false;
	int mid = dMonster[target.x][target.y] - 1;
	if (mid < 0)
		return false;
	const Monster &monster = Monsters[mid];
	if (monster.MType->mtype == MT_GOLEM)
		return false;
	if (monster._mhitpoints >> 6 <= 0)
		return false;

	Direction dir = GetDirection(position, target);
	AddMissile(position, target, dir, MIS_FIREBOLT, TARGET_MONSTERS, missile._misource, missile._midam, GetSpellLevel(missile._misource, SPL_FIREBOLT), &missile);
	SetMissDir(missile, 2);
	missile.var2 = 3;

	return true;
}

bool GrowWall(int playerId, Point position, Point target, missile_id type, int spellLevel, int damage)
{
	int dp = dPiece[position.x][position.y];
	assert(dp <= MAXTILES && dp >= 0);

	if (nMissileTable[dp] || !InDungeonBounds(target)) {
		return false;
	}

	AddMissile(position, position, Players[playerId]._pdir, type, TARGET_BOTH, playerId, damage, spellLevel);
	return true;
}

void SpawnLightning(Missile &missile, int dam)
{
	missile._mirange--;
	missile.position.traveled += missile.position.velocity;
	UpdateMissilePos(missile);

	Point position = missile.position.tile;
	assert(InDungeonBounds(position));
	int pn = dPiece[position.x][position.y];
	assert(pn >= 0 && pn <= MAXTILES);

	if (!missile.IsTrap() || position != missile.position.start) {
		if (nMissileTable[pn]) {
			missile._mirange = 0;
		}
	}

	if (!nMissileTable[pn]) {
		if (position != Point { missile.var1, missile.var2 } && InDungeonBounds(position)) {
			missile_id type = MIS_LIGHTNING;
			if (!missile.IsTrap() && missile._micaster == TARGET_PLAYERS
			    && IsAnyOf(Monsters[missile._misource].MType->mtype, MT_STORM, MT_RSTORM, MT_STORML, MT_MAEL)) {
				type = MIS_LIGHTNING2;
			}
			AddMissile(
			    position,
			    missile.position.start,
			    Direction::South,
			    type,
			    missile._micaster,
			    missile._misource,
			    dam,
			    missile._mispllvl);
			missile.var1 = position.x;
			missile.var2 = position.y;
		}
	}

	if (missile._mirange == 0) {
		missile._miDelFlag = true;
	}
}

} // namespace

bool IsMissileBlockedByTile(Point tile)
{
	if (!InDungeonBounds(tile)) {
		return true;
	}

	if (nMissileTable[dPiece[tile.x][tile.y]]) {
		return true;
	}

	Object *object = ObjectAtPosition(tile);
	// _oMissFlag is true if the object allows missiles to pass through so we need to invert the check here...
	return object != nullptr && !object->_oMissFlag;
}

void GetDamageAmt(int i, int *mind, int *maxd)
{
	assert(MyPlayerId >= 0 && MyPlayerId < MAX_PLRS);
	assert(i >= 0 && i < 64);

	Player &myPlayer = *MyPlayer;

	int sl = myPlayer._pSplLvl[i] + myPlayer._pISplLvlAdd;

	switch (i) {
	case SPL_FIREBOLT:
		*mind = (myPlayer._pMagic / 8) + sl + 1;
		*maxd = *mind + 9;
		break;
	case SPL_HEAL:
	case SPL_HEALOTHER:
		/// BUGFIX: healing calculation is unused
		*mind = AddClassHealingBonus(myPlayer._pLevel + sl + 1, myPlayer._pClass) - 1;
		*maxd = AddClassHealingBonus((4 * myPlayer._pLevel) + (6 * sl) + 10, myPlayer._pClass) - 1;
		break;
	case SPL_LIGHTNING:
	case SPL_RUNELIGHT:
		*mind = 2;
		*maxd = 2 + myPlayer._pLevel;
		break;
	case SPL_FLASH:
		*mind = ScaleSpellEffect(myPlayer._pLevel, sl);
		*mind += *mind / 2;
		*maxd = *mind * 2;
		break;
	case SPL_IDENTIFY:
	case SPL_TOWN:
	case SPL_STONE:
	case SPL_INFRA:
	case SPL_RNDTELEPORT:
	case SPL_MANASHIELD:
	case SPL_DOOMSERP:
	case SPL_BLODRIT:
	case SPL_INVISIBIL:
	case SPL_BLODBOIL:
	case SPL_TELEPORT:
	case SPL_ETHEREALIZE:
	case SPL_REPAIR:
	case SPL_RECHARGE:
	case SPL_DISARM:
	case SPL_RESURRECT:
	case SPL_TELEKINESIS:
	case SPL_BONESPIRIT:
	case SPL_WARP:
	case SPL_REFLECT:
	case SPL_BERSERK:
	case SPL_SEARCH:
	case SPL_RUNESTONE:
		*mind = -1;
		*maxd = -1;
		break;
	case SPL_FIREWALL:
	case SPL_LIGHTWALL:
	case SPL_FIRERING:
		*mind = 2 * myPlayer._pLevel + 4;
		*maxd = *mind + 36;
		break;
	case SPL_FIREBALL:
	case SPL_RUNEFIRE: {
		int base = (2 * myPlayer._pLevel) + 4;
		*mind = ScaleSpellEffect(base, sl);
		*maxd = ScaleSpellEffect(base + 36, sl);
	} break;
	case SPL_GUARDIAN: {
		int base = (myPlayer._pLevel / 2) + 1;
		*mind = ScaleSpellEffect(base, sl);
		*maxd = ScaleSpellEffect(base + 9, sl);
	} break;
	case SPL_CHAIN:
		*mind = 4;
		*maxd = 4 + (2 * myPlayer._pLevel);
		break;
	case SPL_WAVE:
		*mind = 6 * (myPlayer._pLevel + 1);
		*maxd = *mind + 54;
		break;
	case SPL_NOVA:
	case SPL_IMMOLAT:
	case SPL_RUNEIMMOLAT:
	case SPL_RUNENOVA:
		*mind = ScaleSpellEffect((myPlayer._pLevel + 5) / 2, sl) * 5;
		*maxd = ScaleSpellEffect((myPlayer._pLevel + 30) / 2, sl) * 5;
		break;
	case SPL_FLAME:
		*mind = 3;
		*maxd = myPlayer._pLevel + 4;
		*maxd += *maxd / 2;
		break;
	case SPL_GOLEM:
		*mind = 11;
		*maxd = 17;
		break;
	case SPL_APOCA:
		*mind = myPlayer._pLevel;
		*maxd = *mind * 6;
		break;
	case SPL_ELEMENT:
		*mind = ScaleSpellEffect(2 * myPlayer._pLevel + 4, sl);
		/// BUGFIX: add here '*mind /= 2;'
		*maxd = ScaleSpellEffect(2 * myPlayer._pLevel + 40, sl);
		/// BUGFIX: add here '*maxd /= 2;'
		break;
	case SPL_CBOLT:
		*mind = 1;
		*maxd = *mind + (myPlayer._pMagic / 4);
		break;
	case SPL_HBOLT:
		*mind = myPlayer._pLevel + 9;
		*maxd = *mind + 9;
		break;
	case SPL_FLARE:
		*mind = (myPlayer._pMagic / 2) + 3 * sl - (myPlayer._pMagic / 8);
		*maxd = *mind;
		break;
	}
}

int GetSpellLevel(int playerId, spell_id sn)
{
	if (playerId != MyPlayerId)
		return 1; // BUGFIX spell level will be wrong in multiplayer

	Player &player = Players[playerId];

	return std::max(player._pISplLvlAdd + player._pSplLvl[sn], 0);
}

Direction16 GetDirection16(Point p1, Point p2)
{
	Displacement offset = p2 - p1;
	Displacement absolute = abs(offset);

	bool flipY = offset.deltaX != absolute.deltaX;
	bool flipX = offset.deltaY != absolute.deltaY;

	bool flipMedian = false;
	if (absolute.deltaX > absolute.deltaY) {
		std::swap(absolute.deltaX, absolute.deltaY);
		flipMedian = true;
	}

	Direction16 ret = Direction16::South;
	if (3 * absolute.deltaX <= (absolute.deltaY * 2)) { // mx/my <= 2/3, approximation of tan(33.75)
		if (5 * absolute.deltaX < absolute.deltaY)      // mx/my < 0.2, approximation of tan(11.25)
			ret = Direction16::SouthWest;
		else
			ret = Direction16::South_SouthWest;
	}

	Direction16 medianPivot = Direction16::South;
	if (flipY) {
		ret = Direction16Flip(ret, Direction16::SouthWest);
		medianPivot = Direction16Flip(medianPivot, Direction16::SouthWest);
	}
	if (flipX) {
		ret = Direction16Flip(ret, Direction16::SouthEast);
		medianPivot = Direction16Flip(medianPivot, Direction16::SouthEast);
	}
	if (flipMedian)
		ret = Direction16Flip(ret, medianPivot);
	return ret;
}

bool MonsterTrapHit(int m, int mindam, int maxdam, int dist, missile_id t, bool shift)
{
	auto &monster = Monsters[m];

	if (!monster.IsPossibleToHit() || monster.IsImmune(t))
		return false;

	int hit = GenerateRnd(100);
	int hper = 90 - (BYTE)monster.mArmorClass - dist;
	hper = clamp(hper, 5, 95);
	if (monster.TryLiftGargoyle())
		return true;
	if (hit >= hper && monster._mmode != MonsterMode::Petrified) {
#ifdef _DEBUG
		if (!DebugGodMode)
#endif
			return false;
	}

	bool resist = monster.IsResistant(t);
	int dam = mindam + GenerateRnd(maxdam - mindam + 1);
	if (!shift)
		dam <<= 6;
	if (resist)
		monster._mhitpoints -= dam / 4;
	else
		monster._mhitpoints -= dam;
#ifdef _DEBUG
	if (DebugGodMode)
		monster._mhitpoints = 0;
#endif
	if (monster._mhitpoints >> 6 <= 0) {
		M_StartKill(m, -1);
	} else if (resist) {
		PlayEffect(monster, 1);
	} else {
		if (monster.MType->mtype != MT_GOLEM)
			M_StartHit(m, -1, dam);
	}
	return true;
}

bool PlayerMHit(int pnum, Monster *monster, int dist, int mind, int maxd, missile_id mtype, bool shift, int earflag, bool *blocked)
{
	*blocked = false;

	Player &player = Players[pnum];

	if (player._pHitPoints >> 6 <= 0) {
		return false;
	}

	if (player._pInvincible) {
		return false;
	}

	if (HasAnyOf(player._pSpellFlags, SpellFlag::Etherealize) && MissilesData[mtype].mType == 0) {
		return false;
	}

	int hit = GenerateRnd(100);
#ifdef _DEBUG
	if (DebugGodMode)
		hit = 1000;
#endif
	int hper = 40;
	if (MissilesData[mtype].mType == 0) {
		int tac = player.GetArmor();
		if (monster != nullptr) {
			hper = monster->mHit
			    + ((monster->mLevel - player._pLevel) * 2)
			    + 30
			    - (dist * 2) - tac;
		} else {
			hper = 100 - (tac / 2) - (dist * 2);
		}
	} else if (monster != nullptr) {
		hper += (monster->mLevel * 2) - (player._pLevel * 2) - (dist * 2);
	}

	int minhit = 10;
	if (currlevel == 14)
		minhit = 20;
	if (currlevel == 15)
		minhit = 25;
	if (currlevel == 16)
		minhit = 30;
	hper = std::max(hper, minhit);

	int blk = 100;
	if ((player._pmode == PM_STAND || player._pmode == PM_ATTACK) && player._pBlockFlag) {
		blk = GenerateRnd(100);
	}

	if (shift)
		blk = 100;
	if (mtype == MIS_ACIDPUD)
		blk = 100;

	int blkper = player.GetBlockChance(false);
	if (monster != nullptr)
		blkper -= (monster->mLevel - player._pLevel) * 2;
	blkper = clamp(blkper, 0, 100);

	int8_t resper;
	switch (MissilesData[mtype].mResist) {
	case MISR_FIRE:
		resper = player._pFireResist;
		break;
	case MISR_LIGHTNING:
		resper = player._pLghtResist;
		break;
	case MISR_MAGIC:
	case MISR_ACID:
		resper = player._pMagResist;
		break;
	default:
		resper = 0;
		break;
	}

	if (hit >= hper) {
		return false;
	}

	int dam;
	if (mtype == MIS_BONESPIRIT) {
		dam = player._pHitPoints / 3;
	} else {
		if (!shift) {
			dam = (mind << 6) + GenerateRnd(((maxd - mind) << 6) + 1);
			if (monster == nullptr)
				if (HasAnyOf(player._pIFlags, ItemSpecialEffect::HalfTrapDamage))
					dam /= 2;
			dam += player._pIGetHit * 64;
		} else {
			dam = mind + GenerateRnd(maxd - mind + 1);
			if (monster == nullptr)
				if (HasAnyOf(player._pIFlags, ItemSpecialEffect::HalfTrapDamage))
					dam /= 2;
			dam += player._pIGetHit;
		}

		dam = std::max(dam, 64);
	}

	if ((resper <= 0 || gbIsHellfire) && blk < blkper) {
		Direction dir = player._pdir;
		if (monster != nullptr) {
			dir = GetDirection(player.position.tile, monster->position.tile);
		}
		*blocked = true;
		StartPlrBlock(pnum, dir);
		return true;
	}

	if (resper > 0) {
		dam -= dam * resper / 100;
		if (pnum == MyPlayerId) {
			ApplyPlrDamage(pnum, 0, 0, dam, earflag);
		}

		if (player._pHitPoints >> 6 > 0) {
			player.Say(HeroSpeech::ArghClang);
		}
		return true;
	}

	if (pnum == MyPlayerId) {
		ApplyPlrDamage(pnum, 0, 0, dam, earflag);
	}

	if (player._pHitPoints >> 6 > 0) {
		StartPlrHit(pnum, dam, false);
	}

	return true;
}

void SetMissDir(Missile &missile, int dir)
{
	missile._mimfnum = dir;
	SetMissAnim(missile, missile._miAnimType);
}

void InitMissiles()
{
	Player &myPlayer = *MyPlayer;

	AutoMapShowItems = false;
	myPlayer._pSpellFlags &= ~SpellFlag::Etherealize;
	if (myPlayer._pInfraFlag) {
		for (auto &missile : Missiles) {
			if (missile._mitype == MIS_INFRA) {
				int src = missile._misource;
				if (src == MyPlayerId)
					CalcPlrItemVals(myPlayer, true);
			}
		}
	}

	if (HasAnyOf(myPlayer._pSpellFlags, SpellFlag::RageActive | SpellFlag::RageCooldown)) {
		myPlayer._pSpellFlags &= ~SpellFlag::RageActive;
		myPlayer._pSpellFlags &= ~SpellFlag::RageCooldown;
		for (auto &missile : Missiles) {
			if (missile._mitype == MIS_BLODBOIL) {
				if (missile._misource == MyPlayerId) {
					int missingHP = myPlayer._pMaxHP - myPlayer._pHitPoints;
					CalcPlrItemVals(myPlayer, true);
					ApplyPlrDamage(MyPlayerId, 0, 1, missingHP + missile.var2);
				}
			}
		}
	}

	Missiles.clear();
	for (int j = 0; j < MAXDUNY; j++) {
		for (int i = 0; i < MAXDUNX; i++) { // NOLINT(modernize-loop-convert)
			dFlags[i][j] &= ~DungeonFlag::Missile;
		}
	}
}

void AddHiveExplosion(Missile &missile, const AddMissileParameter &parameter)
{
	for (int x : { 80, 81 }) {
		for (int y : { 62, 63 }) {
			AddMissile({ x, y }, { 80, 62 }, parameter.midir, MIS_HIVEEXP, missile._micaster, missile._misource, missile._midam, 0);
		}
	}
	missile._miDelFlag = true;
}

void AddFireRune(Missile &missile, const AddMissileParameter &parameter)
{
	AddRune(missile, parameter.dst, MIS_HIVEEXP);
}

void AddLightningRune(Missile &missile, const AddMissileParameter &parameter)
{
	int id = missile._misource;
	int lvl = (id > -1) ? Players[id]._pLevel : 0;
	int dmg = 16 * (GenerateRndSum(10, 2) + lvl + 2);
	missile._midam = dmg;
	AddRune(missile, parameter.dst, MIS_LIGHTWALL);
}

void AddGreatLightningRune(Missile &missile, const AddMissileParameter &parameter)
{
	AddRune(missile, parameter.dst, MIS_NOVA);
}

void AddImmolationRune(Missile &missile, const AddMissileParameter &parameter)
{
	AddRune(missile, parameter.dst, MIS_IMMOLATION);
}

void AddStoneRune(Missile &missile, const AddMissileParameter &parameter)
{
	AddRune(missile, parameter.dst, MIS_STONE);
}

void AddReflection(Missile &missile, const AddMissileParameter & /*parameter*/)
{
	missile._miDelFlag = true;

	if (missile._misource < 0)
		return;

	Player &player = Players[missile._misource];

	int add = (missile._mispllvl != 0 ? missile._mispllvl : 2) * player._pLevel;
	if (player.wReflections + add >= std::numeric_limits<uint16_t>::max())
		add = 0;
	player.wReflections += add;
	if (missile._misource == MyPlayerId)
		NetSendCmdParam1(true, CMD_SETREFLECT, player.wReflections);

	UseMana(missile._misource, SPL_REFLECT);
}

void AddBerserk(Missile &missile, const AddMissileParameter &parameter)
{
	missile._miDelFlag = true;

	if (missile._misource < 0)
		return;

	std::optional<Point> targetMonsterPosition = FindClosestValidPosition(
	    [](Point target) {
		    if (!InDungeonBounds(target)) {
			    return false;
		    }

		    int monsterId = abs(dMonster[target.x][target.y]) - 1;
		    if (monsterId < 0)
			    return false;

		    const Monster &monster = Monsters[monsterId];
		    if (monster.MType->mtype == MT_GOLEM)
			    return false;
		    if ((monster._mFlags & MFLAG_BERSERK) != 0)
			    return false;
		    if (monster._uniqtype != 0 || monster._mAi == AI_DIABLO)
			    return false;
		    if (IsAnyOf(monster._mmode, MonsterMode::FadeIn, MonsterMode::FadeOut, MonsterMode::Charge))
			    return false;
		    if ((monster.mMagicRes & IMMUNE_MAGIC) != 0)
			    return false;
		    if ((monster.mMagicRes & RESIST_MAGIC) != 0 && ((monster.mMagicRes & RESIST_MAGIC) != 1 || GenerateRnd(2) != 0))
			    return false;

		    return true;
	    },
	    parameter.dst, 0, 5);

	if (targetMonsterPosition) {
		auto &monster = Monsters[abs(dMonster[targetMonsterPosition->x][targetMonsterPosition->y]) - 1];
		int slvl = GetSpellLevel(missile._misource, SPL_BERSERK);
		monster._mFlags |= MFLAG_BERSERK | MFLAG_GOLEM;
		monster.mMinDamage = (GenerateRnd(10) + 120) * monster.mMinDamage / 100 + slvl;
		monster.mMaxDamage = (GenerateRnd(10) + 120) * monster.mMaxDamage / 100 + slvl;
		monster.mMinDamage2 = (GenerateRnd(10) + 120) * monster.mMinDamage2 / 100 + slvl;
		monster.mMaxDamage2 = (GenerateRnd(10) + 120) * monster.mMaxDamage2 / 100 + slvl;
		int lightRadius = (currlevel < 17 || currlevel > 20) ? 3 : 9;
		monster.mlid = AddLight(monster.position.tile, lightRadius);
		UseMana(missile._misource, SPL_BERSERK);
	}
}

void AddHorkSpawn(Missile &missile, const AddMissileParameter &parameter)
{
	UpdateMissileVelocity(missile, parameter.dst, 8);
	missile._mirange = 9;
	missile.var1 = static_cast<int32_t>(parameter.midir);
	PutMissile(missile);
}

void AddJester(Missile &missile, const AddMissileParameter &parameter)
{
	missile_id spell = MIS_FIREBOLT;
	switch (GenerateRnd(10)) {
	case 0:
	case 1:
		spell = MIS_FIREBOLT;
		break;
	case 2:
		spell = MIS_FIREBALL;
		break;
	case 3:
		spell = MIS_FIREWALLC;
		break;
	case 4:
		spell = MIS_GUARDIAN;
		break;
	case 5:
		spell = MIS_CHAIN;
		break;
	case 6:
		spell = MIS_TOWN;
		UseMana(missile._misource, SPL_TOWN);
		break;
	case 7:
		spell = MIS_TELEPORT;
		break;
	case 8:
		spell = MIS_APOCA;
		break;
	case 9:
		spell = MIS_STONE;
		break;
	}
	AddMissile(missile.position.start, parameter.dst, parameter.midir, spell, missile._micaster, missile._misource, 0, missile._mispllvl);
	missile._miDelFlag = true;
}

void AddStealPotions(Missile &missile, const AddMissileParameter & /*parameter*/)
{
	for (int i = 0; i < 3; i++) {
		int k = CrawlNum[i];
		int ck = k + 2;
		for (auto j = static_cast<uint8_t>(CrawlTable[k]); j > 0; j--, ck += 2) {
			Point target = missile.position.start + Displacement { CrawlTable[ck - 1], CrawlTable[ck] };
			if (!InDungeonBounds(target))
				continue;
			int8_t pnum = dPlayer[target.x][target.y];
			if (pnum == 0)
				continue;
			Player &player = Players[abs(pnum) - 1];

			bool hasPlayedSFX = false;
			for (int si = 0; si < MAXBELTITEMS; si++) {
				int ii = -1;
				if (player.SpdList[si]._itype == ItemType::Misc) {
					if (GenerateRnd(2) == 0)
						continue;
					switch (player.SpdList[si]._iMiscId) {
					case IMISC_FULLHEAL:
						ii = ItemMiscIdIdx(IMISC_HEAL);
						break;
					case IMISC_HEAL:
					case IMISC_MANA:
						player.RemoveSpdBarItem(si);
						break;
					case IMISC_FULLMANA:
						ii = ItemMiscIdIdx(IMISC_MANA);
						break;
					case IMISC_REJUV:
						if (GenerateRnd(2) != 0) {
							ii = ItemMiscIdIdx(IMISC_MANA);
						} else {
							ii = ItemMiscIdIdx(IMISC_HEAL);
						}
						break;
					case IMISC_FULLREJUV:
						switch (GenerateRnd(3)) {
						case 0:
							ii = ItemMiscIdIdx(IMISC_FULLMANA);
							break;
						case 1:
							ii = ItemMiscIdIdx(IMISC_FULLHEAL);
							break;
						default:
							ii = ItemMiscIdIdx(IMISC_REJUV);
							break;
						}
						break;
					default:
						continue;
					}
				}
				if (ii != -1) {
					InitializeItem(player.SpdList[si], ii);
					player.SpdList[si]._iStatFlag = true;
				}
				if (!hasPlayedSFX) {
					PlaySfxLoc(IS_POPPOP2, target);
					hasPlayedSFX = true;
				}
			}
			force_redraw = 255;
		}
	}
	missile._miDelFlag = true;
}

void AddManaTrap(Missile &missile, const AddMissileParameter & /*parameter*/)
{
	std::optional<Point> trappedPlayerPosition = FindClosestValidPosition(
	    [](Point target) {
		    return InDungeonBounds(target) && dPlayer[target.x][target.y] != 0;
	    },
	    missile.position.start, 0, 2);

	if (trappedPlayerPosition) {
		Player &player = Players[abs(dPlayer[trappedPlayerPosition->x][trappedPlayerPosition->y]) - 1];

		player._pMana = 0;
		player._pManaBase = player._pMana + player._pMaxManaBase - player._pMaxMana;
		CalcPlrInv(player, false);
		drawmanaflag = true;
		PlaySfxLoc(TSFX_COW7, *trappedPlayerPosition);
	}

	missile._miDelFlag = true;
}

void AddSpecArrow(Missile &missile, const AddMissileParameter &parameter)
{
	int av = 0;

	if (missile._micaster == TARGET_MONSTERS) {
		Player &player = Players[missile._misource];

		if (player._pClass == HeroClass::Rogue)
			av += (player._pLevel - 1) / 4;
		else if (player._pClass == HeroClass::Warrior || player._pClass == HeroClass::Bard)
			av += (player._pLevel - 1) / 8;

		if (HasAnyOf(player._pIFlags, ItemSpecialEffect::QuickAttack))
			av++;
		if (HasAnyOf(player._pIFlags, ItemSpecialEffect::FastAttack))
			av += 2;
		if (HasAnyOf(player._pIFlags, ItemSpecialEffect::FasterAttack))
			av += 4;
		if (HasAnyOf(player._pIFlags, ItemSpecialEffect::FastestAttack))
			av += 8;
	}

	missile._mirange = 1;
	missile.var1 = parameter.dst.x;
	missile.var2 = parameter.dst.y;
	missile.var3 = av;
}

void AddWarp(Missile &missile, const AddMissileParameter & /*parameter*/)
{
	int minDistanceSq = std::numeric_limits<int>::max();
	Point src = missile.position.start;
	Point tile = src;
	if (missile._misource >= 0) {
		tile = Players[missile._misource].position.tile;
	}

	for (int i = 0; i < numtrigs && i < MAXTRIGGERS; i++) {
		TriggerStruct *trg = &trigs[i];
		if (trg->_tmsg == WM_DIABTWARPUP || trg->_tmsg == WM_DIABPREVLVL || trg->_tmsg == WM_DIABNEXTLVL || trg->_tmsg == WM_DIABRTNLVL) {
			Point candidate = trg->position;
			if ((leveltype == DTYPE_CATHEDRAL || leveltype == DTYPE_CATACOMBS) && (trg->_tmsg == WM_DIABNEXTLVL || trg->_tmsg == WM_DIABPREVLVL || trg->_tmsg == WM_DIABRTNLVL)) {
				candidate += Displacement { 0, 1 };
			} else {
				candidate += Displacement { 1, 0 };
			}
			Displacement off = src - candidate;
			int distanceSq = off.deltaY * off.deltaY + off.deltaX * off.deltaX;
			if (distanceSq < minDistanceSq) {
				minDistanceSq = distanceSq;
				tile = candidate;
			}
		}
	}
	missile._mirange = 2;
	missile.position.tile = tile;
	if (missile._micaster == TARGET_MONSTERS)
		UseMana(missile._misource, SPL_WARP);
}

void AddLightningWall(Missile &missile, const AddMissileParameter &parameter)
{
	UpdateMissileVelocity(missile, parameter.dst, 16);
	missile._miAnimFrame = GenerateRnd(8) + 1;
	missile._mirange = 255 * (missile._mispllvl + 1);
	if (missile._misource < 0) {
		missile.var1 = missile.position.start.x;
		missile.var2 = missile.position.start.y;
	} else {
		missile.var1 = Players[missile._misource].position.tile.x;
		missile.var2 = Players[missile._misource].position.tile.y;
	}
}

void AddRuneExplosion(Missile &missile, const AddMissileParameter & /*parameter*/)
{
	if (IsAnyOf(missile._micaster, TARGET_MONSTERS, TARGET_BOTH)) {
		int dmg = 2 * (Players[missile._misource]._pLevel + GenerateRndSum(10, 2)) + 4;
		dmg = ScaleSpellEffect(dmg, missile._mispllvl);

		missile._midam = dmg;

		constexpr Displacement Offsets[] = { { -1, -1 }, { 0, -1 }, { 1, -1 }, { -1, 0 }, { 0, 0 }, { 1, 0 }, { -1, 1 }, { 0, 1 }, { 1, 1 } };
		for (Displacement offset : Offsets)
			CheckMissileCol(missile, dmg, dmg, false, missile.position.tile + offset, true);
	}
	missile._mlid = AddLight(missile.position.start, 8);
	SetMissDir(missile, 0);
	missile._mirange = missile._miAnimLen - 1;
}

void AddFireNova(Missile &missile, const AddMissileParameter &parameter)
{
	Point dst = parameter.dst;
	if (missile.position.start == parameter.dst) {
		dst += parameter.midir;
	}
	int sp = 16;
	if (missile._micaster == TARGET_MONSTERS) {
		sp += std::min(missile._mispllvl, 34);
	}
	UpdateMissileVelocity(missile, dst, sp);
	SetMissDir(missile, GetDirection16(missile.position.start, dst));
	missile._mirange = 256;
	missile._mlid = AddLight(missile.position.start, 8);
}

void AddLightningArrow(Missile &missile, const AddMissileParameter &parameter)
{
	Point dst = parameter.dst;
	if (missile.position.start == parameter.dst) {
		dst += parameter.midir;
	}
	UpdateMissileVelocity(missile, dst, 32);
	missile._miAnimFrame = GenerateRnd(8) + 1;
	missile._mirange = 255;
	if (missile._misource < 0) {
		missile.var1 = missile.position.start.x;
		missile.var2 = missile.position.start.y;
	} else {
		missile.var1 = Players[missile._misource].position.tile.x;
		missile.var2 = Players[missile._misource].position.tile.y;
	}
	missile._midam <<= 6;
}

void AddMana(Missile &missile, const AddMissileParameter & /*parameter*/)
{
	Player &player = Players[missile._misource];

	int manaAmount = (GenerateRnd(10) + 1) << 6;
	for (int i = 0; i < player._pLevel; i++) {
		manaAmount += (GenerateRnd(4) + 1) << 6;
	}
	for (int i = 0; i < missile._mispllvl; i++) {
		manaAmount += (GenerateRnd(6) + 1) << 6;
	}
	if (player._pClass == HeroClass::Sorcerer)
		manaAmount *= 2;
	if (player._pClass == HeroClass::Rogue || player._pClass == HeroClass::Bard)
		manaAmount += manaAmount / 2;
	player._pMana += manaAmount;
	if (player._pMana > player._pMaxMana)
		player._pMana = player._pMaxMana;
	player._pManaBase += manaAmount;
	if (player._pManaBase > player._pMaxManaBase)
		player._pManaBase = player._pMaxManaBase;
	UseMana(missile._misource, SPL_MANA);
	missile._miDelFlag = true;
	drawmanaflag = true;
}

void AddMagi(Missile &missile, const AddMissileParameter & /*parameter*/)
{
	Player &player = Players[missile._misource];

	player._pMana = player._pMaxMana;
	player._pManaBase = player._pMaxManaBase;
	UseMana(missile._misource, SPL_MAGI);
	missile._miDelFlag = true;
	drawmanaflag = true;
}

void AddRing(Missile &missile, const AddMissileParameter & /*parameter*/)
{
	if (missile._micaster == TARGET_MONSTERS)
		UseMana(missile._misource, SPL_FIRERING);
	missile.var1 = missile.position.start.x;
	missile.var2 = missile.position.start.y;
	missile._mirange = 7;
}

void AddSearch(Missile &missile, const AddMissileParameter & /*parameter*/)
{
	if (missile._misource == MyPlayerId)
		AutoMapShowItems = true;
	int lvl = 2;
	if (missile._misource >= 0)
		lvl = Players[missile._misource]._pLevel * 2;
	missile._mirange = lvl + 10 * missile._mispllvl + 245;
	if (missile._micaster == TARGET_MONSTERS)
		UseMana(missile._misource, SPL_SEARCH);

	for (auto &other : Missiles) {
		if (&other != &missile && other._misource == missile._misource && other._mitype == MIS_SEARCH) {
			int r1 = missile._mirange;
			int r2 = other._mirange;
			if (r2 < INT_MAX - r1)
				other._mirange = r1 + r2;
			missile._miDelFlag = true;
			break;
		}
	}
}

void AddCboltArrow(Missile &missile, const AddMissileParameter &parameter)
{
	Point dst = parameter.dst;
	missile._mirnd = GenerateRnd(15) + 1;
	if (missile._micaster != TARGET_MONSTERS) {
		missile._midam = 15;
	}

	if (missile.position.start == dst) {
		dst += parameter.midir;
	}
	missile._miAnimFrame = GenerateRnd(8) + 1;
	missile._mlid = AddLight(missile.position.start, 5);
	UpdateMissileVelocity(missile, dst, 8);
	missile.var1 = 5;
	missile.var2 = static_cast<int32_t>(parameter.midir);
	missile._mirange = 256;
}

void AddLArrow(Missile &missile, const AddMissileParameter &parameter)
{
	Point dst = parameter.dst;
	if (missile.position.start == dst) {
		dst += parameter.midir;
	}
	int av = 32;
	if (missile._micaster == TARGET_MONSTERS) {
		Player &player = Players[missile._misource];
		if (player._pClass == HeroClass::Rogue)
			av += (player._pLevel) / 4;
		else if (IsAnyOf(player._pClass, HeroClass::Warrior, HeroClass::Bard))
			av += (player._pLevel) / 8;

		if (gbIsHellfire) {
			if (HasAnyOf(player._pIFlags, ItemSpecialEffect::QuickAttack))
				av++;
			if (HasAnyOf(player._pIFlags, ItemSpecialEffect::FastAttack))
				av += 2;
			if (HasAnyOf(player._pIFlags, ItemSpecialEffect::FasterAttack))
				av += 4;
			if (HasAnyOf(player._pIFlags, ItemSpecialEffect::FastestAttack))
				av += 8;
		} else {
			if (IsAnyOf(player._pClass, HeroClass::Rogue, HeroClass::Warrior, HeroClass::Bard))
				av -= 1;
		}
	}
	UpdateMissileVelocity(missile, dst, av);

	SetMissDir(missile, GetDirection16(missile.position.start, dst));
	missile._mirange = 256;
	missile.var1 = missile.position.start.x;
	missile.var2 = missile.position.start.y;
	missile._mlid = AddLight(missile.position.start, 5);
}

void AddArrow(Missile &missile, const AddMissileParameter &parameter)
{
	Point dst = parameter.dst;
	if (missile.position.start == dst) {
		dst += parameter.midir;
	}
	int av = 32;
	if (missile._micaster == TARGET_MONSTERS) {
		Player &player = Players[missile._misource];

		if (HasAnyOf(player._pIFlags, ItemSpecialEffect::RandomArrowVelocity)) {
			av = GenerateRnd(32) + 16;
		}
		if (player._pClass == HeroClass::Rogue)
			av += (player._pLevel - 1) / 4;
		else if (player._pClass == HeroClass::Warrior || player._pClass == HeroClass::Bard)
			av += (player._pLevel - 1) / 8;

		if (gbIsHellfire) {
			if (HasAnyOf(player._pIFlags, ItemSpecialEffect::QuickAttack))
				av++;
			if (HasAnyOf(player._pIFlags, ItemSpecialEffect::FastAttack))
				av += 2;
			if (HasAnyOf(player._pIFlags, ItemSpecialEffect::FasterAttack))
				av += 4;
			if (HasAnyOf(player._pIFlags, ItemSpecialEffect::FastestAttack))
				av += 8;
		}
	}
	UpdateMissileVelocity(missile, dst, av);
	missile._miAnimFrame = static_cast<int>(GetDirection16(missile.position.start, dst)) + 1;
	missile._mirange = 256;
}

void UpdateVileMissPos(Missile &missile, Point dst)
{
	for (int k = 1; k < 50; k++) {
		for (int j = -k; j <= k; j++) {
			int yy = j + dst.y;
			for (int i = -k; i <= k; i++) {
				int xx = i + dst.x;
				if (PosOkPlayer(*MyPlayer, { xx, yy })) {
					missile.position.tile = { xx, yy };
					return;
				}
			}
		}
	}
}

void AddRndTeleport(Missile &missile, const AddMissileParameter &parameter)
{
	missile._mirange = 2;

	Player &player = Players[missile._misource];

	if (missile._micaster == TARGET_BOTH) {
		missile.position.tile = parameter.dst;
		if (!PosOkPlayer(player, parameter.dst))
			UpdateVileMissPos(missile, parameter.dst);
		return;
	}

	std::array<Point, 4 * 9> targets;

	int count = 0;
	for (int y = -6; y <= 6; y++) {
		for (int x = -6; x <= 6; x++) {
			if ((x >= -3 && x <= 3) || (y >= -3 && y <= 3))
				continue; // Skip center

			Point target = missile.position.start + Displacement { x, y };
			if (!PosOkPlayer(player, target))
				continue;

			targets[count] = target;
			count++;
		}
	}

	if (count == 0) {
		missile._miDelFlag = true;
		return;
	}

	missile.position.tile = targets[std::max<int32_t>(GenerateRnd(count), 0)];

	if (missile._micaster == TARGET_MONSTERS)
		UseMana(missile._misource, SPL_RNDTELEPORT);
}

void AddFirebolt(Missile &missile, const AddMissileParameter &parameter)
{
	Point dst = parameter.dst;
	if (missile.position.start == dst) {
		dst += parameter.midir;
	}
	int sp = 26;
	if (missile._micaster == TARGET_MONSTERS) {
		sp = 16;
		if (!missile.IsTrap()) {
			sp += std::min(missile._mispllvl * 2, 47);
		}

		if (parameter.pParent == nullptr || parameter.pParent->_mitype != MIS_GUARDIAN)
			UseMana(missile._misource, SPL_FIREBOLT);
	}
	UpdateMissileVelocity(missile, dst, sp);
	SetMissDir(missile, GetDirection16(missile.position.start, dst));
	missile._mirange = 256;
	missile.var1 = missile.position.start.x;
	missile.var2 = missile.position.start.y;
	missile._mlid = AddLight(missile.position.start, 8);
}

void AddMagmaball(Missile &missile, const AddMissileParameter &parameter)
{
	UpdateMissileVelocity(missile, parameter.dst, 16);
	missile.position.traveled.deltaX += 3 * missile.position.velocity.deltaX;
	missile.position.traveled.deltaY += 3 * missile.position.velocity.deltaY;
	UpdateMissilePos(missile);
	if (!gbIsHellfire || (missile.position.velocity.deltaX & 0xFFFF0000) != 0 || (missile.position.velocity.deltaY & 0xFFFF0000) != 0)
		missile._mirange = 256;
	else
		missile._mirange = 1;
	missile.var1 = missile.position.start.x;
	missile.var2 = missile.position.start.y;
	missile._mlid = AddLight(missile.position.start, 8);
}

void AddTeleport(Missile &missile, const AddMissileParameter &parameter)
{
	std::optional<Point> teleportDestination = FindClosestValidPosition(
	    [&player = Players[missile._misource]](Point target) {
		    return PosOkPlayer(player, target);
	    },
	    parameter.dst, 0, 5);

	if (teleportDestination) {
		missile.position.tile = *teleportDestination;
		missile.position.start = *teleportDestination;
		UseMana(missile._misource, SPL_TELEPORT);
		missile._mirange = 2;
	} else {
		missile._miDelFlag = true;
	}
}

void AddLightball(Missile &missile, const AddMissileParameter &parameter)
{
	UpdateMissileVelocity(missile, parameter.dst, 16);
	missile._miAnimFrame = GenerateRnd(8) + 1;
	missile._mirange = 255;
	const Point position { missile._misource < 0 ? missile.position.start : Players[missile._misource].position.tile };
	missile.var1 = position.x;
	missile.var2 = position.y;
}

void AddFirewall(Missile &missile, const AddMissileParameter &parameter)
{
	missile._midam = GenerateRndSum(10, 2) + 2;
	missile._midam += missile._misource >= 0 ? Players[missile._misource]._pLevel : currlevel; // BUGFIX: missing parenthesis around ternary (fixed)
	missile._midam <<= 3;
	UpdateMissileVelocity(missile, parameter.dst, 16);
	int i = missile._mispllvl;
	missile._mirange = 10;
	if (i > 0)
		missile._mirange *= i + 1;
	if (missile._micaster == TARGET_PLAYERS || missile._misource < 0)
		missile._mirange += currlevel;
	else
		missile._mirange += (Players[missile._misource]._pISplDur * missile._mirange) / 128;
	missile._mirange *= 16;
	missile.var1 = missile._mirange - missile._miAnimLen;
}

void AddFireball(Missile &missile, const AddMissileParameter &parameter)
{
	Point dst = parameter.dst;
	if (missile.position.start == dst) {
		dst += parameter.midir;
	}
	int sp = 16;
	if (missile._micaster == TARGET_MONSTERS) {
		sp += std::min(missile._mispllvl * 2, 34);

		int dmg = 2 * (Players[missile._misource]._pLevel + GenerateRndSum(10, 2)) + 4;
		missile._midam = ScaleSpellEffect(dmg, missile._mispllvl);

		UseMana(missile._misource, SPL_FIREBALL);
	}
	UpdateMissileVelocity(missile, dst, sp);
	SetMissDir(missile, GetDirection16(missile.position.start, dst));
	missile._mirange = 256;
	missile.var1 = missile.position.start.x;
	missile.var2 = missile.position.start.y;
	missile._mlid = AddLight(missile.position.start, 8);
}

void AddLightctrl(Missile &missile, const AddMissileParameter &parameter)
{
	if (missile._midam == 0 && missile._micaster == TARGET_MONSTERS)
		UseMana(missile._misource, SPL_LIGHTNING);
	missile.var1 = missile.position.start.x;
	missile.var2 = missile.position.start.y;
	UpdateMissileVelocity(missile, parameter.dst, 32);
	missile._miAnimFrame = GenerateRnd(8) + 1;
	missile._mirange = 256;
}

void AddLightning(Missile &missile, const AddMissileParameter &parameter)
{
	missile.position.start = parameter.dst;

	missile._miAnimFrame = GenerateRnd(8) + 1;

	if (missile._micaster == TARGET_PLAYERS || missile.IsTrap()) {
		if (missile.IsTrap() || Monsters[missile._misource].MType->mtype == MT_FAMILIAR)
			missile._mirange = 8;
		else
			missile._mirange = 10;
	} else {
		missile._mirange = (missile._mispllvl / 2) + 6;
	}
	missile._mlid = AddLight(missile.position.tile, 4);
}

void AddMisexp(Missile &missile, const AddMissileParameter &parameter)
{
	if (missile._micaster != TARGET_MONSTERS && missile._misource >= 0) {
		switch (Monsters[missile._misource].MType->mtype) {
		case MT_SUCCUBUS:
			SetMissAnim(missile, MFILE_FLAREEXP);
			break;
		case MT_SNOWWICH:
			SetMissAnim(missile, MFILE_SCBSEXPB);
			break;
		case MT_HLSPWN:
			SetMissAnim(missile, MFILE_SCBSEXPD);
			break;
		case MT_SOLBRNR:
			SetMissAnim(missile, MFILE_SCBSEXPC);
			break;
		default:
			break;
		}
	}

	assert(parameter.pParent != nullptr); // AddMisexp will always be called with a parent associated to the missile.
	auto &parent = *parameter.pParent;
	missile.position.tile = parent.position.tile;
	missile.position.start = parent.position.start;
	missile.position.offset = parent.position.offset;
	missile.position.traveled = parent.position.traveled;
	missile._mirange = missile._miAnimLen;
}

void AddWeapexp(Missile &missile, const AddMissileParameter &parameter)
{
	missile.var2 = parameter.dst.x;
	if (parameter.dst.x == 1)
		SetMissAnim(missile, MFILE_MAGBLOS);
	else
		SetMissAnim(missile, MFILE_MINILTNG);
	missile._mirange = missile._miAnimLen - 1;
}

void AddTown(Missile &missile, const AddMissileParameter &parameter)
{
	if (currlevel == 0) {
		missile.position.tile = parameter.dst;
		missile.position.start = parameter.dst;
	} else {
		std::optional<Point> targetPosition = FindClosestValidPosition(
		    [](Point target) {
			    if (!InDungeonBounds(target)) {
				    return false;
			    }
			    if (IsObjectAtPosition(target)) {
				    return false;
			    }
			    if (dPlayer[target.x][target.y] != 0) {
				    return false;
			    }
			    if (TileContainsMissile(target)) {
				    return false;
			    }

			    int dp = dPiece[target.x][target.y];
			    if (nSolidTable[dp] || nMissileTable[dp]) {
				    return false;
			    }
			    return !CheckIfTrig(target);
		    },
		    parameter.dst, 0, 5);

		if (targetPosition) {
			missile.position.tile = *targetPosition;
			missile.position.start = *targetPosition;
			missile._miDelFlag = false;
		} else {
			missile._miDelFlag = true;
		}
	}

	missile._mirange = 100;
	missile.var1 = missile._mirange - missile._miAnimLen;
	for (auto &other : Missiles) {
		if (other._mitype == MIS_TOWN && &other != &missile && other._misource == missile._misource)
			other._mirange = 0;
	}
	PutMissile(missile);
	if (missile._misource == MyPlayerId && !missile._miDelFlag && currlevel != 0) {
		if (!setlevel) {
			NetSendCmdLocParam3(true, CMD_ACTIVATEPORTAL, missile.position.tile, currlevel, leveltype, 0);
		} else {
			NetSendCmdLocParam3(true, CMD_ACTIVATEPORTAL, missile.position.tile, setlvlnum, leveltype, 1);
		}
	}
}

void AddFlash(Missile &missile, const AddMissileParameter & /*parameter*/)
{
	if (!missile.IsTrap()) {
		if (missile._micaster == TARGET_MONSTERS) {
			int dmg = GenerateRndSum(20, Players[missile._misource]._pLevel + 1) + Players[missile._misource]._pLevel + 1;
			missile._midam = ScaleSpellEffect(dmg, missile._mispllvl);
			missile._midam += missile._midam / 2;
			UseMana(missile._misource, SPL_FLASH);
		} else {
			missile._midam = Monsters[missile._misource].mLevel * 2;
		}
	} else {
		missile._midam = currlevel / 2;
	}
	missile._mirange = 19;
}

void AddFlash2(Missile &missile, const AddMissileParameter & /*parameter*/)
{
	if (missile._micaster == TARGET_MONSTERS) {
		if (!missile.IsTrap()) {
			int dmg = Players[missile._misource]._pLevel + 1;
			dmg += GenerateRndSum(20, dmg);
			missile._midam = ScaleSpellEffect(dmg, missile._mispllvl);
			missile._midam += missile._midam / 2;
		} else {
			missile._midam = currlevel / 2;
		}
	}
	missile._miPreFlag = true;
	missile._mirange = 19;
}

void AddManashield(Missile &missile, const AddMissileParameter & /*parameter*/)
{
	missile._miDelFlag = true;

	if (missile._misource < 0)
		return;

	Player &player = Players[missile._misource];

	if (player.pManaShield)
		return;

	player.pManaShield = true;
	if (missile._misource == MyPlayerId)
		NetSendCmd(true, CMD_SETSHIELD);

	if (missile._micaster == TARGET_MONSTERS)
		UseMana(missile._misource, SPL_MANASHIELD);
}

void AddFiremove(Missile &missile, const AddMissileParameter &parameter)
{
	missile._midam = GenerateRnd(10) + Players[missile._misource]._pLevel + 1;
	UpdateMissileVelocity(missile, parameter.dst, 16);
	missile._mirange = 255;
	missile.position.tile += Displacement { 1, 1 };
	missile.position.offset.deltaY -= 32;
}

void AddGuardian(Missile &missile, const AddMissileParameter &parameter)
{
	Player &player = Players[missile._misource];

	int dmg = GenerateRnd(10) + (player._pLevel / 2) + 1;
	missile._midam = ScaleSpellEffect(dmg, missile._mispllvl);

	std::optional<Point> spawnPosition = FindClosestValidPosition(
	    [start = missile.position.start](Point target) {
		    if (!InDungeonBounds(target)) {
			    return false;
		    }
		    if (dMonster[target.x][target.y] != 0) {
			    return false;
		    }
		    if (IsObjectAtPosition(target)) {
			    return false;
		    }
		    if (TileContainsMissile(target)) {
			    return false;
		    }

		    int dp = dPiece[target.x][target.y];
		    if (nSolidTable[dp] || nMissileTable[dp]) {
			    return false;
		    }

		    return LineClearMissile(start, target);
	    },
	    parameter.dst, 0, 5);

	if (!spawnPosition) {
		missile._miDelFlag = true;
		return;
	}

	missile._miDelFlag = false;
	missile.position.tile = *spawnPosition;
	missile.position.start = *spawnPosition;
	UseMana(missile._misource, SPL_GUARDIAN);

	missile._mlid = AddLight(missile.position.tile, 1);
	missile._mirange = missile._mispllvl + (player._pLevel / 2);
	missile._mirange += (missile._mirange * player._pISplDur) / 128;

	if (missile._mirange > 30)
		missile._mirange = 30;
	missile._mirange <<= 4;
	if (missile._mirange < 30)
		missile._mirange = 30;

	missile.var1 = missile._mirange - missile._miAnimLen;
	missile.var3 = 1;
}

void AddChain(Missile &missile, const AddMissileParameter &parameter)
{
	missile.var1 = parameter.dst.x;
	missile.var2 = parameter.dst.y;
	missile._mirange = 1;
	UseMana(missile._misource, SPL_CHAIN);
}

namespace {
void InitMissileAnimationFromMonster(Missile &mis, Direction midir, const Monster &mon, MonsterGraphic graphic)
{
	const AnimStruct &anim = mon.MType->GetAnimData(graphic);
	mis._mimfnum = static_cast<int32_t>(midir);
	mis._miAnimFlags = MissileDataFlags::None;
	CelSprite celSprite = *anim.GetCelSpritesForDirection(midir);
	mis._miAnimData = celSprite.Data();
	mis._miAnimDelay = anim.Rate;
	mis._miAnimLen = anim.Frames;
	mis._miAnimWidth = celSprite.Width();
	mis._miAnimWidth2 = CalculateWidth2(celSprite.Width());
	mis._miAnimAdd = 1;
	mis.var1 = 0;
	mis.var2 = 0;
	mis._miLightFlag = true;
	mis._mirange = 256;
}
} // namespace

void AddRhino(Missile &missile, const AddMissileParameter &parameter)
{
	Monster &monster = Monsters[missile._misource];

	MonsterGraphic graphic = MonsterGraphic::Walk;
	if (IsAnyOf(monster.MType->mtype, MT_HORNED, MT_MUDRUN, MT_FROSTC, MT_OBLORD)) {
		graphic = MonsterGraphic::Special;
	} else if (IsAnyOf(monster.MType->mtype, MT_NSNAKE, MT_RSNAKE, MT_BSNAKE, MT_GSNAKE)) {
		graphic = MonsterGraphic::Attack;
	}
	UpdateMissileVelocity(missile, parameter.dst, 18);
	InitMissileAnimationFromMonster(missile, parameter.midir, monster, graphic);
	if (IsAnyOf(monster.MType->mtype, MT_NSNAKE, MT_RSNAKE, MT_BSNAKE, MT_GSNAKE))
		missile._miAnimFrame = 7;
	if (monster._uniqtype != 0) {
		missile._mlid = monster.mlid;
	}
	PutMissile(missile);
}

void AddFlare(Missile &missile, const AddMissileParameter &parameter)
{
	Point dst = parameter.dst;
	if (missile.position.start == dst) {
		dst += parameter.midir;
	}
	UpdateMissileVelocity(missile, dst, 16);
	missile._mirange = 256;
	missile.var1 = missile.position.start.x;
	missile.var2 = missile.position.start.y;
	missile._mlid = AddLight(missile.position.start, 8);
	if (missile._micaster == TARGET_MONSTERS) {
		UseMana(missile._misource, SPL_FLARE);
		ApplyPlrDamage(missile._misource, 5);
	} else if (missile._misource > 0) {
		auto &monster = Monsters[missile._misource];
		if (monster.MType->mtype == MT_SUCCUBUS)
			SetMissAnim(missile, MFILE_FLARE);
		if (monster.MType->mtype == MT_SNOWWICH)
			SetMissAnim(missile, MFILE_SCUBMISB);
		if (monster.MType->mtype == MT_HLSPWN)
			SetMissAnim(missile, MFILE_SCUBMISD);
		if (monster.MType->mtype == MT_SOLBRNR)
			SetMissAnim(missile, MFILE_SCUBMISC);
	}

	if (MissileSpriteData[missile._miAnimType].animFAmt == 16) {
		SetMissDir(missile, GetDirection16(missile.position.start, dst));
	}
}

void AddAcid(Missile &missile, const AddMissileParameter &parameter)
{
	UpdateMissileVelocity(missile, parameter.dst, 16);
	SetMissDir(missile, GetDirection16(missile.position.start, parameter.dst));
	if (!gbIsHellfire || (missile.position.velocity.deltaX & 0xFFFF0000) != 0 || (missile.position.velocity.deltaY & 0xFFFF0000) != 0)
		missile._mirange = 5 * (Monsters[missile._misource]._mint + 4);
	else
		missile._mirange = 1;
	missile._mlid = NO_LIGHT;
	missile.var1 = missile.position.start.x;
	missile.var2 = missile.position.start.y;
	PutMissile(missile);
}

void AddAcidpud(Missile &missile, const AddMissileParameter & /*parameter*/)
{
	missile._miLightFlag = true;
	int monst = missile._misource;
	missile._mirange = GenerateRnd(15) + 40 * (Monsters[monst]._mint + 1);
	missile._miPreFlag = true;
}

void AddStone(Missile &missile, const AddMissileParameter &parameter)
{
	std::optional<Point> targetMonsterPosition = FindClosestValidPosition(
	    [](Point target) {
		    if (!InDungeonBounds(target)) {
			    return false;
		    }

		    int monsterId = abs(dMonster[target.x][target.y]) - 1;
		    if (monsterId < 0) {
			    return false;
		    }

		    auto &monster = Monsters[monsterId];

		    if (IsAnyOf(monster.MType->mtype, MT_GOLEM, MT_DIABLO, MT_NAKRUL)) {
			    return false;
		    }
		    if (IsAnyOf(monster._mmode, MonsterMode::FadeIn, MonsterMode::FadeOut, MonsterMode::Charge)) {
			    return false;
		    }

		    return true;
	    },
	    parameter.dst, 0, 5);

	if (!targetMonsterPosition) {
		missile._miDelFlag = true;
		return;
	}

	// Petrify the targeted monster
	int monsterId = abs(dMonster[targetMonsterPosition->x][targetMonsterPosition->y]) - 1;
	auto &monster = Monsters[monsterId];
	missile.var1 = static_cast<int>(monster._mmode);
	missile.var2 = monsterId;
	monster.Petrify();

	// And set up the missile to unpetrify it in the future
	missile.position.tile = *targetMonsterPosition;
	missile.position.start = missile.position.tile;
	missile._mirange = missile._mispllvl + 6;
	missile._mirange += (missile._mirange * Players[missile._misource]._pISplDur) / 128;

	if (missile._mirange > 15)
		missile._mirange = 15;
	missile._mirange <<= 4;
	UseMana(missile._misource, SPL_STONE);
}

void AddGolem(Missile &missile, const AddMissileParameter &parameter)
{
	missile._miDelFlag = true;

	int playerId = missile._misource;

	for (auto &other : Missiles) {
		if (other._mitype == MIS_GOLEM && &other != &missile && other._misource == playerId) {
			return;
		}
	}
	if (Monsters[playerId].position.tile != GolemHoldingCell && playerId == MyPlayerId)
		M_StartKill(playerId, playerId);

	UseMana(playerId, SPL_GOLEM);

	if (Monsters[playerId].position.tile == GolemHoldingCell) {
		std::optional<Point> spawnPosition = FindClosestValidPosition(
		    [start = missile.position.start](Point target) {
			    return !IsTileOccupied(target) && LineClearMissile(start, target);
		    },
		    parameter.dst, 0, 5);

		if (spawnPosition) {
			SpawnGolem(playerId, *spawnPosition, missile);
		}
	}
}

void AddBoom(Missile &missile, const AddMissileParameter &parameter)
{
	missile.position.tile = parameter.dst;
	missile.position.start = parameter.dst;
	missile._mirange = missile._miAnimLen;
}

void AddHeal(Missile &missile, const AddMissileParameter & /*parameter*/)
{
	Player &player = Players[missile._misource];

	int hp = GenerateRnd(10) + 1;
	hp += GenerateRndSum(4, player._pLevel) + player._pLevel;
	hp += GenerateRndSum(6, missile._mispllvl) + missile._mispllvl;
	hp <<= 6;

	if (player._pClass == HeroClass::Warrior || player._pClass == HeroClass::Barbarian || player._pClass == HeroClass::Monk) {
		hp *= 2;
	} else if (player._pClass == HeroClass::Rogue || player._pClass == HeroClass::Bard) {
		hp += hp / 2;
	}

	player._pHitPoints = std::min(player._pHitPoints + hp, player._pMaxHP);
	player._pHPBase = std::min(player._pHPBase + hp, player._pMaxHPBase);

	UseMana(missile._misource, SPL_HEAL);
	missile._miDelFlag = true;
	drawhpflag = true;
}

void AddHealOther(Missile &missile, const AddMissileParameter & /*parameter*/)
{
	missile._miDelFlag = true;
	UseMana(missile._misource, SPL_HEALOTHER);
	if (missile._misource == MyPlayerId) {
		NewCursor(CURSOR_HEALOTHER);
		if (ControlMode != ControlTypes::KeyboardAndMouse)
			TryIconCurs();
	}
}

void AddElement(Missile &missile, const AddMissileParameter &parameter)
{
	Point dst = parameter.dst;
	if (missile.position.start == dst) {
		dst += parameter.midir;
	}

	int dmg = 2 * (Players[missile._misource]._pLevel + GenerateRndSum(10, 2)) + 4;
	missile._midam = ScaleSpellEffect(dmg, missile._mispllvl) / 2;

	UpdateMissileVelocity(missile, dst, 16);
	SetMissDir(missile, GetDirection(missile.position.start, dst));
	missile._mirange = 256;
	missile.var1 = missile.position.start.x;
	missile.var2 = missile.position.start.y;
	missile.var4 = dst.x;
	missile.var5 = dst.y;
	missile._mlid = AddLight(missile.position.start, 8);
	UseMana(missile._misource, SPL_ELEMENT);
}

extern void FocusOnInventory();

void AddIdentify(Missile &missile, const AddMissileParameter & /*parameter*/)
{
	missile._miDelFlag = true;
	UseMana(missile._misource, SPL_IDENTIFY);
	if (missile._misource == MyPlayerId) {
		if (sbookflag)
			sbookflag = false;
		if (!invflag) {
			invflag = true;
			if (ControlMode != ControlTypes::KeyboardAndMouse)
				FocusOnInventory();
		}
		NewCursor(CURSOR_IDENTIFY);
	}
}

void AddFirewallC(Missile &missile, const AddMissileParameter &parameter)
{
	std::optional<Point> spreadPosition = FindClosestValidPosition(
	    [start = missile.position.start](Point target) {
		    return start != target && IsTileNotSolid(target) && !IsObjectAtPosition(target) && LineClearMissile(start, target);
	    },
	    parameter.dst, 0, 5);

	if (!spreadPosition) {
		missile._miDelFlag = true;
		return;
	}

	missile._miDelFlag = false;
	missile.var1 = spreadPosition->x;
	missile.var2 = spreadPosition->y;
	missile.var5 = spreadPosition->x;
	missile.var6 = spreadPosition->y;
	missile.var3 = static_cast<int>(Left(Left(parameter.midir)));
	missile.var4 = static_cast<int>(Right(Right(parameter.midir)));
	missile._mirange = 7;
	UseMana(missile._misource, SPL_FIREWALL);
}

void AddInfra(Missile &missile, const AddMissileParameter & /*parameter*/)
{
	missile._mirange = ScaleSpellEffect(1584, missile._mispllvl);
	missile._mirange += missile._mirange * Players[missile._misource]._pISplDur / 128;

	if (missile._micaster == TARGET_MONSTERS)
		UseMana(missile._misource, SPL_INFRA);
}

void AddWave(Missile &missile, const AddMissileParameter &parameter)
{
	missile.var1 = parameter.dst.x;
	missile.var2 = parameter.dst.y;
	missile._mirange = 1;
	missile._miAnimFrame = 4;
	UseMana(missile._misource, SPL_WAVE);
}

void AddNova(Missile &missile, const AddMissileParameter &parameter)
{
	missile.var1 = parameter.dst.x;
	missile.var2 = parameter.dst.y;

	if (!missile.IsTrap()) {
		int dmg = GenerateRndSum(6, 5) + Players[missile._misource]._pLevel + 5;
		missile._midam = ScaleSpellEffect(dmg / 2, missile._mispllvl);

		if (missile._micaster == TARGET_MONSTERS)
			UseMana(missile._misource, SPL_NOVA);
	} else {
		missile._midam = (currlevel / 2) + GenerateRndSum(3, 3);
	}

	missile._mirange = 1;
}

void AddBlodboil(Missile &missile, const AddMissileParameter & /*parameter*/)
{
	Player &player = Players[missile._misource];

	if (HasAnyOf(player._pSpellFlags, SpellFlag::RageActive | SpellFlag::RageCooldown) || player._pHitPoints <= player._pLevel << 6) {
		missile._miDelFlag = true;
		return;
	}

	UseMana(missile._misource, SPL_BLODBOIL);
	int tmp = 3 * player._pLevel;
	tmp <<= 7;
	player._pSpellFlags |= SpellFlag::RageActive;
	missile.var2 = tmp;
	int lvl = player._pLevel * 2;
	missile._mirange = lvl + 10 * missile._mispllvl + 245;
	CalcPlrItemVals(player, true);
	force_redraw = 255;
	player.Say(HeroSpeech::Aaaaargh);
}

void AddRepair(Missile &missile, const AddMissileParameter & /*parameter*/)
{
	missile._miDelFlag = true;
	UseMana(missile._misource, SPL_REPAIR);
	if (missile._misource == MyPlayerId) {
		if (sbookflag)
			sbookflag = false;
		if (!invflag) {
			invflag = true;
			if (ControlMode != ControlTypes::KeyboardAndMouse)
				FocusOnInventory();
		}
		NewCursor(CURSOR_REPAIR);
	}
}

void AddRecharge(Missile &missile, const AddMissileParameter & /*parameter*/)
{
	missile._miDelFlag = true;
	UseMana(missile._misource, SPL_RECHARGE);
	if (missile._misource == MyPlayerId) {
		if (sbookflag)
			sbookflag = false;
		if (!invflag) {
			invflag = true;
			if (ControlMode != ControlTypes::KeyboardAndMouse)
				FocusOnInventory();
		}
		NewCursor(CURSOR_RECHARGE);
	}
}

void AddDisarm(Missile &missile, const AddMissileParameter & /*parameter*/)
{
	missile._miDelFlag = true;
	UseMana(missile._misource, SPL_DISARM);
	if (missile._misource == MyPlayerId) {
		NewCursor(CURSOR_DISARM);
		if (ControlMode != ControlTypes::KeyboardAndMouse) {
			if (pcursobj != -1)
				NetSendCmdLocParam1(true, CMD_DISARMXY, cursPosition, pcursobj);
			else
				NewCursor(CURSOR_HAND);
		}
	}
}

void AddApoca(Missile &missile, const AddMissileParameter & /*parameter*/)
{
	missile.var1 = 8;
	missile.var2 = std::max(missile.position.start.y - 8, 1);
	missile.var3 = std::min(missile.position.start.y + 8, MAXDUNY - 1);
	missile.var4 = std::max(missile.position.start.x - 8, 1);
	missile.var5 = std::min(missile.position.start.x + 8, MAXDUNX - 1);
	missile.var6 = missile.var4;
	int playerLevel = Players[missile._misource]._pLevel;
	missile._midam = GenerateRndSum(6, playerLevel) + playerLevel;
	missile._mirange = 255;
	UseMana(missile._misource, SPL_APOCA);
}

void AddFlame(Missile &missile, const AddMissileParameter &parameter)
{
	missile.var2 = 5 * missile._midam;
	missile.position.start = parameter.dst;

	missile._mirange = missile.var2 + 20;
	missile._mlid = AddLight(missile.position.start, 1);
	if (missile._micaster == TARGET_MONSTERS) {
		int i = GenerateRnd(Players[missile._misource]._pLevel) + GenerateRnd(2);
		missile._midam = 8 * i + 16 + ((8 * i + 16) / 2);
	} else {
		auto &monster = Monsters[missile._misource];
		missile._midam = monster.mMinDamage + GenerateRnd(monster.mMaxDamage - monster.mMinDamage + 1);
	}
}

void AddFlamec(Missile &missile, const AddMissileParameter &parameter)
{
	Point dst = parameter.dst;
	if (missile.position.start == parameter.dst) {
		dst += parameter.midir;
	}
	UpdateMissileVelocity(missile, dst, 32);
	if (missile._micaster == TARGET_MONSTERS) {
		UseMana(missile._misource, SPL_FLAME);
	}
	missile.var1 = missile.position.start.x;
	missile.var2 = missile.position.start.y;
	missile._mirange = 256;
}

void AddCbolt(Missile &missile, const AddMissileParameter &parameter)
{
	Point dst = parameter.dst;
	missile._mirnd = GenerateRnd(15) + 1;
	missile._midam = (missile._micaster == TARGET_MONSTERS) ? (GenerateRnd(Players[missile._misource]._pMagic / 4) + 1) : 15;

	if (missile.position.start == dst) {
		dst += parameter.midir;
	}
	missile._miAnimFrame = GenerateRnd(8) + 1;
	missile._mlid = AddLight(missile.position.start, 5);

	UpdateMissileVelocity(missile, dst, 8);
	missile.var1 = 5;
	missile.var2 = static_cast<int>(parameter.midir);
	missile._mirange = 256;
}

void AddHbolt(Missile &missile, const AddMissileParameter &parameter)
{
	Point dst = parameter.dst;
	if (missile.position.start == dst) {
		dst += parameter.midir;
	}
	int sp = 16;
	if (!missile.IsTrap()) {
		sp += std::min(missile._mispllvl * 2, 47);
	}

	UpdateMissileVelocity(missile, dst, sp);
	SetMissDir(missile, GetDirection16(missile.position.start, dst));
	missile._mirange = 256;
	missile.var1 = missile.position.start.x;
	missile.var2 = missile.position.start.y;
	missile._mlid = AddLight(missile.position.start, 8);
	missile._midam = GenerateRnd(10) + Players[missile._misource]._pLevel + 9;
	UseMana(missile._misource, SPL_HBOLT);
}

void AddResurrect(Missile &missile, const AddMissileParameter & /*parameter*/)
{
	UseMana(missile._misource, SPL_RESURRECT);
	if (missile._misource == MyPlayerId) {
		NewCursor(CURSOR_RESURRECT);
		if (ControlMode != ControlTypes::KeyboardAndMouse)
			TryIconCurs();
	}
	missile._miDelFlag = true;
}

void AddResurrectBeam(Missile &missile, const AddMissileParameter &parameter)
{
	missile.position.tile = parameter.dst;
	missile.position.start = parameter.dst;
	missile._mirange = MissileSpriteData[MFILE_RESSUR1].animLen[0];
}

void AddTelekinesis(Missile &missile, const AddMissileParameter & /*parameter*/)
{
	missile._miDelFlag = true;
	UseMana(missile._misource, SPL_TELEKINESIS);
	if (missile._misource == MyPlayerId)
		NewCursor(CURSOR_TELEKINESIS);
}

void AddBoneSpirit(Missile &missile, const AddMissileParameter &parameter)
{
	Point dst = parameter.dst;
	if (missile.position.start == parameter.dst) {
		dst += parameter.midir;
	}
	UpdateMissileVelocity(missile, dst, 16);
	SetMissDir(missile, GetDirection(missile.position.start, dst));
	missile._mirange = 256;
	missile.var1 = missile.position.start.x;
	missile.var2 = missile.position.start.y;
	missile.var4 = dst.x;
	missile.var5 = dst.y;
	missile._mlid = AddLight(missile.position.start, 8);
	if (missile._micaster == TARGET_MONSTERS) {
		UseMana(missile._misource, SPL_BONESPIRIT);
		ApplyPlrDamage(missile._misource, 6);
	}
}

void AddRportal(Missile &missile, const AddMissileParameter & /*parameter*/)
{
	missile._mirange = 100;
	missile.var1 = 100 - missile._miAnimLen;
	PutMissile(missile);
}

void AddDiabApoca(Missile &missile, const AddMissileParameter & /*parameter*/)
{
	int players = gbIsMultiplayer ? MAX_PLRS : 1;
	for (int pnum = 0; pnum < players; pnum++) {
		Player &player = Players[pnum];
		if (!player.plractive)
			continue;
		if (!LineClearMissile(missile.position.start, player.position.future))
			continue;

		AddMissile({ 0, 0 }, player.position.future, Direction::South, MIS_BOOM2, missile._micaster, missile._misource, missile._midam, 0);
	}
	missile._miDelFlag = true;
}

Missile *AddMissile(Point src, Point dst, Direction midir, missile_id mitype, mienemy_type micaster, int id, int midam, int spllvl, Missile *pParent /*= nullptr*/)
{
	if (Missiles.size() >= Missiles.max_size()) {
		return nullptr;
	}

	Missiles.emplace_back(Missile {});
	auto &missile = Missiles.back();

	const auto &missileData = MissilesData[mitype];

	missile._mitype = mitype;
	missile._micaster = micaster;
	missile._misource = id;
	missile._midam = midam;
	missile._mispllvl = spllvl;
	missile.position.tile = src;
	missile.position.start = src;
	missile._miAnimAdd = 1;
	missile._miAnimType = missileData.mFileNum;
	missile._miDrawFlag = missileData.mDraw;
	missile._mlid = NO_LIGHT;
	missile.lastCollisionTargetHash = 0;

	if (!missile.IsTrap() && micaster == TARGET_PLAYERS) {
		Monster &monster = Monsters[id];
		if (monster._uniqtype != 0) {
			missile._miUniqTrans = monster._uniqtrans + 1;
		}
	}

	if (missile._miAnimType == MFILE_NONE || MissileSpriteData[missile._miAnimType].animFAmt < 8)
		SetMissDir(missile, 0);
	else
		SetMissDir(missile, midir);

	if (missileData.mlSFX != SFX_NONE) {
		PlaySfxLoc(missileData.mlSFX, missile.position.start);
	}

	AddMissileParameter parameter = { dst, midir, pParent };
	missileData.mAddProc(missile, parameter);

	return &missile;
}

void MI_LArrow(Missile &missile)
{
	missile._mirange--;
	if (missile._miAnimType == MFILE_MINILTNG || missile._miAnimType == MFILE_MAGBLOS) {
		ChangeLight(missile._mlid, missile.position.tile, missile._miAnimFrame + 5);
	} else {
		int mind;
		int maxd;
		int p = missile._misource;
		missile._midist++;
		if (!missile.IsTrap()) {
			if (missile._micaster == TARGET_MONSTERS) {
				Player &player = Players[p];
				mind = player._pIMinDam;
				maxd = player._pIMaxDam;
			} else {
				Monster &monster = Monsters[p];
				mind = monster.mMinDamage;
				maxd = monster.mMaxDamage;
			}
		} else {
			mind = GenerateRnd(10) + 1 + currlevel;
			maxd = GenerateRnd(10) + 1 + currlevel * 2;
		}
		missile_resistance rst = MissilesData[missile._mitype].mResist;
		MissilesData[missile._mitype].mResist = MISR_NONE;
		MoveMissileAndCheckMissileCol(missile, mind, maxd, true, false);
		MissilesData[missile._mitype].mResist = rst;
		if (missile._mirange == 0) {
			missile._mimfnum = 0;
			missile._mirange = missile._miAnimLen - 1;
			missile.position.StopMissile();

			rst = MissilesData[missile._mitype].mResist;

			int eMind;
			int eMaxd;
			missile_graphic_id eAnim;
			missile_resistance eRst;
			switch (missile._mitype) {
			case MIS_LARROW:
				if (!missile.IsTrap()) {
					Player &player = Players[p];
					eMind = player._pILMinDam;
					eMaxd = player._pILMaxDam;
				} else {
					eMind = GenerateRnd(10) + 1 + currlevel;
					eMaxd = GenerateRnd(10) + 1 + currlevel * 2;
				}
				eAnim = MFILE_MINILTNG;
				eRst = MISR_LIGHTNING;
				break;
			case MIS_FARROW:
				if (!missile.IsTrap()) {
					Player &player = Players[p];
					eMind = player._pIFMinDam;
					eMaxd = player._pIFMaxDam;
				} else {
					eMind = GenerateRnd(10) + 1 + currlevel;
					eMaxd = GenerateRnd(10) + 1 + currlevel * 2;
				}
				eAnim = MFILE_MAGBLOS;
				eRst = MISR_FIRE;
				break;
			default:
				break;
			}
			SetMissAnim(missile, eAnim);
			MissilesData[missile._mitype].mResist = eRst;
			CheckMissileCol(missile, eMind, eMaxd, false, missile.position.tile, true);
			MissilesData[missile._mitype].mResist = rst;
		} else {
			if (missile.position.tile != Point { missile.var1, missile.var2 }) {
				missile.var1 = missile.position.tile.x;
				missile.var2 = missile.position.tile.y;
				ChangeLight(missile._mlid, missile.position.tile, 5);
			}
		}
	}
	if (missile._mirange == 0) {
		missile._miDelFlag = true;
		AddUnLight(missile._mlid);
	}
	PutMissile(missile);
}

void MI_Arrow(Missile &missile)
{
	missile._mirange--;
	missile._midist++;
	int p = missile._misource;

	int mind;
	int maxd;
	if (!missile.IsTrap()) {
		if (missile._micaster == TARGET_MONSTERS) {
			Player &player = Players[p];
			mind = player._pIMinDam;
			maxd = player._pIMaxDam;
		} else {
			auto &monster = Monsters[p];
			mind = monster.mMinDamage;
			maxd = monster.mMaxDamage;
		}
	} else {
		mind = currlevel;
		maxd = 2 * currlevel;
	}
	MoveMissileAndCheckMissileCol(missile, mind, maxd, true, false);
	if (missile._mirange == 0)
		missile._miDelFlag = true;
	PutMissile(missile);
}

void MI_Firebolt(Missile &missile)
{
	int d = 0;

	missile._mirange--;
	if (missile._mitype != MIS_BONESPIRIT || missile._mimfnum != 8) {
		int p = missile._misource;
		if (!missile.IsTrap()) {
			if (missile._micaster == TARGET_MONSTERS) {
				Player &player = Players[p];
				switch (missile._mitype) {
				case MIS_FIREBOLT:
					d = GenerateRnd(10) + (player._pMagic / 8) + missile._mispllvl + 1;
					break;
				case MIS_FLARE:
					d = 3 * missile._mispllvl - (player._pMagic / 8) + (player._pMagic / 2);
					break;
				case MIS_BONESPIRIT:
					d = 0;
					break;
				default:
					break;
				}
			} else {
				auto &monster = Monsters[p];
				d = monster.mMinDamage + GenerateRnd(monster.mMaxDamage - monster.mMinDamage + 1);
			}
		} else {
			d = currlevel + GenerateRnd(2 * currlevel);
		}
		MoveMissileAndCheckMissileCol(missile, d, d, true, true);
		if (missile._mirange == 0) {
			missile._miDelFlag = true;
			Point dst = { 0, 0 };
			auto dir = static_cast<Direction>(missile._mimfnum);
			switch (missile._mitype) {
			case MIS_FIREBOLT:
			case MIS_MAGMABALL:
				AddMissile(missile.position.tile, dst, dir, MIS_MISEXP, missile._micaster, missile._misource, 0, 0, &missile);
				break;
			case MIS_FLARE:
				AddMissile(missile.position.tile, dst, dir, MIS_MISEXP2, missile._micaster, missile._misource, 0, 0, &missile);
				break;
			case MIS_ACID:
				AddMissile(missile.position.tile, dst, dir, MIS_MISEXP3, missile._micaster, missile._misource, 0, 0, &missile);
				break;
			case MIS_BONESPIRIT:
				SetMissDir(missile, 8);
				missile._mirange = 7;
				missile._miDelFlag = false;
				PutMissile(missile);
				return;
			case MIS_LICH:
				AddMissile(missile.position.tile, dst, dir, MIS_EXORA1, missile._micaster, missile._misource, 0, 0, &missile);
				break;
			case MIS_PSYCHORB:
				AddMissile(missile.position.tile, dst, dir, MIS_EXBL2, missile._micaster, missile._misource, 0, 0, &missile);
				break;
			case MIS_NECROMORB:
				AddMissile(missile.position.tile, dst, dir, MIS_EXRED3, missile._micaster, missile._misource, 0, 0, &missile);
				break;
			case MIS_ARCHLICH:
				AddMissile(missile.position.tile, dst, dir, MIS_EXYEL2, missile._micaster, missile._misource, 0, 0, &missile);
				break;
			case MIS_BONEDEMON:
				AddMissile(missile.position.tile, dst, dir, MIS_EXBL3, missile._micaster, missile._misource, 0, 0, &missile);
				break;
			default:
				break;
			}
			if (missile._mlid != NO_LIGHT)
				AddUnLight(missile._mlid);
			PutMissile(missile);
		} else {
			if (missile.position.tile != Point { missile.var1, missile.var2 }) {
				missile.var1 = missile.position.tile.x;
				missile.var2 = missile.position.tile.y;
				if (missile._mlid != NO_LIGHT)
					ChangeLight(missile._mlid, missile.position.tile, 8);
			}
			PutMissile(missile);
		}
	} else if (missile._mirange == 0) {
		if (missile._mlid != NO_LIGHT)
			AddUnLight(missile._mlid);
		missile._miDelFlag = true;
		PlaySfxLoc(LS_BSIMPCT, missile.position.tile);
		PutMissile(missile);
	} else
		PutMissile(missile);
}

void MI_Lightball(Missile &missile)
{
	Point targetPosition = { missile.var1, missile.var2 };
	missile._mirange--;
	int j = missile._mirange;
	MoveMissileAndCheckMissileCol(missile, missile._midam, missile._midam, false, false);
	if (missile._miHitFlag)
		missile._mirange = j;

	if (missile.position.tile == targetPosition) {
		Object *object = ObjectAtPosition(targetPosition);
		if (object != nullptr && object->IsShrine()) {
			missile._mirange = j;
		}
	}
	if (missile._mirange == 0)
		missile._miDelFlag = true;
	PutMissile(missile);
}

void MI_Acidpud(Missile &missile)
{
	missile._mirange--;
	int range = missile._mirange;
	CheckMissileCol(missile, missile._midam, missile._midam, true, missile.position.tile, false);
	missile._mirange = range;
	if (range == 0) {
		if (missile._mimfnum != 0) {
			missile._miDelFlag = true;
		} else {
			SetMissDir(missile, 1);
			missile._mirange = missile._miAnimLen;
		}
	}
	PutMissile(missile);
}

void MI_Firewall(Missile &missile)
{
	constexpr int ExpLight[14] = { 2, 3, 4, 5, 5, 6, 7, 8, 9, 10, 11, 12, 12 };

	missile._mirange--;
	if (missile._mirange == missile.var1) {
		SetMissDir(missile, 1);
		missile._miAnimFrame = GenerateRnd(11) + 1;
	}
	if (missile._mirange == missile._miAnimLen - 1) {
		SetMissDir(missile, 0);
		missile._miAnimFrame = 13;
		missile._miAnimAdd = -1;
	}
	CheckMissileCol(missile, missile._midam, missile._midam, true, missile.position.tile, true);
	if (missile._mirange == 0) {
		missile._miDelFlag = true;
		AddUnLight(missile._mlid);
	}
	if (missile._mimfnum != 0 && missile._mirange != 0 && missile._miAnimAdd != -1 && missile.var2 < 12) {
		if (missile.var2 == 0)
			missile._mlid = AddLight(missile.position.tile, ExpLight[0]);
		ChangeLight(missile._mlid, missile.position.tile, ExpLight[missile.var2]);
		missile.var2++;
	}
	PutMissile(missile);
}

void MI_Fireball(Missile &missile)
{
	missile._mirange--;

	int id = missile._misource;

	if (missile._miAnimType == MFILE_BIGEXP) {
		if (missile._mirange == 0) {
			missile._miDelFlag = true;
			AddUnLight(missile._mlid);
		}
	} else {
		int minDam = missile._midam;
		int maxDam = missile._midam;

		if (missile._micaster != TARGET_MONSTERS) {
			auto &monster = Monsters[id];
			minDam = monster.mMinDamage;
			maxDam = monster.mMaxDamage;
		}
		MoveMissileAndCheckMissileCol(missile, minDam, maxDam, true, false);
		if (missile._mirange == 0) {
			const Point missilePosition = missile.position.tile;
			ChangeLight(missile._mlid, missile.position.tile, missile._miAnimFrame);

			constexpr Displacement Offsets[] = { { 0, 0 }, { 0, 1 }, { 0, -1 }, { 1, 0 }, { 1, -1 }, { 1, 1 }, { -1, 0 }, { -1, 1 }, { -1, -1 } };
			for (Displacement offset : Offsets) {
				if (!CheckBlock(missile.position.start, missilePosition + offset))
					CheckMissileCol(missile, minDam, maxDam, false, missilePosition + offset, true);
			}

			if (!TransList[dTransVal[missilePosition.x][missilePosition.y]]
			    || (missile.position.velocity.deltaX < 0 && ((TransList[dTransVal[missilePosition.x][missilePosition.y + 1]] && nSolidTable[dPiece[missilePosition.x][missilePosition.y + 1]]) || (TransList[dTransVal[missilePosition.x][missilePosition.y - 1]] && nSolidTable[dPiece[missilePosition.x][missilePosition.y - 1]])))) {
				missile.position.tile += Displacement { 1, 1 };
				missile.position.offset.deltaY -= 32;
			}
			if (missile.position.velocity.deltaY > 0
			    && ((TransList[dTransVal[missilePosition.x + 1][missilePosition.y]] && nSolidTable[dPiece[missilePosition.x + 1][missilePosition.y]])
			        || (TransList[dTransVal[missilePosition.x - 1][missilePosition.y]] && nSolidTable[dPiece[missilePosition.x - 1][missilePosition.y]]))) {
				missile.position.offset.deltaY -= 32;
			}
			if (missile.position.velocity.deltaX > 0
			    && ((TransList[dTransVal[missilePosition.x][missilePosition.y + 1]] && nSolidTable[dPiece[missilePosition.x][missilePosition.y + 1]])
			        || (TransList[dTransVal[missilePosition.x][missilePosition.y - 1]] && nSolidTable[dPiece[missilePosition.x][missilePosition.y - 1]]))) {
				missile.position.offset.deltaX -= 32;
			}
			missile._mimfnum = 0;
			SetMissAnim(missile, MFILE_BIGEXP);
			missile._mirange = missile._miAnimLen - 1;
			missile.position.velocity = {};
		} else if (missile.position.tile != Point { missile.var1, missile.var2 }) {
			missile.var1 = missile.position.tile.x;
			missile.var2 = missile.position.tile.y;
			ChangeLight(missile._mlid, missile.position.tile, 8);
		}
	}

	PutMissile(missile);
}

void MI_HorkSpawn(Missile &missile)
{
	missile._mirange--;
	CheckMissileCol(missile, 0, 0, false, missile.position.tile, false);
	if (missile._mirange <= 0) {
		missile._miDelFlag = true;

		std::optional<Point> spawnPosition = FindClosestValidPosition(
		    [](Point target) {
			    return !IsTileOccupied(target);
		    },
		    missile.position.tile, 0, 1);

		if (spawnPosition) {
			auto facing = static_cast<Direction>(missile.var1);
			int monsterId = AddMonster(*spawnPosition, facing, 1, true);
			if (monsterId != -1) {
				M_StartStand(Monsters[monsterId], facing);
			}
		}
	} else {
		missile._midist++;
		missile.position.traveled += missile.position.velocity;
		UpdateMissilePos(missile);
	}
	PutMissile(missile);
}

void MI_Rune(Missile &missile)
{
	Point position = missile.position.tile;
	int mid = dMonster[position.x][position.y];
	int pid = dPlayer[position.x][position.y];
	if (mid != 0 || pid != 0) {
		Point targetPosition = mid != 0 ? Monsters[abs(mid) - 1].position.tile : Players[abs(pid) - 1].position.tile;
		Direction dir = GetDirection(position, targetPosition);

		missile._miDelFlag = true;
		AddUnLight(missile._mlid);

		AddMissile(position, position, dir, static_cast<missile_id>(missile.var1), TARGET_BOTH, missile._misource, missile._midam, missile._mispllvl);
	}

	PutMissile(missile);
}

void MI_LightningWall(Missile &missile)
{
	missile._mirange--;
	int range = missile._mirange;
	CheckMissileCol(missile, missile._midam, missile._midam, true, missile.position.tile, false);
	if (missile._miHitFlag)
		missile._mirange = range;
	if (missile._mirange == 0)
		missile._miDelFlag = true;
	PutMissile(missile);
}

void MI_HiveExplode(Missile &missile)
{
	missile._mirange--;
	if (missile._mirange <= 0) {
		missile._miDelFlag = true;
		AddUnLight(missile._mlid);
	}
	PutMissile(missile);
}

void MI_LightningArrow(Missile &missile)
{
	SpawnLightning(missile, missile._midam);
}

void MI_FireRing(Missile &missile)
{
	missile._miDelFlag = true;
	int8_t src = missile._misource;
	uint8_t lvl = missile._micaster == TARGET_MONSTERS ? Players[src]._pLevel : currlevel;
	int dmg = 16 * (GenerateRndSum(10, 2) + lvl + 2) / 2;

	int k = CrawlNum[3];
	int ck = k + 2;
	for (auto j = static_cast<uint8_t>(CrawlTable[k]); j > 0; j--, ck += 2) {
		Point target { missile.var1 + CrawlTable[ck - 1], missile.var2 + CrawlTable[ck] };
		if (!InDungeonBounds(target))
			continue;
		int dp = dPiece[target.x][target.y];
		if (nSolidTable[dp])
			continue;
		if (IsObjectAtPosition(target))
			continue;
		if (!LineClearMissile(missile.position.tile, target))
			continue;
		if (nMissileTable[dp] || missile.limitReached) {
			missile.limitReached = true;
			continue;
		}

		AddMissile(target, target, Direction::South, MIS_FIREWALL, TARGET_BOTH, src, dmg, missile._mispllvl);
	}
}

void MI_Search(Missile &missile)
{
	missile._mirange--;
	if (missile._mirange != 0)
		return;

	missile._miDelFlag = true;
	PlaySfxLoc(IS_CAST7, Players[missile._misource].position.tile);
	if (missile._misource == MyPlayerId)
		AutoMapShowItems = false;
}

void MI_LightningWallC(Missile &missile)
{
	missile._mirange--;
	if (missile._mirange == 0) {
		missile._miDelFlag = true;
		return;
	}

	int id = missile._misource;
	int lvl = !missile.IsTrap() ? Players[id]._pLevel : 0;
	int dmg = 16 * (GenerateRndSum(10, 2) + lvl + 2);

	{
		Point position = { missile.var1, missile.var2 };
		Point target = position + static_cast<Direction>(missile.var3);

		if (!missile.limitReached && GrowWall(id, position, target, MIS_LIGHTWALL, missile._mispllvl, dmg)) {
			missile.var1 = target.x;
			missile.var2 = target.y;
		} else {
			missile.limitReached = true;
		}
	}

	{
		Point position = { missile.var5, missile.var6 };
		Point target = position + static_cast<Direction>(missile.var4);

		if (missile.var7 == 0 && GrowWall(id, position, target, MIS_LIGHTWALL, missile._mispllvl, dmg)) {
			missile.var5 = target.x;
			missile.var6 = target.y;
		} else {
			missile.var7 = 1;
		}
	}
}

void MI_FireNova(Missile &missile)
{
	int sx1 = 0;
	int sy1 = 0;
	int id = missile._misource;
	int dam = missile._midam;
	Point src = missile.position.tile;
	Direction dir = Direction::South;
	mienemy_type en = TARGET_PLAYERS;
	if (!missile.IsTrap()) {
		dir = Players[id]._pdir;
		en = TARGET_MONSTERS;
	}
	for (const auto &k : VisionCrawlTable) {
		if (sx1 != k[6] || sy1 != k[7]) {
			Displacement offsets[] = { { k[6], k[7] }, { -k[6], -k[7] }, { -k[6], +k[7] }, { +k[6], -k[7] } };
			for (Displacement offset : offsets)
				AddMissile(src, src + offset, dir, MIS_FIRENOVA, en, id, dam, missile._mispllvl);
			sx1 = k[6];
			sy1 = k[7];
		}
	}
	missile._mirange--;
	if (missile._mirange == 0)
		missile._miDelFlag = true;
}

void MI_SpecArrow(Missile &missile)
{
	int id = missile._misource;
	int dam = missile._midam;
	Point src = missile.position.tile;
	Point dst = { missile.var1, missile.var2 };
	int spllvl = missile.var3;
	missile_id mitype = MIS_ARROW;
	Direction dir = Direction::South;
	mienemy_type micaster = TARGET_PLAYERS;
	if (!missile.IsTrap()) {
		Player &player = Players[id];
		dir = player._pdir;
		micaster = TARGET_MONSTERS;

		switch (player._pILMinDam) {
		case 0:
			mitype = MIS_FIRENOVA;
			break;
		case 1:
			mitype = MIS_LIGHTARROW;
			break;
		case 2:
			mitype = MIS_CBOLTARROW;
			break;
		case 3:
			mitype = MIS_HBOLTARROW;
			break;
		}
	}
	AddMissile(src, dst, dir, mitype, micaster, id, dam, spllvl);
	if (mitype == MIS_CBOLTARROW) {
		AddMissile(src, dst, dir, mitype, micaster, id, dam, spllvl);
		AddMissile(src, dst, dir, mitype, micaster, id, dam, spllvl);
	}
	missile._mirange--;
	if (missile._mirange == 0)
		missile._miDelFlag = true;
}

void MI_Lightctrl(Missile &missile)
{
	missile._mirange--;

	int dam;
	if (missile.IsTrap()) {
		dam = GenerateRnd(currlevel) + 2 * currlevel;
	} else if (missile._micaster == TARGET_MONSTERS) {
		dam = (GenerateRnd(2) + GenerateRnd(Players[missile._misource]._pLevel) + 2) << 6;
	} else {
		auto &monster = Monsters[missile._misource];
		dam = 2 * (monster.mMinDamage + GenerateRnd(monster.mMaxDamage - monster.mMinDamage + 1));
	}

	SpawnLightning(missile, dam);
}

void MI_Lightning(Missile &missile)
{
	missile._mirange--;
	int j = missile._mirange;
	if (missile.position.tile != missile.position.start)
		CheckMissileCol(missile, missile._midam, missile._midam, true, missile.position.tile, false);
	if (missile._miHitFlag)
		missile._mirange = j;
	if (missile._mirange == 0) {
		missile._miDelFlag = true;
		AddUnLight(missile._mlid);
	}
	PutMissile(missile);
}

void MI_Town(Missile &missile)
{
	int expLight[17] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15 };

	if (missile._mirange > 1)
		missile._mirange--;
	if (missile._mirange == missile.var1)
		SetMissDir(missile, 1);
	if (currlevel != 0 && missile._mimfnum != 1 && missile._mirange != 0) {
		if (missile.var2 == 0)
			missile._mlid = AddLight(missile.position.tile, 1);
		ChangeLight(missile._mlid, missile.position.tile, expLight[missile.var2]);
		missile.var2++;
	}

	for (int p = 0; p < MAX_PLRS; p++) {
		Player &player = Players[p];
		if (player.plractive && currlevel == player.plrlevel && !player._pLvlChanging && player._pmode == PM_STAND && player.position.tile == missile.position.tile) {
			ClrPlrPath(player);
			if (p == MyPlayerId) {
				NetSendCmdParam1(true, CMD_WARP, missile._misource);
				player._pmode = PM_NEWLVL;
			}
		}
	}

	if (missile._mirange == 0) {
		missile._miDelFlag = true;
		AddUnLight(missile._mlid);
	}
	PutMissile(missile);
}

void MI_Flash(Missile &missile)
{
	if (missile._micaster == TARGET_MONSTERS) {
		if (!missile.IsTrap())
			Players[missile._misource]._pInvincible = true;
	}
	missile._mirange--;

	constexpr Displacement Offsets[] = { { -1, 0 }, { 0, 0 }, { 1, 0 }, { -1, 1 }, { 0, 1 }, { 1, 1 } };
	for (Displacement offset : Offsets)
		CheckMissileCol(missile, missile._midam, missile._midam, true, missile.position.tile + offset, true);

	if (missile._mirange == 0) {
		missile._miDelFlag = true;
		if (missile._micaster == TARGET_MONSTERS) {
			if (!missile.IsTrap())
				Players[missile._misource]._pInvincible = false;
		}
	}
	PutMissile(missile);
}

void MI_Flash2(Missile &missile)
{
	if (missile._micaster == TARGET_MONSTERS) {
		if (!missile.IsTrap())
			Players[missile._misource]._pInvincible = true;
	}
	missile._mirange--;

	constexpr Displacement Offsets[] = { { -1, -1 }, { 0, -1 }, { 1, -1 } };
	for (Displacement offset : Offsets)
		CheckMissileCol(missile, missile._midam, missile._midam, true, missile.position.tile + offset, true);

	if (missile._mirange == 0) {
		missile._miDelFlag = true;
		if (missile._micaster == TARGET_MONSTERS) {
			if (!missile.IsTrap())
				Players[missile._misource]._pInvincible = false;
		}
	}
	PutMissile(missile);
}

void MI_Firemove(Missile &missile)
{
	constexpr int ExpLight[14] = { 2, 3, 4, 5, 5, 6, 7, 8, 9, 10, 11, 12, 12 };

	missile.var1++;
	if (missile.var1 == missile._miAnimLen) {
		SetMissDir(missile, 1);
		missile._miAnimFrame = GenerateRnd(11) + 1;
	}
	int j = missile._mirange;
	MoveMissileAndCheckMissileCol(missile, missile._midam, missile._midam, false, false);
	if (missile._miHitFlag)
		missile._mirange = j;
	if (missile._mirange == 0) {
		missile._miDelFlag = true;
		AddUnLight(missile._mlid);
	}
	if (missile._mimfnum != 0 || missile._mirange == 0) {
		if (missile.position.tile != Point { missile.var3, missile.var4 }) {
			missile.var3 = missile.position.tile.x;
			missile.var4 = missile.position.tile.y;
			ChangeLight(missile._mlid, missile.position.tile, 8);
		}
	} else {
		if (missile.var2 == 0)
			missile._mlid = AddLight(missile.position.tile, ExpLight[0]);
		ChangeLight(missile._mlid, missile.position.tile, ExpLight[missile.var2]);
		missile.var2++;
	}
	missile.position.tile += Direction::South;
	missile.position.offset.deltaY -= 32;
	PutMissile(missile);
}

void MI_Guardian(Missile &missile)
{
	missile._mirange--;

	if (missile.var2 > 0) {
		missile.var2--;
	}
	if (missile._mirange == missile.var1 || (missile._mimfnum == MFILE_GUARD && missile.var2 == 0)) {
		SetMissDir(missile, 1);
	}

	Point position = missile.position.tile;

	if ((missile._mirange % 16) == 0) {
		Displacement previous = { 0, 0 };

		bool found = false;
		for (int j = 0; j < 23 && !found; j++) {
			for (int k = 10; k >= 0 && !found; k -= 2) {
				const Displacement offset { VisionCrawlTable[j][k], VisionCrawlTable[j][k + 1] };
				if (offset == Displacement { 0, 0 }) {
					break;
				}
				if (previous == offset) {
					continue;
				}
				found = GuardianTryFireAt(missile, { position.x + offset.deltaX, position.y + offset.deltaY })
				    || GuardianTryFireAt(missile, { position.x - offset.deltaX, position.y - offset.deltaY })
				    || GuardianTryFireAt(missile, { position.x + offset.deltaX, position.y - offset.deltaY })
				    || GuardianTryFireAt(missile, { position.x - offset.deltaX, position.y + offset.deltaY });
				if (!found) {
					previous = offset;
				}
			}
		}
	}

	if (missile._mirange == 14) {
		SetMissDir(missile, 0);
		missile._miAnimFrame = 15;
		missile._miAnimAdd = -1;
	}

	missile.var3 += missile._miAnimAdd;

	if (missile.var3 > 15) {
		missile.var3 = 15;
	} else if (missile.var3 > 0) {
		ChangeLight(missile._mlid, position, missile.var3);
	}

	if (missile._mirange == 0) {
		missile._miDelFlag = true;
		AddUnLight(missile._mlid);
	}

	PutMissile(missile);
}

void MI_Chain(Missile &missile)
{
	int id = missile._misource;
	Point position = missile.position.tile;
	Point dst { missile.var1, missile.var2 };
	Direction dir = GetDirection(position, dst);
	AddMissile(position, dst, dir, MIS_LIGHTCTRL, TARGET_MONSTERS, id, 1, missile._mispllvl);
	int rad = missile._mispllvl + 3;
	if (rad > 19)
		rad = 19;
	for (int i = 1; i < rad; i++) {
		int k = CrawlNum[i];
		int ck = k + 2;
		for (auto j = static_cast<uint8_t>(CrawlTable[k]); j > 0; j--, ck += 2) {
			Point target = position + Displacement { CrawlTable[ck - 1], CrawlTable[ck] };
			if (InDungeonBounds(target) && dMonster[target.x][target.y] > 0) {
				dir = GetDirection(position, target);
				AddMissile(position, target, dir, MIS_LIGHTCTRL, TARGET_MONSTERS, id, 1, missile._mispllvl);
			}
		}
	}
	missile._mirange--;
	if (missile._mirange == 0)
		missile._miDelFlag = true;
}

void MI_Weapexp(Missile &missile)
{
	constexpr int ExpLight[10] = { 9, 10, 11, 12, 11, 10, 8, 6, 4, 2 };

	missile._mirange--;
	int id = missile._misource;
	int mind;
	int maxd;
	if (missile.var2 == 1) {
		mind = Players[id]._pIFMinDam;
		maxd = Players[id]._pIFMaxDam;
		MissilesData[missile._mitype].mResist = MISR_FIRE;
	} else {
		mind = Players[id]._pILMinDam;
		maxd = Players[id]._pILMaxDam;
		MissilesData[missile._mitype].mResist = MISR_LIGHTNING;
	}
	CheckMissileCol(missile, mind, maxd, false, missile.position.tile, false);
	if (missile.var1 == 0) {
		missile._mlid = AddLight(missile.position.tile, 9);
	} else {
		if (missile._mirange != 0)
			ChangeLight(missile._mlid, missile.position.tile, ExpLight[missile.var1]);
	}
	missile.var1++;
	if (missile._mirange == 0) {
		missile._miDelFlag = true;
		AddUnLight(missile._mlid);
	} else {
		PutMissile(missile);
	}
}

void MI_Misexp(Missile &missile)
{
	constexpr int ExpLight[] = { 9, 10, 11, 12, 11, 10, 8, 6, 4, 2, 1, 0, 0, 0, 0 };

	missile._mirange--;
	if (missile._mirange == 0) {
		missile._miDelFlag = true;
		AddUnLight(missile._mlid);
	} else {
		if (missile.var1 == 0)
			missile._mlid = AddLight(missile.position.tile, 9);
		else
			ChangeLight(missile._mlid, missile.position.tile, ExpLight[missile.var1]);
		missile.var1++;
		PutMissile(missile);
	}
}

void MI_Acidsplat(Missile &missile)
{
	if (missile._mirange == missile._miAnimLen) {
		missile.position.tile += Displacement { 1, 1 };
		missile.position.offset.deltaY -= 32;
	}
	missile._mirange--;
	if (missile._mirange == 0) {
		missile._miDelFlag = true;
		int monst = missile._misource;
		int dam = (Monsters[monst].MData->mLevel >= 2 ? 2 : 1);
		AddMissile(missile.position.tile, { 0, 0 }, Direction::South, MIS_ACIDPUD, TARGET_PLAYERS, monst, dam, missile._mispllvl);
	} else {
		PutMissile(missile);
	}
}

void MI_Teleport(Missile &missile)
{
	missile._mirange--;
	if (missile._mirange <= 0) {
		missile._miDelFlag = true;
		return;
	}

	int id = missile._misource;
	Player &player = Players[id];

	dPlayer[player.position.tile.x][player.position.tile.y] = 0;
	PlrClrTrans(player.position.tile);
	player.position.tile = missile.position.tile;
	player.position.future = player.position.tile;
	player.position.old = player.position.tile;
	PlrDoTrans(player.position.tile);
	missile.var1 = 1;
	dPlayer[player.position.tile.x][player.position.tile.y] = id + 1;
	if (leveltype != DTYPE_TOWN) {
		ChangeLightXY(player._plid, player.position.tile);
		ChangeVisionXY(player._pvid, player.position.tile);
	}
	if (id == MyPlayerId) {
		ViewPosition = Point { 0, 0 } + (player.position.tile - ScrollInfo.tile);
	}
}

void MI_Stone(Missile &missile)
{
	missile._mirange--;
	auto &monster = Monsters[missile.var2];
	if (monster._mhitpoints == 0 && missile._miAnimType != MFILE_SHATTER1) {
		missile._mimfnum = 0;
		missile._miDrawFlag = true;
		SetMissAnim(missile, MFILE_SHATTER1);
		missile._mirange = 11;
	}
	if (monster._mmode != MonsterMode::Petrified) {
		missile._miDelFlag = true;
		return;
	}

	if (missile._mirange == 0) {
		missile._miDelFlag = true;
		if (monster._mhitpoints > 0) {
			monster._mmode = static_cast<MonsterMode>(missile.var1);
			monster.AnimInfo.IsPetrified = false;
		} else {
			AddCorpse(monster.position.tile, stonendx, monster._mdir);
		}
	}
	if (missile._miAnimType == MFILE_SHATTER1)
		PutMissile(missile);
}

void MI_Boom(Missile &missile)
{
	missile._mirange--;
	if (missile.var1 == 0)
		CheckMissileCol(missile, missile._midam, missile._midam, false, missile.position.tile, true);
	if (missile._miHitFlag)
		missile.var1 = 1;
	if (missile._mirange == 0)
		missile._miDelFlag = true;
	PutMissile(missile);
}

void MI_Rhino(Missile &missile)
{
	int monst = missile._misource;
	auto &monster = Monsters[monst];
	if (monster._mmode != MonsterMode::Charge) {
		missile._miDelFlag = true;
		return;
	}
	UpdateMissilePos(missile);
	Point prevPos = missile.position.tile;
	Point newPosSnake;
	dMonster[prevPos.x][prevPos.y] = 0;
	if (monster._mAi == AI_SNAKE) {
		missile.position.traveled += missile.position.velocity * 2;
		UpdateMissilePos(missile);
		newPosSnake = missile.position.tile;
		missile.position.traveled -= missile.position.velocity;
	} else {
		missile.position.traveled += missile.position.velocity;
	}
	UpdateMissilePos(missile);
	Point newPos = missile.position.tile;
	if (!IsTileAvailable(monster, newPos) || (monster._mAi == AI_SNAKE && !IsTileAvailable(monster, newPosSnake))) {
		MissToMonst(missile, prevPos);
		missile._miDelFlag = true;
		return;
	}
	monster.position.future = newPos;
	monster.position.old = newPos;
	monster.position.tile = newPos;
	dMonster[newPos.x][newPos.y] = -(monst + 1);
	if (monster._uniqtype != 0)
		ChangeLightXY(missile._mlid, newPos);
	MoveMissilePos(missile);
	PutMissile(missile);
}

void MI_FirewallC(Missile &missile)
{
	missile._mirange--;
	if (missile._mirange == 0) {
		missile._miDelFlag = true;
		return;
	}

	int id = missile._misource;

	{
		Point position = { missile.var1, missile.var2 };
		Point target = position + static_cast<Direction>(missile.var3);

		if (!missile.limitReached && GrowWall(id, position, target, MIS_FIREWALL, missile._mispllvl, 0)) {
			missile.var1 = target.x;
			missile.var2 = target.y;
		} else {
			missile.limitReached = true;
		}
	}

	{
		Point position = { missile.var5, missile.var6 };
		Point target = position + static_cast<Direction>(missile.var4);

		if (missile.var7 == 0 && GrowWall(id, position, target, MIS_FIREWALL, missile._mispllvl, 0)) {
			missile.var5 = target.x;
			missile.var6 = target.y;
		} else {
			missile.var7 = 1;
		}
	}
}

void MI_Infra(Missile &missile)
{
	Player &player = Players[missile._misource];
	missile._mirange--;
	player._pInfraFlag = true;
	if (missile._mirange == 0) {
		missile._miDelFlag = true;
		CalcPlrItemVals(player, true);
	}
}

void MI_Apoca(Missile &missile)
{
	int id = missile._misource;
	bool exit = false;
	int j;
	int k;
	for (j = missile.var2; j < missile.var3 && !exit; j++) {
		for (k = missile.var4; k < missile.var5 && !exit; k++) {
			int mid = dMonster[k][j] - 1;
			if (mid < 0)
				continue;
			if (Monsters[mid].MType->mtype == MT_GOLEM)
				continue;
			if (nSolidTable[dPiece[k][j]])
				continue;
			if (gbIsHellfire && !LineClearMissile(missile.position.tile, { k, j }))
				continue;
			AddMissile({ k, j }, { k, j }, Players[id]._pdir, MIS_BOOM, TARGET_MONSTERS, id, missile._midam, 0);
			exit = true;
		}
		if (!exit) {
			missile.var4 = missile.var6;
		}
	}

	if (exit) {
		missile.var2 = j - 1;
		missile.var4 = k;
	} else {
		missile._miDelFlag = true;
	}
}

void MI_Wave(Missile &missile)
{
	bool f1 = false;
	bool f2 = false;

	int id = missile._misource;
	Point src = missile.position.tile;
	Direction sd = GetDirection(src, { missile.var1, missile.var2 });
	Direction dira = Left(Left(sd));
	Direction dirb = Right(Right(sd));
	Point na = src + sd;
	int pn = dPiece[na.x][na.y];
	assert(pn >= 0 && pn <= MAXTILES);
	if (!nMissileTable[pn]) {
		Direction pdir = Players[id]._pdir;
		AddMissile(na, na + sd, pdir, MIS_FIREMOVE, TARGET_MONSTERS, id, 0, missile._mispllvl);
		na += dira;
		Point nb = src + sd + dirb;
		for (int j = 0; j < (missile._mispllvl / 2) + 2; j++) {
			pn = dPiece[na.x][na.y]; // BUGFIX: dPiece is accessed before check against dungeon size and 0
			assert(pn >= 0 && pn <= MAXTILES);
			if (nMissileTable[pn] || f1 || !InDungeonBounds(na)) {
				f1 = true;
			} else {
				AddMissile(na, na + sd, pdir, MIS_FIREMOVE, TARGET_MONSTERS, id, 0, missile._mispllvl);
				na += dira;
			}
			pn = dPiece[nb.x][nb.y]; // BUGFIX: dPiece is accessed before check against dungeon size and 0
			assert(pn >= 0 && pn <= MAXTILES);
			if (nMissileTable[pn] || f2 || !InDungeonBounds(nb)) {
				f2 = true;
			} else {
				AddMissile(nb, nb + sd, pdir, MIS_FIREMOVE, TARGET_MONSTERS, id, 0, missile._mispllvl);
				nb += dirb;
			}
		}
	}

	missile._mirange--;
	if (missile._mirange == 0)
		missile._miDelFlag = true;
}

void MI_Nova(Missile &missile)
{
	int sx1 = 0;
	int sy1 = 0;
	int id = missile._misource;
	int dam = missile._midam;
	Point src = missile.position.tile;
	Direction dir = Direction::South;
	mienemy_type en = TARGET_PLAYERS;
	if (!missile.IsTrap()) {
		dir = Players[id]._pdir;
		en = TARGET_MONSTERS;
	}
	for (const auto &k : VisionCrawlTable) {
		if (sx1 != k[6] || sy1 != k[7]) {
			AddMissile(src, src + Displacement { k[6], k[7] }, dir, MIS_LIGHTBALL, en, id, dam, missile._mispllvl);
			AddMissile(src, src + Displacement { -k[6], -k[7] }, dir, MIS_LIGHTBALL, en, id, dam, missile._mispllvl);
			AddMissile(src, src + Displacement { -k[6], k[7] }, dir, MIS_LIGHTBALL, en, id, dam, missile._mispllvl);
			AddMissile(src, src + Displacement { k[6], -k[7] }, dir, MIS_LIGHTBALL, en, id, dam, missile._mispllvl);
			sx1 = k[6];
			sy1 = k[7];
		}
	}
	missile._mirange--;
	if (missile._mirange == 0)
		missile._miDelFlag = true;
}

void MI_Blodboil(Missile &missile)
{
	missile._mirange--;

	if (missile._mirange != 0) {
		return;
	}

	int id = missile._misource;
	Player &player = Players[id];

	int hpdif = player._pMaxHP - player._pHitPoints;

	if (HasAnyOf(player._pSpellFlags, SpellFlag::RageActive)) {
		player._pSpellFlags &= ~SpellFlag::RageActive;
		player._pSpellFlags |= SpellFlag::RageCooldown;
		int lvl = player._pLevel * 2;
		missile._mirange = lvl + 10 * missile._mispllvl + 245;
	} else {
		player._pSpellFlags &= ~SpellFlag::RageCooldown;
		missile._miDelFlag = true;
		hpdif += missile.var2;
	}

	CalcPlrItemVals(player, true);
	ApplyPlrDamage(id, 0, 1, hpdif);
	force_redraw = 255;
	player.Say(HeroSpeech::HeavyBreathing);
}

void MI_Flame(Missile &missile)
{
	missile._mirange--;
	missile.var2--;
	int k = missile._mirange;
	CheckMissileCol(missile, missile._midam, missile._midam, true, missile.position.tile, false);
	if (missile._mirange == 0 && missile._miHitFlag)
		missile._mirange = k;
	if (missile.var2 == 0)
		missile._miAnimFrame = 20;
	if (missile.var2 <= 0) {
		k = missile._miAnimFrame;
		if (k > 11)
			k = 24 - k;
		ChangeLight(missile._mlid, missile.position.tile, k);
	}
	if (missile._mirange == 0) {
		missile._miDelFlag = true;
		AddUnLight(missile._mlid);
	}
	if (missile.var2 <= 0)
		PutMissile(missile);
}

void MI_Flamec(Missile &missile)
{
	missile._mirange--;
	int src = missile._misource;
	missile.position.traveled += missile.position.velocity;
	UpdateMissilePos(missile);
	if (missile.position.tile != Point { missile.var1, missile.var2 }) {
		int id = dPiece[missile.position.tile.x][missile.position.tile.y];
		if (!nMissileTable[id]) {
			AddMissile(
			    missile.position.tile,
			    missile.position.start,
			    Direction::South,
			    MIS_FLAME,
			    missile._micaster,
			    src,
			    missile.var3,
			    missile._mispllvl);
		} else {
			missile._mirange = 0;
		}
		missile.var1 = missile.position.tile.x;
		missile.var2 = missile.position.tile.y;
		missile.var3++;
	}
	if (missile._mirange == 0 || missile.var3 == 3)
		missile._miDelFlag = true;
}

void MI_Cbolt(Missile &missile)
{
	missile._mirange--;
	if (missile._miAnimType != MFILE_LGHNING) {
		if (missile.var3 == 0) {
			constexpr int BPath[16] = { -1, 0, 1, -1, 0, 1, -1, -1, 0, 0, 1, 1, 0, 1, -1, 0 };

			auto md = static_cast<Direction>(missile.var2);
			switch (BPath[missile._mirnd]) {
			case -1:
				md = Left(md);
				break;
			case 1:
				md = Right(md);
				break;
			}

			missile._mirnd = (missile._mirnd + 1) & 0xF;
			UpdateMissileVelocity(missile, missile.position.tile + md, 8);
			missile.var3 = 16;
		} else {
			missile.var3--;
		}
		MoveMissileAndCheckMissileCol(missile, missile._midam, missile._midam, false, false);
		if (missile._miHitFlag) {
			missile.var1 = 8;
			missile._mimfnum = 0;
			missile.position.offset = { 0, 0 };
			missile.position.velocity = {};
			SetMissAnim(missile, MFILE_LGHNING);
			missile._mirange = missile._miAnimLen;
		}
		ChangeLight(missile._mlid, missile.position.tile, missile.var1);
	}
	if (missile._mirange == 0) {
		missile._miDelFlag = true;
		AddUnLight(missile._mlid);
	}
	PutMissile(missile);
}

void MI_Hbolt(Missile &missile)
{
	missile._mirange--;
	if (missile._miAnimType != MFILE_HOLYEXPL) {
		int dam = missile._midam;
		MoveMissileAndCheckMissileCol(missile, dam, dam, true, true);
		if (missile._mirange == 0) {
			missile._mimfnum = 0;
			SetMissAnim(missile, MFILE_HOLYEXPL);
			missile._mirange = missile._miAnimLen - 1;
			missile.position.StopMissile();
		} else {
			if (missile.position.tile != Point { missile.var1, missile.var2 }) {
				missile.var1 = missile.position.tile.x;
				missile.var2 = missile.position.tile.y;
				ChangeLight(missile._mlid, missile.position.tile, 8);
			}
		}
	} else {
		ChangeLight(missile._mlid, missile.position.tile, missile._miAnimFrame + 7);
		if (missile._mirange == 0) {
			missile._miDelFlag = true;
			AddUnLight(missile._mlid);
		}
	}
	PutMissile(missile);
}

void MI_Element(Missile &missile)
{
	missile._mirange--;
	int dam = missile._midam;
	int id = missile._misource;
	const Point missilePosition = missile.position.tile;
	if (missile._miAnimType == MFILE_BIGEXP) {
		ChangeLight(missile._mlid, missile.position.tile, missile._miAnimFrame);

		Point startPoint = missile.var3 == 2 ? Point { missile.var4, missile.var5 } : missile.position.start;
		constexpr Displacement Offsets[] = { { 0, 0 }, { 0, 1 }, { 0, -1 }, { 1, 0 }, { 1, -1 }, { 1, 1 }, { -1, 0 }, { -1, 1 }, { -1, -1 } };
		for (Displacement offset : Offsets) {
			if (!CheckBlock(startPoint, missilePosition + offset))
				CheckMissileCol(missile, dam, dam, true, missilePosition + offset, true);
		}

		if (missile._mirange == 0) {
			missile._miDelFlag = true;
			AddUnLight(missile._mlid);
		}
	} else {
		MoveMissileAndCheckMissileCol(missile, dam, dam, false, false);
		if (missile.var3 == 0 && missilePosition == Point { missile.var4, missile.var5 })
			missile.var3 = 1;
		if (missile.var3 == 1) {
			missile.var3 = 2;
			missile._mirange = 255;
			auto *nextMonster = FindClosest(missilePosition, 19);
			if (nextMonster != nullptr) {
				Direction sd = GetDirection(missilePosition, nextMonster->position.tile);
				SetMissDir(missile, sd);
				UpdateMissileVelocity(missile, nextMonster->position.tile, 16);
			} else {
				Direction sd = Players[id]._pdir;
				SetMissDir(missile, sd);
				UpdateMissileVelocity(missile, missilePosition + sd, 16);
			}
		}
		if (missilePosition != Point { missile.var1, missile.var2 }) {
			missile.var1 = missilePosition.x;
			missile.var2 = missilePosition.y;
			ChangeLight(missile._mlid, missilePosition, 8);
		}
		if (missile._mirange == 0) {
			missile._mimfnum = 0;
			SetMissAnim(missile, MFILE_BIGEXP);
			missile._mirange = missile._miAnimLen - 1;
			missile.position.StopMissile();
		}
	}
	PutMissile(missile);
}

void MI_Bonespirit(Missile &missile)
{
	missile._mirange--;
	int dam = missile._midam;
	int id = missile._misource;
	if (missile._mimfnum == 8) {
		ChangeLight(missile._mlid, missile.position.tile, missile._miAnimFrame);
		if (missile._mirange == 0) {
			missile._miDelFlag = true;
			AddUnLight(missile._mlid);
		}
		PutMissile(missile);
	} else {
		MoveMissileAndCheckMissileCol(missile, dam, dam, false, false);
		Point c = missile.position.tile;
		if (missile.var3 == 0 && c == Point { missile.var4, missile.var5 })
			missile.var3 = 1;
		if (missile.var3 == 1) {
			missile.var3 = 2;
			missile._mirange = 255;
			auto *monster = FindClosest(c, 19);
			if (monster != nullptr) {
				missile._midam = monster->_mhitpoints >> 7;
				SetMissDir(missile, GetDirection(c, monster->position.tile));
				UpdateMissileVelocity(missile, monster->position.tile, 16);
			} else {
				Direction sd = Players[id]._pdir;
				SetMissDir(missile, sd);
				UpdateMissileVelocity(missile, c + sd, 16);
			}
		}
		if (c != Point { missile.var1, missile.var2 }) {
			missile.var1 = c.x;
			missile.var2 = c.y;
			ChangeLight(missile._mlid, c, 8);
		}
		if (missile._mirange == 0) {
			SetMissDir(missile, 8);
			missile.position.velocity = {};
			missile._mirange = 7;
		}
		PutMissile(missile);
	}
}

void MI_ResurrectBeam(Missile &missile)
{
	missile._mirange--;
	if (missile._mirange == 0)
		missile._miDelFlag = true;
	PutMissile(missile);
}

void MI_Rportal(Missile &missile)
{
	int expLight[17] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15 };

	if (missile._mirange > 1)
		missile._mirange--;
	if (missile._mirange == missile.var1)
		SetMissDir(missile, 1);

	if (currlevel != 0 && missile._mimfnum != 1 && missile._mirange != 0) {
		if (missile.var2 == 0)
			missile._mlid = AddLight(missile.position.tile, 1);
		ChangeLight(missile._mlid, missile.position.tile, expLight[missile.var2]);
		missile.var2++;
	}
	if (missile._mirange == 0) {
		missile._miDelFlag = true;
		AddUnLight(missile._mlid);
	}
	PutMissile(missile);
}

static void DeleteMissiles()
{
	Missiles.remove_if([](Missile &missile) { return missile._miDelFlag; });
}

void ProcessManaShield()
{
	Player &myPlayer = *MyPlayer;
	if (myPlayer.pManaShield && myPlayer._pMana <= 0) {
		myPlayer.pManaShield = false;
		NetSendCmd(true, CMD_REMSHIELD);
	}
}

void ProcessMissiles()
{
	for (auto &missile : Missiles) {
		const auto &position = missile.position.tile;
		if (InDungeonBounds(position)) {
			dFlags[position.x][position.y] &= ~DungeonFlag::Missile;
		} else {
			missile._miDelFlag = true;
		}
	}

	DeleteMissiles();

	MissilePreFlag = false;

	for (auto &missile : Missiles) {
		if (MissilesData[missile._mitype].mProc != nullptr)
			MissilesData[missile._mitype].mProc(missile);
		if (missile._miAnimFlags == MissileDataFlags::NotAnimated)
			continue;

		missile._miAnimCnt++;
		if (missile._miAnimCnt < missile._miAnimDelay)
			continue;

		missile._miAnimCnt = 0;
		missile._miAnimFrame += missile._miAnimAdd;
		if (missile._miAnimFrame > missile._miAnimLen)
			missile._miAnimFrame = 1;
		else if (missile._miAnimFrame < 1)
			missile._miAnimFrame = missile._miAnimLen;
	}

	ProcessManaShield();
	DeleteMissiles();
}

void missiles_process_charge()
{
	for (auto &missile : Missiles) {
		missile._miAnimData = MissileSpriteData[missile._miAnimType].GetFrame(missile._mimfnum);
		if (missile._mitype != MIS_RHINO)
			continue;

		CMonster *mon = Monsters[missile._misource].MType;

		MonsterGraphic graphic;
		if (IsAnyOf(mon->mtype, MT_HORNED, MT_MUDRUN, MT_FROSTC, MT_OBLORD)) {
			graphic = MonsterGraphic::Special;
		} else if (IsAnyOf(mon->mtype, MT_NSNAKE, MT_RSNAKE, MT_BSNAKE, MT_GSNAKE)) {
			graphic = MonsterGraphic::Attack;
		} else {
			graphic = MonsterGraphic::Walk;
		}
		missile._miAnimData = mon->GetAnimData(graphic).CelSpritesForDirections[missile._mimfnum];
	}
}

void RedoMissileFlags()
{
	for (auto &missile : Missiles) {
		PutMissile(missile);
	}
}

} // namespace devilution

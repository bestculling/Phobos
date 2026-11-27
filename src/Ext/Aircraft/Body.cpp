#include "Body.h"

#include <Ext/AircraftType/Body.h>
#include <Ext/BuildingType/Body.h>
#include <Ext/WeaponType/Body.h>
#include <Utilities/GeneralUtils.h>

AircraftExt::ExtContainer AircraftExt::ExtMap;

void AircraftExt::FireWeapon(AircraftClass* pThis, AbstractClass* pTarget)
{
	auto const pExt = AircraftExt::Fetch(pThis);
	const int weaponIndex = pExt->CurrentAircraftWeaponIndex;
	auto const pWeapon = pThis->GetWeapon(weaponIndex)->WeaponType;
	auto const pWeaponExt = WeaponTypeExt::Fetch(pWeapon);
	const int burstCount = pWeapon->Burst;
	const bool isStrafe = pThis->Is_Strafe();

	if (burstCount > 0)
	{
		int& bombDropCount = pExt->Strafe_BombsDroppedThisRound;
		int& currentBurstIndex = pThis->CurrentBurstIndex;
		const bool simulateBurst = pWeaponExt->Strafing_SimulateBurst.Get(RulesExt::Global()->Strafing_SimulateBurst);

		for (int i = 0; i < burstCount; i++)
		{
			if (isStrafe && burstCount < 2 && simulateBurst)
				currentBurstIndex = bombDropCount % 2 == 0;

			pThis->Fire(pTarget, weaponIndex);
		}

		if (isStrafe)
		{
			bombDropCount++;

			if (pWeaponExt->Strafing_UseAmmoPerShot.Get(RulesExt::Global()->Strafing_UseAmmoPerShot))
			{
				pThis->Ammo--;
				pThis->ShouldLoseAmmo = false;

				if (!pThis->Ammo)
				{
					pThis->SetTarget(nullptr);
					pThis->SetDestination(nullptr, true);
				}
			}
		}
	}
}

// Paradrop, spy plane, airstrike.
bool AircraftExt::PlaceReinforcementAircraft(AircraftClass* pThis, CoordStruct edgeCoords)
{
	auto const pType = pThis->Type;
	auto const pTypeExt = AircraftTypeExt::Fetch(pType);
	auto dir = DirType::North;
	auto coords = edgeCoords;
	coords.Z = 0;
	AbstractClass* pTarget = pThis->Target ? pThis->Target : pThis->Destination;

	if (pTarget)
	{
		auto const pTargetCoords = pTarget->GetCoords();

		if (pTypeExt->SpawnDistanceFromTarget.isset())
			coords = GeneralUtils::CalculateCoordsFromDistance(edgeCoords, pTargetCoords, pTypeExt->SpawnDistanceFromTarget.Get());

		dir = GeneralUtils::GetDirectionBetweenCoords(coords, pTargetCoords).GetDir();
	}

	bool result = false;

	++Unsorted::ScenarioInit;
	result = pThis->Unlimbo(coords, dir);
	--Unsorted::ScenarioInit;

	pThis->SetHeight(pTypeExt->SpawnHeight.isset() ? pTypeExt->SpawnHeight.Get() : pType->GetFlightLevel());

	if (pTarget)
		pThis->PrimaryFacing.SetDesired(pThis->GetTargetDirection(pTarget));

	return result;
}

CellStruct AircraftExt::PickEdgeCellForPlane(AircraftTypeClass* pPlaneType, CellStruct destCell, Edge edge, bool isOnRetreat)
{
	auto const pTypeExt = AircraftTypeExt::Fetch(pPlaneType);
	auto const edgeMode = !isOnRetreat ? pTypeExt->SpawnFromEdge.Get(RulesExt::Global()->AircraftSpawnFromEdge)
		: pTypeExt->RetreatToEdge.Get(RulesExt::Global()->AircraftRetreatToEdge);
	auto spawnEdge = edge;
	auto refCell = CellStruct::Empty;

	switch (edgeMode)
	{
	case EdgeType::Closest:
	{
		if (destCell != CellStruct::Empty)
		{
			spawnEdge = Edge::None;
			refCell = destCell;

			// Scatter the coords a bit to randomize spawn cell a little - otherwise multiple planes sent at same target
			// from same source might end up overlapping - still a possibility, just less likely.
			// The edge cell picking function itself will do no randomization on Edge::None + waypoint cell set mode.
			int const randomRange = 5;
			short const randomX = static_cast<short>(ScenarioClass::Instance->Random.RandomRanged(-randomRange, randomRange));
			short const randomY = static_cast<short>(ScenarioClass::Instance->Random.RandomRanged(-randomRange, randomRange));
			refCell += CellStruct { randomX, randomY };
		}
		break;
	}
	case EdgeType::Random:
	{
		int const min = static_cast<int>(Edge::North);
		int const max = static_cast<int>(Edge::West);
		spawnEdge = static_cast<Edge>(ScenarioClass::Instance->Random.RandomRanged(min, max));
		break;
	}
	default:
	{
		break;
	}
	}

	return MapClass::Instance.PickCellOnEdge(spawnEdge, refCell, CellStruct::Empty, SpeedType::Winged, true, MovementZone::Normal);
}

DirType AircraftExt::GetLandingDir(AircraftClass* pThis, BuildingClass* pDock, bool isProduction)
{
	auto const poseDir = static_cast<DirType>(RulesClass::Instance->PoseDir);

	if (!pThis)
		return poseDir;

	// If this is a spawnee, use the spawner's facing.
	if (auto const pOwner = pThis->SpawnOwner)
		return pOwner->PrimaryFacing.Current().GetDir();

	auto const pType = pThis->Type;

	const bool hasDockOrLink = (pDock || pThis->HasAnyLink());

	if (hasDockOrLink)
	{
		auto const pLink = pThis->GetNthLink(0);

		if (auto const pBuilding = pDock ? pDock : abstract_cast<BuildingClass*, true>(pLink))
		{
			auto const pBuildingType = pBuilding->Type;
			auto const pBuildingTypeExt = BuildingTypeExt::Fetch(pBuildingType);
			const int docks = pBuildingType->NumberOfDocks;
			const int linkIndex = pBuilding->FindLinkIndex(pThis);

			if (docks > 0 && linkIndex >= 0 && linkIndex < docks)
			{
				if (pBuildingTypeExt->AircraftDockingDirs[linkIndex].has_value())
					return *pBuildingTypeExt->AircraftDockingDirs[linkIndex];
			}
			else if (docks > 0 && pBuildingTypeExt->AircraftDockingDirs[0].has_value())
				return *pBuildingTypeExt->AircraftDockingDirs[0];

			if (!pBuildingTypeExt->AircraftDockingDir_DefaultToPoseDir.Get(RulesExt::Global()->AircraftDockingDir_DefaultToPoseDir))
			{
				if (!isProduction)
					return pBuilding->PrimaryFacing.Current().GetDir();
			}
		}
		else if (!pType->AirportBound)
			return pLink->PrimaryFacing.Current().GetDir();
	}

	auto const pTypeExt = AircraftTypeExt::Fetch(pType);

	if (pTypeExt->LandingDir.isset())
	{
		int landingDir = pTypeExt->LandingDir.Get();
		if (pType->AirportBound)
			return static_cast<DirType>(landingDir & 0xFF);
		else if (landingDir < 0)
			return pThis->PrimaryFacing.Current().GetDir();
		else
			return static_cast<DirType>(landingDir & 0xFF);
	}

	if (isProduction)
		return static_cast<DirType>(RulesExt::Global()->PoseDir_Production.Get((int)poseDir) & 0xFF);

	if (hasDockOrLink)
		return poseDir;

	int fieldDir = RulesExt::Global()->PoseDir_Field.Get((int)poseDir * 32);
	return static_cast<DirType>(fieldDir & 0xFF);
}

AircraftTypeClass* AircraftExt::GetAircraftTypeExtra(AircraftClass* pAircraft)
{
	auto const pType = pAircraft->Type;
	auto const pData = AircraftTypeExt::Fetch(pType);

	if (!pData->NeedDamagedImage || pAircraft->IsGreenHP())
	{
		return pType;
	}
	else if (pAircraft->IsYellowHP())
	{
		if (auto const imageYellow = pData->Image_ConditionYellow)
			return abstract_cast<AircraftTypeClass*, true>(imageYellow);
	}
	else
	{
		if (auto const imageRed = pData->Image_ConditionRed)
			return abstract_cast<AircraftTypeClass*, true>(imageRed);
		else if (auto const imageYellow = pData->Image_ConditionYellow)
			return abstract_cast<AircraftTypeClass*, true>(imageYellow);
	}

	return pType;
}

// =============================
// AutoScramble (ra2-custom): idle aircraft with ammo automatically attack enemies that come
// within AutoScramble.Range cells of their airport. Aircraft of the same owner share target
// information: a target that is already covered by enough firepower is left to the others.

static WeaponTypeClass* AutoScramble_WeaponFor(AircraftClass* pThis, TechnoClass* pTarget, double* pVersus)
{
	const int idx = pThis->SelectWeapon(pTarget);
	const auto pWeaponStruct = pThis->GetWeapon(idx);
	const auto pWeapon = pWeaponStruct ? pWeaponStruct->WeaponType : nullptr;

	if (!pWeapon || !pWeapon->Projectile || !pWeapon->Warhead)
		return nullptr;

	if (pTarget->IsInAir() ? !pWeapon->Projectile->AA : !pWeapon->Projectile->AG)
		return nullptr;

	const double versus = GeneralUtils::GetWarheadVersusArmor(pWeapon->Warhead, pTarget, pTarget->GetTechnoType());

	if (versus <= 0.0)
		return nullptr;

	if (pVersus)
		*pVersus = versus;

	return pWeapon;
}

void AircraftExt::UpdateAutoScramble(AircraftClass* pThis)
{
	const auto pTypeExt = AircraftTypeExt::Fetch(pThis->Type);

	if (!pTypeExt->AutoScramble)
		return;

	// stagger the scans so not every aircraft searches in the same frame
	if ((Unsorted::CurrentFrame + pThis->UniqueID) % 15 != 0)
		return;

	if (!pThis->IsAlive || !pThis->IsOnMap || pThis->InLimbo || pThis->Health <= 0)
		return;

	if (pThis->Team || pThis->Airstrike || pThis->IsALoaner || pThis->Ammo <= 0 || pThis->Target)
		return;

	const auto pOwner = pThis->Owner;

	if (!pOwner || pOwner->Defeated || pOwner->Type->MultiplayPassive)
		return;

	// only take over idle aircraft: parked (sleep / guard) or flying home (enter)
	const auto mission = pThis->CurrentMission;

	if (mission != Mission::Sleep && mission != Mission::Guard && mission != Mission::Enter)
		return;

	// the protected area is centred on the aircraft's airport
	AbstractClass* pCenter = pThis;

	if (pThis->DockNowHeadingTo)
		pCenter = pThis->DockNowHeadingTo;
	else if (const auto pLink = pThis->GetNthLink(0))
		pCenter = pLink;

	const int range = static_cast<int>(pTypeExt->AutoScramble_Range * Unsorted::LeptonsPerCell);

	TechnoClass* pBest = nullptr;
	double bestScore = 0.0;

	for (auto const pTechno : TechnoClass::Array)
	{
		if (!pTechno->IsAlive || !pTechno->IsOnMap || pTechno->InLimbo || pTechno->Health <= 0)
			continue;

		const auto pTargetOwner = pTechno->Owner;

		if (!pTargetOwner || pTargetOwner == pOwner || pTargetOwner->Type->MultiplayPassive || pOwner->IsAlliedWith(pTargetOwner))
			continue;

		if (pTechno->CloakState == CloakState::Cloaked)
			continue;

		const bool isBuilding = pTechno->WhatAmI() == AbstractType::Building;

		if (isBuilding && static_cast<BuildingClass*>(pTechno)->Type->InvisibleInGame)
			continue;

		const int distance = pCenter->DistanceFrom(pTechno);

		if (distance > range)
			continue;

		double versus = 1.0;

		if (!AutoScramble_WeaponFor(pThis, pTechno, &versus))
			continue;

		// shared information: how much firepower of ours is already on its way to this target?
		double assigned = 0.0;
		int attackers = 0;

		for (auto const pOther : AircraftClass::Array)
		{
			if (pOther == pThis || pOther->Owner != pOwner || pOther->Target != pTechno || !pOther->IsAlive)
				continue;

			++attackers;
			double otherVersus = 1.0;

			if (const auto pOtherWeapon = AutoScramble_WeaponFor(pOther, pTechno, &otherVersus))
				assigned += pOtherWeapon->Damage * Math::max(pOtherWeapon->Burst, 1) * Math::max(pOther->Ammo, 1) * otherVersus;
		}

		// already enough planes on it -> leave it, let the others pick something else
		if (attackers > 0 && assigned >= pTechno->Health)
			continue;

		// lower score = better: close targets first, units before buildings,
		// targets nobody is attacking yet before shared ones
		double score = distance;

		if (isBuilding)
			score += range;

		score += attackers * range * 2.0;

		if (!pBest || score < bestScore)
		{
			pBest = pTechno;
			bestScore = score;
		}
	}

	if (!pBest)
		return;

	pThis->SetArchiveTarget(nullptr); // return to the airport afterwards instead of area guarding
	pThis->ClickedMission(Mission::Attack, pBest, nullptr, nullptr);
}

// =============================
// load / save

template <typename T>
void AircraftExt::Serialize(T& Stm)
{
	Stm
		.Process(this->Strafe_BombsDroppedThisRound)
		.Process(this->Strafe_TargetCell)
		.Process(this->CurrentAircraftWeaponIndex)
		;
}

void AircraftExt::LoadFromStream(PhobosStreamReader& Stm)
{
	FootExt::LoadFromStream(Stm);
	this->Serialize(Stm);
}

void AircraftExt::SaveToStream(PhobosStreamWriter& Stm)
{
	FootExt::SaveToStream(Stm);
	this->Serialize(Stm);
}

// =============================
// container

AircraftExt::ExtContainer::ExtContainer() : Container("AircraftClass") { }
AircraftExt::ExtContainer::~ExtContainer() = default;

// =============================
// container hooks

DEFINE_HOOK(0x413D30, AircraftClass_CTOR, 0x7)
{
	GET(AircraftClass*, pItem, ESI);

	AircraftExt::ExtMap.Allocate(pItem);

	return 0;
}

// Late in every destructor body of the class, right before it chains into the
// base destructor: the last point where the extension is no longer used.
DEFINE_HOOK_AGAIN(0x41426D, AircraftClass_DTOR, 0x9)
DEFINE_HOOK(0x4141FA, AircraftClass_DTOR, 0x9)
{
	GET(AircraftClass*, pItem, EDI);

	AircraftExt::ExtMap.Remove(pItem);

	return 0;
}

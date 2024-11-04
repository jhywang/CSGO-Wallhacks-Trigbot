#include "hooks.h"

// include minhook for epic hookage
#include "../../ext/minhook/minhook.h"
#include "../../ext/x86retspoof/x86RetSpoof.h"

#include <intrin.h>
#include "../hacks/misc.h"

void hooks::Setup() noexcept
{
	MH_Initialize();

	// AllocKeyValuesMemory hook
	MH_CreateHook(
		memory::Get(interfaces::keyValuesSystem, 1),
		&AllocKeyValuesMemory,
		reinterpret_cast<void**>(&AllocKeyValuesMemoryOriginal)
	);

	// CreateMove hook
	MH_CreateHook(
		memory::Get(interfaces::clientMode, 24),
		&CreateMove,
		reinterpret_cast<void**>(&CreateMoveOriginal)
	);

	// DoPostScreenSpaceEffects hook
	MH_CreateHook(
		memory::Get(interfaces::clientMode, 44),					// function is at index 44
		&DoPostScreenSpaceEffects,									// our hook
		reinterpret_cast<void**>(&DoPostScreenSpaceEffectsOriginal) // the og function
	);

	MH_EnableHook(MH_ALL_HOOKS);
}

void hooks::Destroy() noexcept
{
	// restore hooks
	MH_DisableHook(MH_ALL_HOOKS);
	MH_RemoveHook(MH_ALL_HOOKS);

	// uninit minhook
	MH_Uninitialize();
}

void* __stdcall hooks::AllocKeyValuesMemory(const std::int32_t size) noexcept
{
	// if function is returning to speficied addresses, return nullptr to "bypass"
	if (const std::uint32_t address = reinterpret_cast<std::uint32_t>(_ReturnAddress());
		address == reinterpret_cast<std::uint32_t>(memory::allocKeyValuesEngine) ||
		address == reinterpret_cast<std::uint32_t>(memory::allocKeyValuesClient)) 
		return nullptr;

	// return original
	return AllocKeyValuesMemoryOriginal(interfaces::keyValuesSystem, size);
}

bool __stdcall hooks::CreateMove(float frameTime, CUserCmd* cmd) noexcept
{
	static const auto sequence = reinterpret_cast<std::uintptr_t>(memory::PatternScan("client.dll", "FF 23"));
	const auto result = x86RetSpoof::invokeStdcall<bool>((uintptr_t)hooks::CreateMoveOriginal, sequence, frameTime, cmd);

	// make sure this function is being called from CInput::CreateMove
	if (!cmd || !cmd->commandNumber)
		return result;

	// this would be done anyway by returning true
	if (CreateMoveOriginal(interfaces::clientMode, frameTime, cmd))
		interfaces::engine->SetViewAngles(cmd->viewAngles);

	// get our local player here
	globals::UpdateLocalPlayer();

	if (globals::localPlayer && globals::localPlayer->IsAlive())
	{
		// example bhop
		hacks::RunBunnyHop(cmd);

		// trig bot

		// get eye position
		CVector eyePosition;
		globals::localPlayer->GetEyePosition(eyePosition);

		// aimPunch is when aim recoils after getting shot in the head
		CVector aimPunch;
		globals::localPlayer->GetAimPunch(aimPunch);

		// calculate destination of the ray
		const CVector dst = eyePosition + CVector{ cmd->viewAngles + aimPunch }.ToVector() * 1000.f;

		// trace the ray from eyes->dest
		CTrace trace;
		interfaces::engineTrace->TraceRay({ eyePosition, dst }, 0x46004009, globals::localPlayer, trace);

		// make sure we hit a player
		if (!trace.entity || !trace.entity->IsPlayer())
			return false;

		// make sure player is alive and is an enemy
		if (!trace.entity->IsAlive() || trace.entity->GetTeam() == globals::localPlayer->GetTeam())
			return false;

		// make local player shoot
		cmd->buttons |= CUserCmd::ECommandButton::IN_ATTACK;

		return false;
	}


	return false;
}

// our hook
void __stdcall hooks::DoPostScreenSpaceEffects(const void* viewSetup) noexcept {

	// make sure local player is valid && we are ingame
	if (globals::localPlayer && interfaces::engine->IsInGame()) {

		// loop through glow objects
		for (int i = 0; i < interfaces::glow->glowObjects.size; ++i) {
			//get glow object
			IGlowManager::CGlowObject& glowObject = interfaces::glow->glowObjects[i];

			// make sure it is used
			if (glowObject.IsUnused())
				continue;
			// make sure we have a valid entity
			if (!glowObject.entity)
				continue;

			// check class index of the entity
			switch (glowObject.entity->GetClientClass()->classID) {
			// entity is a player
			case CClientClass::CCSPlayer:
				// make sure they are alive
				if (!glowObject.entity->IsAlive())
					break;

				// check what team enemies are on
				if (glowObject.entity->GetTeam() != globals::localPlayer->GetTeam()) {
					// make them red
					glowObject.SetColor(1.f, 0.f, 0.f);
				}
				else {
					// make teammates blue
					glowObject.SetColor(0.f, 0.f, 1.f);
				}

				// it is possible to make other entities glow e.g. grenades, guns
				break;
			default:
				break;
			}
		}
	}

	// call the original function
	DoPostScreenSpaceEffectsOriginal(interfaces::clientMode, viewSetup);
}

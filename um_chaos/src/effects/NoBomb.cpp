#include "effect.hpp"
#include "util.hpp"

class NoBomb : public Effect {
public:
    int timer = Rand::RangeFrames(10, 2 * 60);
    CodePatches patches;
    AnmVM* disable_vm = nullptr;

    NoBomb() {
        uint8_t patch[] = {0x33, 0xC0, 0xC3}; // xor eax, eax; ret
        patches.Add(0x420420, patch, sizeof(patch));

        uint32_t id;
        auto anm = AnmManager::Instance->Preload(18, "chaos.anm");
        disable_vm = AnmManager::AllocateVM();
        anm->MakeVM(disable_vm, 6);
        AnmManager::AddToUIBack(&id, disable_vm);
    }

    virtual ~NoBomb() {
        AnmVM::QueueDeletion(disable_vm);
    }

    virtual bool Update() {
        return --timer > 0;
    }
};
REGISTER_EFFECT(NoBomb);

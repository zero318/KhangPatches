#include "effect.hpp"
#include "util.hpp"

class DoubleBulletCount : public Effect {
public:
    static CodeCave Cave1;
    static CodeCave Cave2;
    static CodeCave Cave3;

    size_t timer = Rand::RangeFrames(10, 2 * 60);
    CodePatches patches;

    DoubleBulletCount() {
        patches.AddJmp(0x434277, &Cave1); // etCount
        patches.AddJmp(0x4346D1, &Cave2); // etCountR3 and etCountR5
        patches.AddJmp(0x43482A, &Cave3); // etCountR2
        patches.AddJmp(0x4343D1, &Cave1); // etCountD
    }

    virtual bool Update() {
        return --timer != 0;
    }
};
REGISTER_EFFECT(DoubleBulletCount);

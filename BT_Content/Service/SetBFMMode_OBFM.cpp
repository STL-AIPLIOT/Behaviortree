#include "SetBFMMode_OBFM.h"
#include <algorithm>

namespace Action {  // ★ 추가

    static inline float clampf(float v, float lo, float hi) {
        return std::max(lo, std::min(hi, v));
    }

    BT::NodeStatus SetBFMMode_OBFM::tick() {
        auto bb_ptr = getInput<CPPBlackBoard*>("BB");
        if (!bb_ptr) {
            std::cerr << "[SetBFMMode_OBFM] BB nullptr\n";
            return BT::NodeStatus::FAILURE;
        }
        CPPBlackBoard* BB = bb_ptr.value();

        const bool sight = BB->EnemyInSight;
        const bool e_sup = (BB->EnergyCompareResult > 0);
        const float AA = BB->MyAspectAngle_Degree;
        const float D = BB->Distance;

        const bool aa_ok = (AA < 35.0f);                   // [변경]
        const bool dist_ok = (D >= 400.0f && D <= 1500.0f);  // [변경]

        if (sight && e_sup && aa_ok && dist_ok) {
            BB->BFM = OBFM;
            std::cout << "[SetBFMMode_OBFM] t=" << BB->RunningTime << "s | Enter OBFM (AA=" << AA
                << ", D=" << D << ", EnergySup=" << e_sup << ")\n";
            return BT::NodeStatus::SUCCESS;
        }

        std::cout << "[SetBFMMode_OBFM] t=" << BB->RunningTime << "s | Blocked: sight=" << sight
            << ", e_sup=" << e_sup
            << ", AA=" << AA
            << ", D=" << D << "\n";
        return BT::NodeStatus::FAILURE;
    }

} // namespace Action

#include "EnergyCompare.h"

namespace Action
{
	EnergyCompare::EnergyCompare(const std::string& name, const NodeConfiguration& config)
		: SyncActionNode(name, config)
	{
	}

	EnergyCompare::~EnergyCompare() {}

	PortsList EnergyCompare::providedPorts()
	{
		return {
			InputPort<CPPBlackBoard*>("BB")
		};
	}

	NodeStatus EnergyCompare::tick()
	{
		Optional<CPPBlackBoard*> BB = getInput<CPPBlackBoard*>("BB");
		if (!BB || !(*BB)) return NodeStatus::FAILURE;

		const float g = 9.81f;

		float myV = (*BB)->MySpeed_MS;
		float myH = static_cast<float>((*BB)->MyLocation_Cartesian.Z);

		float targetV = (*BB)->TargetSpeed_MS;
		float targetH = static_cast<float>((*BB)->TargetLocaion_Cartesian.Z);

		float myEnergy = myV * myV + 2 * g * myH;
		float targetEnergy = targetV * targetV + 2 * g * targetH;

		(*BB)->IsEnergySuperior = (myEnergy > targetEnergy);
		(*BB)->IsEnergyInferior = (myEnergy < targetEnergy);
		(*BB)->EnergyCompareResult = (myEnergy > targetEnergy) ? 1 : (myEnergy < targetEnergy ? -1 : 0);

		return NodeStatus::SUCCESS;
	}
}

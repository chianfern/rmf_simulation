#include <ignition/plugin/Register.hh>

#include <ignition/gazebo/System.hh>
#include <ignition/gazebo/Model.hh>
#include <ignition/gazebo/Util.hh>
#include <ignition/gazebo/components/JointPosition.hh>
#include <ignition/gazebo/components/JointVelocity.hh>
#include <ignition/gazebo/components/JointVelocityCmd.hh>

#include <rclcpp/rclcpp.hpp>

#include <rmf_building_sim_common/utils.hpp>
#include <rmf_building_sim_common/door_common.hpp>

using namespace ignition::gazebo;

using namespace rmf_building_sim_common;

namespace rmf_building_sim_ignition_plugins {

//==============================================================================

class IGNITION_GAZEBO_VISIBLE DoorPlugin
  : public System,
  public ISystemConfigure,
  public ISystemPreUpdate
{
private:
  rclcpp::Node::SharedPtr _ros_node;
  std::unordered_map<std::string, Entity> _joints;

  std::shared_ptr<DoorCommon> _door_common = nullptr;

  bool _initialized = false;
  bool _first_iteration = true;

  void create_entity_components(Entity entity, EntityComponentManager& ecm)
  {
    enableComponent<components::JointPosition>(ecm, entity);
    enableComponent<components::JointVelocity>(ecm, entity);
    enableComponent<components::JointVelocityCmd>(ecm, entity);
  }

public:
  DoorPlugin()
  {
  }

  void Configure(const Entity& entity,
    const std::shared_ptr<const sdf::Element>& sdf,
    EntityComponentManager& ecm, EventManager& /*_eventMgr*/) override
  {
    // TODO proper rclcpp init (only once and pass args)
    auto model = Model(entity);
    char const** argv = NULL;
    std::string name;
    auto door_ele = sdf->GetElementImpl("door");
    get_sdf_attribute_required<std::string>(door_ele, "name", name);
    if (!rclcpp::ok())
      rclcpp::init(0, argv);
    std::string plugin_name("plugin_" + name);
    _ros_node = std::make_shared<rclcpp::Node>(plugin_name);

    RCLCPP_INFO(_ros_node->get_logger(),
      "Loading DoorPlugin for [%s]",
      name.c_str());

    _door_common = DoorCommon::make(
      name,
      _ros_node,
      sdf);

    if (!_door_common)
      return;

    for (const auto& joint_name : _door_common->joint_names())
    {
      const auto joint = model.JointByName(ecm, joint_name);
      if (!joint)
      {
        RCLCPP_ERROR(_ros_node->get_logger(),
          " -- Model is missing the joint [%s]",
          joint_name.c_str());
        return;
      }
      create_entity_components(joint, ecm);
      _joints.insert({joint_name, joint});
    }

    _initialized = true;

    RCLCPP_INFO(_ros_node->get_logger(),
      "Finished loading [%s]",
      name.c_str());
  }

  void PreUpdate(const UpdateInfo& info, EntityComponentManager& ecm) override
  {
    rclcpp::spin_some(_ros_node);
    // JointPosition and JointVelocity components are populated by Physics
    // system in Update, hence they are uninitialized in the first PreUpdate.
    if (!_initialized || _first_iteration)
    {
      _first_iteration = false;
      return;
    }

    // Don't update the pose if the simulation is paused
    if (info.paused)
      return;

    double t =
      (std::chrono::duration_cast<std::chrono::nanoseconds>(info.simTime).
      count()) * 1e-9;

    // Create DoorUpdateRequest
    std::vector<DoorCommon::DoorUpdateRequest> requests;
    for (const auto& joint : _joints)
    {
      DoorCommon::DoorUpdateRequest request;
      request.joint_name = joint.first;
      request.position = ecm.Component<components::JointPosition>(
        joint.second)->Data()[0];
      request.velocity = ecm.Component<components::JointVelocity>(
        joint.second)->Data()[0];
      requests.push_back(request);
    }

    auto results = _door_common->update(t, requests);

    // Apply motions to the joints
    for (const auto& result : results)
    {
      const auto it = _joints.find(result.joint_name);
      assert(it != _joints.end());
      auto vel_cmd = ecm.Component<components::JointVelocityCmd>(
        it->second);
      vel_cmd->Data() = {result.velocity};
    }
  }

};

IGNITION_ADD_PLUGIN(
  DoorPlugin,
  System,
  DoorPlugin::ISystemConfigure,
  DoorPlugin::ISystemPreUpdate)

IGNITION_ADD_PLUGIN_ALIAS(DoorPlugin, "door")

} // namespace rmf_building_sim_ignition_plugins

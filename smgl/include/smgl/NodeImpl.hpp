#include "smgl/Utilities.hpp"

namespace smgl
{

void Node::update()
{
    // Check if inputs have updated
    if (!update_input_ports_()) {
        return;
    }

    // Compute
    notify_output_ports_(Port::Status::Waiting);
    if (compute) {
        compute();
    }

    // Update outputs
    update_output_ports_();
}

Metadata Node::serialize(bool useCache, const filesystem::path& cacheRoot)
{
    Metadata meta;
    meta["type"] = NodeName(this);
    meta["uuid"] = uuid_.string();

    // Construct the cache dir if needed
    auto nodeCache = cacheRoot / uuid_.string();
    if (useCache and not filesystem::exists(nodeCache)) {
        filesystem::create_directories(nodeCache);
    }

    // Serialize port info
    for (const auto& ip : inputs_by_name_) {
        meta["inputPorts"][ip.first] = ip.second->serialize();
    }
    for (const auto& op : outputs_by_name_) {
        meta["outputPorts"][op.first] = op.second->serialize();
    }

    // Serialize the node
    meta["data"] = serialize_(useCache, nodeCache);

    return meta;
}

void Node::deserialize(const Metadata& meta, const filesystem::path& cacheRoot)
{
    uuid_ = Uuid::FromString(meta["uuid"].get<std::string>());

    // Deserialize port info
    for (const auto& n : meta["inputPorts"].items()) {
        LoadAndRegisterPort(
            n.key(), n.value(), inputs_by_uuid_, inputs_by_name_);
    }
    for (const auto& n : meta["outputPorts"].items()) {
        LoadAndRegisterPort(
            n.key(), n.value(), outputs_by_uuid_, outputs_by_name_);
    }

    // Load custom node state
    auto nodeCache = cacheRoot / meta["uuid"].get<std::string>();
    deserialize_(meta["data"], nodeCache);
}

template <class PortType>
void Node::LoadAndRegisterPort(
    const std::string& name,
    const Metadata& data,
    std::unordered_map<Uuid, PortType*>& byUuid,
    std::unordered_map<std::string, PortType*>& byName)
{
    // Get old port info
    auto* port = byName[name];
    auto oldUuid = port->uuid();

    // Load old port data
    port->deserialize(data);

    // Delete old port registration
    auto nIt = byName.find(name);
    assert(nIt != byName.end());
    byName.erase(nIt);

    auto uIt = byUuid.find(oldUuid);
    assert(uIt != byUuid.end());
    byUuid.erase(uIt);

    // Reregister ports
    byName[name] = port;
    byUuid[port->uuid()] = port;
}

Input& Node::getInputPort(const Uuid& uuid)
{
    return *inputs_by_uuid_.at(uuid);
}

Input& Node::getInputPort(const std::string& name)
{
    return *inputs_by_name_.at(name);
}

Output& Node::getOutputPort(const Uuid& uuid)
{
    return *outputs_by_uuid_.at(uuid);
}

Output& Node::getOutputPort(const std::string& name)
{
    return *outputs_by_name_.at(name);
}

std::vector<Connection> Node::getInputConnections() const
{
    std::vector<Connection> cns;
    for (const auto& ip : inputs_by_name_) {
        auto pcns = ip.second->getConnections();
        cns.insert(cns.end(), pcns.begin(), pcns.end());
    }
    return cns;
}

size_t Node::getNumberOfInputConnections() const
{
    size_t cns{0};
    for (const auto& n : inputs_by_name_) {
        cns += n.second->numConnections();
    }
    return cns;
}

std::vector<Connection> Node::getOutputConnections() const
{
    std::vector<Connection> cns;
    for (const auto& op : outputs_by_name_) {
        auto pcns = op.second->getConnections();
        cns.insert(cns.end(), pcns.begin(), pcns.end());
    }
    return cns;
}

size_t Node::getNumberOfOutputConnections() const
{
    size_t cns{0};
    for (const auto& n : outputs_by_name_) {
        cns += n.second->numConnections();
    }
    return cns;
}

Node::Status Node::status()
{
    // TODO: Lock internal status
    if (status_ == Status::Updating or status_ == Status::Error) {
        return status_;
    }

    // Check status of input ports
    auto queued = false;
    for (const auto& ip : inputs_by_name_) {
        auto status = ip.second->status();

        // Can break early if any are waiting
        if (status == Port::Status::Waiting) {
            return Status::Waiting;
        }

        // Give all ports a chance to be waiting, so keep track
        // of whether any port is queued
        queued |= status == Port::Status::Queued;
    }

    // Return port statuses
    if (queued) {
        return Status::Ready;
    } else {
        return Status::Idle;
    }
}

Metadata Node::serialize_(bool useCache, const filesystem::path& cacheDir)
{
    return Metadata::object();
}

void Node::deserialize_(const Metadata& data, const filesystem::path& cacheDir)
{
}

bool Node::update_input_ports_()
{
    auto res = false;
    for (const auto& p : inputs_by_name_) {
        res |= p.second->update();
    }
    return res;
}

void Node::notify_output_ports_(Port::Status s)
{
    for (const auto& p : outputs_by_name_) {
        p.second->notify(s);
    }
}

bool Node::update_output_ports_()
{
    auto res = false;
    for (const auto& p : outputs_by_name_) {
        p.second->setStatus(Port::Status::Idle);
        res |= p.second->update();
    }
    return res;
}

template <typename T>
void Node::registerInputPort(const std::string& name, InputPort<T>& port)
{
    port.setParent(this);
    inputs_by_uuid_[port.uuid()] = &port;
    assert(inputs_by_name_.find(name) == inputs_by_name_.end());
    inputs_by_name_[name] = &port;
}

template <typename T, typename... Args>
void Node::registerOutputPort(
    const std::string& name, OutputPort<T, Args...>& port)
{
    port.setParent(this);
    outputs_by_uuid_[port.uuid()] = &port;
    assert(outputs_by_name_.find(name) == outputs_by_name_.end());
    outputs_by_name_[name] = &port;
}

template <class T>
bool RegisterNode()
{
    return RegisterNode<T>(detail::type_name<T>());
}

template <class T>
bool RegisterNode(const std::string& name)
{
    return detail::NodeFactoryType::Instance().Register(
        name, []() { return std::make_shared<T>(); }, typeid(T));
}

template <class T>
bool DeregisterNode()
{
    auto name =
        detail::NodeFactoryType::Instance().GetTypeIdentifier(typeid(T));
    return detail::NodeFactoryType::Instance().Deregister(name);
}

bool DeregisterNode(const std::string& name)
{
    return detail::NodeFactoryType::Instance().Deregister(name);
}

std::shared_ptr<Node> CreateNode(const std::string& name)
{
    return detail::NodeFactoryType::Instance().CreateObject(name);
}

std::string NodeName(const Node::Pointer& node)
{
    if (node == nullptr) {
        throw std::invalid_argument("node is a nullptr");
    }
    auto& n = *node.get();
    return detail::NodeFactoryType::Instance().GetTypeIdentifier(typeid(n));
}

std::string NodeName(const Node* node)
{
    if (node == nullptr) {
        throw std::invalid_argument("node is a nullptr");
    }
    return detail::NodeFactoryType::Instance().GetTypeIdentifier(typeid(*node));
}

}  // namespace smgl
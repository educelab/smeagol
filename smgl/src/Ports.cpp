#include "smgl/Ports.hpp"

using namespace smgl;

///////////////////////////
///// Port Connection /////
///////////////////////////

void smgl::connect(Output& op, Input& ip)
{
    // op->ip verifies that ports are compatible, so do it first
    op.connect(&ip);
    ip.connect(&op);
}
void smgl::disconnect(Output& op, Input& ip)
{
    op.disconnect(&ip);
    ip.disconnect(&op);
}

void smgl::operator>>(Output& op, Input& ip) { connect(op, ip); }

void smgl::operator<<(Input& ip, Output& op) { connect(op, ip); }

////////////////
///// Port /////
////////////////

Port::Port(Port::Status s) : status_{s} {}

void Port::setParent(Node* p) { parent_ = p; }

Port::Status Port::status() const { return status_; }

void Port::setStatus(Port::Status s) { status_ = s; }

//////////////////
///// Output /////
//////////////////

Output::Output() : Port(Status::Waiting) {}

/////////////////
///// Input /////
/////////////////

Input::Input() : Port(Status::Idle) {}

Input::~Input()
{
    if (src_) {
        src_->disconnect(this);
    }
}

std::vector<Connection> Input::getConnections() const
{
    if (src_) {
        return {{src_->parent_, src_, parent_, const_cast<Input*>(this)}};
    } else {
        return {};
    }
}

size_t Input::numConnections() const { return (src_) ? 1 : 0; }

void Input::connect(Output* op) { src_ = op; }

void Input::disconnect(Output* op)
{
    if (src_ and src_ == op) {
        src_ = nullptr;
    }
}

#include "_editor.hh"

namespace {

struct state_t
{
};

}

namespace Editor
{

Interface::Interface()
    : state{ new state_t{} }
{ ReloadPointers(); }

Interface::~Interface()
{
    delete (state_t*)state;
}

void Interface::ReloadPointers()
{
}

}

#include <iostream>
#include <signal.h>
#include <tbox/event/loop.h>
#include <tbox/event/signal_item.h>

using namespace std;
using namespace tbox;
using namespace tbox::event;

void SignalCallback()
{
    cout << "Got interupt signal" << endl;
}

void PrintUsage(const char *process_name)
{
    cout << "Usage:" << process_name << " libevent|libev" << endl;
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        PrintUsage(argv[0]);
        return 0;
    }

    Loop::Engine loop_engine;
    if (string(argv[1]) == "libevent")
        loop_engine = Loop::Engine::kLibevent;
    else if (string(argv[1]) == "libev")
        loop_engine = Loop::Engine::kLibev;
    else {
        PrintUsage(argv[0]);
        return 0;
    }

    Loop* sp_loop = Loop::New(loop_engine);
    if (sp_loop == nullptr) {
        cout << "fail, exit" << endl;
        return 0;
    }

    SignalItem* sp_signal = sp_loop->newSignalItem();
    sp_signal->initialize(SIGINT, Item::Mode::kPersist);
    sp_signal->setCallback(SignalCallback);
    sp_signal->enable();

    cout << "Please ctrl+c" << endl;
    sp_loop->runLoop(Loop::Mode::kForever);

    delete sp_signal;
    delete sp_loop;
    return 0;
}
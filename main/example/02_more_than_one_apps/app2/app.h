#ifndef TBOX_MAIN_EXAMPLE_APP2_H_20211226
#define TBOX_MAIN_EXAMPLE_APP2_H_20211226

#include <tbox/main/main.h>

namespace app2 {

class App : public tbox::main::App
{
  public:
    App(tbox::main::Context &ctx);
    ~App();

    bool initialize() override;
    bool start() override;
    void stop() override;
    void cleanup() override;
};

}

#endif //TBOX_MAIN_EXAMPLE_APP2_H_20211226
#ifndef TBOX_LOG_OUTPUT_STDOUT_H_20220408
#define TBOX_LOG_OUTPUT_STDOUT_H_20220408

#include "async_channel.h"

namespace tbox {
namespace log {

class StdoutChannel : public AsyncChannel {
  public:
    bool initialize();

  protected:
    virtual void writeLog(const char *str, size_t len) override;
};

}
}

#endif //TBOX_LOG_OUTPUT_STDOUT_H_20220408

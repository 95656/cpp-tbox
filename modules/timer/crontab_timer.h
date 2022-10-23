#ifndef TBOX_TIMER_CRON_TIMER_H
#define TBOX_TIMER_CRON_TIMER_H

#include <string>

#include <functional>
#include <tbox/event/forward.h>

#include "timer.h"

namespace tbox {
namespace timer {

/*
 * @brief The linux crontab timer.
 *
 * CrontabTimer allow the user to make plans by linux crontab expression.
 *  Linux-cron tab expression
  *    *    *    *    *    *
  -    -    -    -    -    -
  |    |    |    |    |    |
  |    |    |    |    |    +----- day of week (0 - 7) (Sunday=0 or 7) OR sun,mon,tue,wed,thu,fri,sat
  |    |    |    |    +---------- month (1 - 12) OR jan,feb,mar,apr ...
  |    |    |    +--------------- day of month (1 - 31)
  |    |    +-------------------- hour (0 - 23)
  |    +------------------------- minute (0 - 59)
  +------------------------------ second (0 - 59)

 *
 * code example:
 *
 * Loop *loop = Loop::New();
 *
 * CrontabTimer tmr(loop);
 * tmr.initialize("18 28 14 * * *"); // every day at 14:28:18
 * tmr.setCallback([] { std::cout << "timeout" << std::endl; });
 * tmr.enable();
 *
 * loop->runLoop();
 */
class CrontabTimer : public Timer
{
  public:
    explicit CrontabTimer(event::Loop *wp_loop);
    virtual ~CrontabTimer();

    bool initialize(const std::string &crontab_expr);

  protected:
    virtual int calculateWaitSeconds(uint32_t curr_local_ts) override;

  private:
    void *sp_cron_expr_;
};

}
}

#endif //TBOX_TIMER_CRON_TIMER_H

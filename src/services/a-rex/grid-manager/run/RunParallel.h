#ifndef GRID_MANAGER_RUN_PARALLEL_H
#define GRID_MANAGER_RUN_PARALLEL_H

#include <arc/Run.h>

#include "../jobs/JobsList.h"
#include "RunPlugin.h"

namespace ARex {

/// Run child process in parallel with stderr redirected to job.jobid.errors
class RunParallel {
 private:
  RunParallel(const GMConfig& config,const Arc::User& user,const char* jobid,bool su,bool job_proxy,RunPlugin* cred,RunPlugin::substitute_t subst,void* subst_arg)
    :config_(config),user_(user),jobid_(jobid?jobid:""),su_(su),job_proxy_(job_proxy),cred_(cred),subst_(subst),subst_arg_(subst_arg) { };
  ~RunParallel(void) { };
  const GMConfig& config_;
  const Arc::User& user_;
  std::string jobid_;
  bool su_;
  bool job_proxy_;
  RunPlugin* cred_;
  RunPlugin::substitute_t subst_;
  void* subst_arg_;
  static void initializer(void* arg);
  // TODO: no static variables
  static void (*kicker_func_)(void*);
  static void* kicker_arg_;
 public:
  operator bool(void) const { return true; };
  bool operator!(void) const { return false; };
  static bool run(const GMConfig& config,const Arc::User& user,const char* jobid,const std::string& args,Arc::Run**,bool su = true,bool job_proxy = true, RunPlugin* cred = NULL, RunPlugin::substitute_t subst = NULL, void* subst_arg = NULL);
  static bool run(const GMConfig& config,const GMJob& job,const std::string& args,Arc::Run**,bool su = true);
  static void kicker(void (*kicker_func)(void*),void* kicker_arg) {
    kicker_arg_=kicker_arg;
    kicker_func_=kicker_func;
  };
};

} // namespace ARex

#endif
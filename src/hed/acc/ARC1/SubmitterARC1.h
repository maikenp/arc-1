// -*- indent-tabs-mode: nil -*-

#ifndef __ARC_SUBMITTERARC1_H__
#define __ARC_SUBMITTERARC1_H__

#include <arc/client/Submitter.h>
#include <arc/XMLNode.h>
#include <arc/URL.h>
#include <arc/loader/Loader.h>
#include <arc/client/ClientInterface.h>

namespace Arc {

  class Config;

  class SubmitterARC1
    : public Submitter {

  private:
    static Logger logger;

    SubmitterARC1(const UserConfig& usercfg);
    ~SubmitterARC1();

  public:
    static Plugin* Instance(PluginArgument *arg);
    URL Submit(const JobDescription& jobdesc,
               const ExecutionTarget& et) const;
    URL Migrate(const URL& jobid, const JobDescription& jobdesc,
                const ExecutionTarget& et,
                bool forcemigration) const;
    bool ModifyJobDescription(JobDescription& jobdesc, const ExecutionTarget& et) const;
  };

} // namespace Arc

#endif // __ARC_SUBMITTERARC1_H__

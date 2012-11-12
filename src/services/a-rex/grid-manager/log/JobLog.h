/* write essential inforamtion about job started/finished */
#ifndef __GM_JOB_LOG_H__
#define __GM_JOB_LOG_H__

#include <string>
#include <list>
#include <fstream>

#include <arc/Run.h>

#include "../jobs/GMJob.h"

namespace ARex {

class GMConfig;
class JobLocalDescription;

///  Put short information into log when every job starts/finishes.
///  And store more detailed information for Reporter.
class JobLog {
 private:
  std::string filename;
  std::list<std::string> urls;
  std::list<std::string> report_config; // additional configuration for usage reporter
  std::string certificate_path;
  std::string ca_certificates_dir;
  std::string logger;
  Arc::Run *proc;
  time_t last_run;
  time_t ex_period;
  bool open_stream(std::ofstream &o);
 public:
  JobLog(void);
  //JobLog(const char* fname);
  ~JobLog(void);
  /* chose name of log file */
  void SetOutput(const char* fname);
  /* log job start information */
  bool start_info(GMJob &job,const GMConfig &config);
  /* log job finish iformation */
  bool finish_info(GMJob &job,const GMConfig& config);
  /* read information stored by start_info and finish_info */
  static bool read_info(std::fstream &i,bool &processed,bool &jobstart,struct tm &t,JobId &jobid,JobLocalDescription &job_desc,std::string &failure);
  bool is_reporting(void) { return (!urls.empty()); };
  /* Run external utility to report gathered information to logger service */
  bool RunReporter(const GMConfig& config);
  /* Set name of the accounting reporter */
  bool SetLogger(const char* fname);
  /* Set url of service and local name to use */
  // bool SetReporter(const char* destination,const char* name = NULL);
  bool SetReporter(const char* destination);
  /* Set after which too old logger information is removed */
  void SetExpiration(time_t period = 0);
  /* Create data file for Reporter */
  bool make_file(GMJob &job,const GMConfig &config);
  /* Set credential file names for accessing logging service */
  void set_credentials(std::string &key_path,std::string &certificate_path,std::string &ca_certificates_dir);
  /* Set accounting options (e.g. batch size for SGAS LUTS) */
  void set_options(std::string &options) { report_config.push_back(std::string("accounting_options=")+options); }
};

} // namespace ARex

#endif
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <iostream>
#include <pwd.h>

#include <arc/StringConv.h>
#include <arc/Utils.h>
#include <arc/XMLNode.h>
#include "../jobs/users.h"
#include "../jobs/states.h"
#include "../jobs/plugins.h"
#include "conf.h"
#include "conf_sections.h"
#include "environment.h"
#include "gridmap.h"
#include "../run/run_plugin.h"
#include "conf_cache.h"
#include "conf_file.h"

static Arc::Logger& logger = Arc::Logger::getRootLogger();

JobLog job_log;
ContinuationPlugins plugins;
RunPlugin cred_plugin;

static void check_lrms_backends(const std::string& default_lrms) {
  std::string tool_path;
  tool_path=nordugrid_libexec_loc()+"/cancel-"+default_lrms+"-job";
  if(!Glib::file_test(tool_path,Glib::FILE_TEST_IS_REGULAR)) {
    logger.msg(Arc::WARNING,"Missing cancel-%s-job - job cancelation may not work",default_lrms);
  };
  tool_path=nordugrid_libexec_loc()+"/submit-"+default_lrms+"-job";
  if(!Glib::file_test(tool_path,Glib::FILE_TEST_IS_REGULAR)) {
    logger.msg(Arc::WARNING,"Missing submit-%s-job - job submission to LRMS may not work",default_lrms);
  };
  tool_path=nordugrid_libexec_loc()+"/scan-"+default_lrms+"-job";
  if(!Glib::file_test(tool_path,Glib::FILE_TEST_IS_REGULAR)) {
    logger.msg(Arc::WARNING,"Missing scan-%s-job - may miss when job finished executing",default_lrms);
  };
}

bool configure_serviced_users(JobUsers &users,uid_t my_uid,const std::string &my_username,JobUser &my_user,Daemon* daemon) {
  std::ifstream cfile;
  std::string session_root("");
  std::string default_lrms("");
  std::string default_queue("");
  std::string last_control_dir("");
  time_t default_ttl = DEFAULT_KEEP_FINISHED;
  time_t default_ttr = DEFAULT_KEEP_DELETED;
  int default_reruns = DEFAULT_JOB_RERUNS;
  int default_diskspace = DEFAULT_DISKSPACE;
  bool superuser = (my_uid == 0);
  bool strict_session = false;
  std::string central_control_dir("");
  std::string infosys_user("");
  std::string jobreport_key("");
  std::string jobreport_cert("");
  std::string jobreport_cadir("");

  /* read configuration and add users and other things */
  if(!config_open(cfile)) {
    logger.msg(Arc::ERROR,"Can't read configuration file"); return false;
  };
  /* detect type of file */
  switch(config_detect(cfile)) {
    case config_file_XML: {
      Arc::XMLNode cfg;
      if(!cfg.ReadFromStream(cfile)) {
        config_close(cfile);
        logger.msg(Arc::ERROR,"Can't interpret configuration file as XML");
        return false;
      }; 
      config_close(cfile);
      return configure_serviced_users(cfg,users,my_uid,my_username,my_user);
    }; break;
    case config_file_INI: {
      // Fall through. TODO: make INI processing a separate function.
    }; break;
    default: {
      logger.msg(Arc::ERROR,"Can't recognize type of configuration file"); return false;
    }; break;
  };
  ConfigSections* cf = new ConfigSections(cfile);
  cf->AddSection("common");
  cf->AddSection("grid-manager");
  cf->AddSection("infosys");
  /* process configuration information here */
  for(;;) {
    std::string rest;
    std::string command;
    cf->ReadNext(command,rest);
    if(cf->SectionNum() == 2) { // infosys - looking for user name only
      if(command == "user") infosys_user=rest;
      continue;
    };
    if(cf->SectionNum() == 0) { // infosys user may be in common too
      if(command == "user") infosys_user=rest;
    };
    if(daemon) {
      int r = daemon->config(command,rest);
      if(r == 0) continue;
      if(r == -1) goto exit;
    } else {
      int r = Daemon::skip_config(command);
      if(r == 0) continue;
    };
    if(command.length() == 0) {
      if(central_control_dir.length() != 0) {
        command="control"; rest=central_control_dir+" .";
        central_control_dir="";
      } else {
        break;
      };
    };
    if(command == "runtimedir") { 
      runtime_config_dir(rest);
    } else if(command == "joblog") { /* where to write job inforamtion */ 
      std::string fname = config_next_arg(rest);  /* empty is allowed too */
      job_log.SetOutput(fname.c_str());
    }
    else if(command == "jobreport") { /* service to report information to */ 
      for(;;) {
        std::string url = config_next_arg(rest); 
        if(url.length() == 0) break;
        unsigned int i;
        if(Arc::stringto(url,i)) {
          job_log.SetExpiration(i);
          continue;
        };
        job_log.SetReporter(url.c_str());
      };
    }
    else if(command == "jobreport_credentials") {
      jobreport_key = config_next_arg(rest); 
      jobreport_cert = config_next_arg(rest); 
      jobreport_cadir = config_next_arg(rest); 
    }
    else if(command == "jobreport_options") { /* e.g. for SGAS, interpreted by usage reporter */ 
      std::string accounting_options = config_next_arg(rest); 
      job_log.set_options(accounting_options);
    }
    else if(command == "maxjobs") { /* maximum number of the jobs to support */ 
      std::string max_jobs_s = config_next_arg(rest);
      long int i;
      int max_jobs, max_jobs_running = -1;
      if(max_jobs_s.length() != 0) {
        if(!Arc::stringto(max_jobs_s,i)) {
          logger.msg(Arc::ERROR,"wrong number in maxjobs: %s",max_jobs_s);
          goto exit;
        };
        if(i<0) i=-1; max_jobs=i;
      };
      max_jobs_s = config_next_arg(rest);
      if(max_jobs_s.length() != 0) {
        if(!Arc::stringto(max_jobs_s,i)) {
          logger.msg(Arc::ERROR,"wrong number in maxjobs: %s",max_jobs_s);
          goto exit;
        };
        if(i<0) i=-1; max_jobs_running=i;
      };
      JobsList::SetMaxJobs(
              max_jobs,max_jobs_running);
    }
    else if(command == "maxload") { /* maximum number of the jobs processed on frontend */
      std::string max_jobs_s = config_next_arg(rest);
      long int i;
      int max_jobs_processing, max_jobs_processing_emergency, max_downloads = -1;
      if(max_jobs_s.length() != 0) {
        if(!Arc::stringto(max_jobs_s,i)) {
          logger.msg(Arc::ERROR,"wrong number in maxload: %s",max_jobs_s);
          goto exit;
        };
        if(i<0) i=-1; max_jobs_processing=i;
      };
      max_jobs_s = config_next_arg(rest);
      if(max_jobs_s.length() != 0) {
        if(!Arc::stringto(max_jobs_s,i)) {
          logger.msg(Arc::ERROR,"wrong number in maxload: %s",max_jobs_s);
          goto exit;
        };
        if(i<0) i=-1; max_jobs_processing_emergency=i;
      };
      max_jobs_s = config_next_arg(rest);
      if(max_jobs_s.length() != 0) {
        if(!Arc::stringto(max_jobs_s,i)) {
          logger.msg(Arc::ERROR,"wrong number in maxload: %s",max_jobs_s);
          goto exit;
        };
        if(i<0) i=-1; max_downloads=i;
      };
      JobsList::SetMaxJobsLoad(
              max_jobs_processing,max_jobs_processing_emergency,max_downloads);
    }
    else if(command == "speedcontrol") {  
      std::string speed_s = config_next_arg(rest);
      int min_speed=0;
      int min_speed_time=300;
      int min_average_speed=0;
      int max_inactivity_time=300;
      if(speed_s.length() != 0) {
        if(!Arc::stringto(speed_s,min_speed)) {
          logger.msg(Arc::ERROR,"wrong number in speedcontrol: %s",speed_s);
          goto exit;
        };
      };
      speed_s = config_next_arg(rest);
      if(speed_s.length() != 0) {
        if(!Arc::stringto(speed_s,min_speed_time)) {
          logger.msg(Arc::ERROR,"wrong number in speedcontrol: ",speed_s);
          goto exit;
        };
      };
      speed_s = config_next_arg(rest);
      if(speed_s.length() != 0) {
        if(!Arc::stringto(speed_s,min_average_speed)) {
          logger.msg(Arc::ERROR,"wrong number in speedcontrol: %s",speed_s);
          goto exit;
        };
      };
      speed_s = config_next_arg(rest);
      if(speed_s.length() != 0) {
        if(!Arc::stringto(speed_s,max_inactivity_time)) {
          logger.msg(Arc::ERROR,"wrong number in speedcontrol: %s",speed_s);
          goto exit;
        };
      };
      JobsList::SetSpeedControl(
              min_speed,min_speed_time,min_average_speed,max_inactivity_time);
    }
    else if(command == "wakeupperiod") { 
      std::string wakeup_s = config_next_arg(rest);
      unsigned int wakeup_period;
      if(wakeup_s.length() != 0) {
        if(!Arc::stringto(wakeup_s,wakeup_period)) {
          logger.msg(Arc::ERROR,"wrong number in wakeupperiod: %s",wakeup_s);
          goto exit;
        };
        JobsList::SetWakeupPeriod(wakeup_period);
      };
    }
    else if(command == "securetransfer") {
      std::string s = config_next_arg(rest);
      bool use_secure_transfer;
      if(strcasecmp("yes",s.c_str()) == 0) {
        use_secure_transfer=true;
      } 
      else if(strcasecmp("no",s.c_str()) == 0) {
        use_secure_transfer=false;
      }
      else {
        logger.msg(Arc::ERROR,"wrong option in securetransfer"); goto exit;
      };
      JobsList::SetSecureTransfer(use_secure_transfer);
    }
    else if(command == "passivetransfer") {
      std::string s = config_next_arg(rest);
      bool use_passive_transfer;
      if(strcasecmp("yes",s.c_str()) == 0) {
        use_passive_transfer=true;
      }
      else if(strcasecmp("no",s.c_str()) == 0) {
        use_passive_transfer=false;
      }
      else {
        logger.msg(Arc::ERROR,"wrong option in passivetransfer"); goto exit;
      };
      JobsList::SetPassiveTransfer(use_passive_transfer);
    }
    else if(command == "norootpower") {
      std::string s = config_next_arg(rest);
      if(strcasecmp("yes",s.c_str()) == 0) {
        strict_session=true;
      }
      else if(strcasecmp("no",s.c_str()) == 0) {
        strict_session=false;
      }
      else {
        logger.msg(Arc::ERROR,"wrong option in norootpower"); goto exit;
      };
    }
    else if(command == "localtransfer") {
      std::string s = config_next_arg(rest);
      bool use_local_transfer;
      if(strcasecmp("yes",s.c_str()) == 0) {
        use_local_transfer=true;
      }
      else if(strcasecmp("no",s.c_str()) == 0) {
        use_local_transfer=false;
      }
      else {
        logger.msg(Arc::ERROR,"wrong option in localtransfer"); goto exit;
      };
      JobsList::SetLocalTransfer(use_local_transfer);
    }
    else if(command == "mail") { /* internal address from which to send mail */ 
      support_mail_address(config_next_arg(rest));
      if(support_mail_address().empty()) {
        logger.msg(Arc::ERROR,"mail is empty"); goto exit;
      };
    }
    else if(command == "defaultttl") { /* time to keep job after finished */ 
      char *ep;
      std::string default_ttl_s = config_next_arg(rest);
      if(default_ttl_s.length() == 0) {
        logger.msg(Arc::ERROR,"defaultttl is empty"); goto exit;
      };
      default_ttl=strtoul(default_ttl_s.c_str(),&ep,10);
      if(*ep != 0) {
        logger.msg(Arc::ERROR,"wrong number in defaultttl command"); goto exit;
      };
      default_ttl_s = config_next_arg(rest);
      if(default_ttl_s.length() != 0) {
        if(rest.length() != 0) {
          logger.msg(Arc::ERROR,"junk in defaultttl command"); goto exit;
        };
        default_ttr=strtoul(default_ttl_s.c_str(),&ep,10);
        if(*ep != 0) {
          logger.msg(Arc::ERROR,"wrong number in defaultttl command"); goto exit;
        };
      } else {
        default_ttr=DEFAULT_KEEP_DELETED;
      };
    }
    else if(command == "maxrerun") { /* number of retries allowed */ 
      std::string default_reruns_s = config_next_arg(rest);
      if(default_reruns_s.length() == 0) {
        logger.msg(Arc::ERROR,"maxrerun is empty"); goto exit;
      };
      if(rest.length() != 0) {
        logger.msg(Arc::ERROR,"junk in maxrerun command"); goto exit;
      };
      char *ep;
      default_reruns=strtoul(default_reruns_s.c_str(),&ep,10);
      if(*ep != 0) {
        logger.msg(Arc::ERROR,"wrong number in maxrerun command"); goto exit;
      };      
    }
    else if(command == "diskspace") { /* maximal amount of disk space */ 
      std::string default_diskspace_s = config_next_arg(rest);
      if(default_diskspace_s.length() == 0) {
        logger.msg(Arc::ERROR,"diskspace is empty"); goto exit;
      };
      if(rest.length() != 0) {
        logger.msg(Arc::ERROR,"junk in diskspace command"); goto exit;
      };
      char *ep;
      default_diskspace=strtoull(default_diskspace_s.c_str(),&ep,10);
      if(*ep != 0) {
        logger.msg(Arc::ERROR,"wrong number in diskspace command"); goto exit;
      };      
    }
    else if(command == "lrms") {
      /* set default lrms type and queue 
         (optionally). Applied to all following
         'control' commands. MUST BE thing */
      default_lrms = config_next_arg(rest);
      default_queue = config_next_arg(rest);
      if(default_lrms.empty()) {
        logger.msg(Arc::ERROR,"defaultlrms is empty"); goto exit;
      };
      if(!rest.empty()) {
        logger.msg(Arc::ERROR,"junk in defaultlrms command"); goto exit;
      };
      check_lrms_backends(default_lrms);
    }
    else if(command == "authplugin") { /* set plugin to be called on 
                                          state changes */
      std::string state_name = config_next_arg(rest);
      if(state_name.length() == 0) {
        logger.msg(Arc::ERROR,"state name for plugin is missing"); goto exit;
      };
      std::string options_s = config_next_arg(rest);
      if(options_s.length() == 0) {
        logger.msg(Arc::ERROR,"Options for plugin are missing"); goto exit;
      };
      if(!plugins.add(state_name.c_str(),options_s.c_str(),rest.c_str())) {
        logger.msg(Arc::ERROR,"Failed to register plugin for state %s",state_name);      
        goto exit;
      };
    }
    else if(command == "localcred") {
      std::string timeout_s = config_next_arg(rest);
      if(timeout_s.length() == 0) {
        logger.msg(Arc::ERROR,"timeout for plugin is missing"); goto exit;
      };
      char *ep;
      int to = strtoul(timeout_s.c_str(),&ep,10);
      if((*ep != 0) || (to<0)) {
        logger.msg(Arc::ERROR,"wrong number for timeout in plugin command");
        goto exit;
      };
      cred_plugin = rest;
      cred_plugin.timeout(to);
    }
    else if(command == "sessiondir") {
      /* set session root directory - applied
         to all following 'control' commands */
      session_root = config_next_arg(rest);
      if(session_root.length() == 0) {
        logger.msg(Arc::ERROR,"session root dir is missing"); goto exit;
      };
      if(rest.length() != 0) {
        logger.msg(Arc::ERROR,"junk in session root command"); goto exit;
      };
      if(session_root == "*") session_root="";
    } else if(command == "controldir") {
      central_control_dir=rest;
    } else if(command == "control") {
      std::string control_dir = config_next_arg(rest);
      if(control_dir.length() == 0) {
        logger.msg(Arc::ERROR,"missing directory in control command"); goto exit;
      };
      if(control_dir == "*") control_dir="";
      if(command == "controldir") rest=".";
      for(;;) {
        std::string username = config_next_arg(rest);
        if(username.length() == 0) break;
        if(username == "*") {  /* add all gridmap users */
          if(!gridmap_user_list(rest)) {
            logger.msg(Arc::ERROR,"Can't read users in gridmap file %s",globus_gridmap()); goto exit;
          };
          continue;
        };
        if(username == ".") {  /* accept all users in this control directory */
           /* !!!!!!! substitutions involving user names won't work !!!!!!!  */
           if(superuser) { username=""; }
           else { username=my_username; };
        };
        /* add new user to list */
        if(superuser || (my_username == username)) {
          if(users.HasUser(username)) { /* first is best */
            continue;
          };
          JobUsers::iterator user=users.AddUser(username,&cred_plugin,
                                       control_dir,session_root);
          if(user == users.end()) { /* bad bad user */
            logger.msg(Arc::WARNING,"Warning: creation of user \"%s\" failed",username);
          }
          else {
            // get cache parameters for this user
            try {
              CacheConfig * cache_config = new CacheConfig(user->UnixName());
              std::list<std::string> cache_info = cache_config->getCacheDirs();
              for (std::list<std::string>::iterator i = cache_info.begin(); i != cache_info.end(); i++) {
                user->substitute(*i);
              }
              cache_config->setCacheDirs(cache_info);
              user->SetCacheParams(cache_config);
            }
            catch (CacheConfigException e) {
              logger.msg(Arc::ERROR, "Error with cache configuration: %s", e.what());
              logger.msg(Arc::ERROR, "Caching is disabled");
            }
            std::string control_dir_ = control_dir;
            std::string session_root_ = session_root;
            user->SetLRMS(default_lrms,default_queue);
            user->SetKeepFinished(default_ttl);
            user->SetKeepDeleted(default_ttr);
            user->SetReruns(default_reruns);
            user->SetDiskSpace(default_diskspace);
            user->substitute(control_dir_);
            user->substitute(session_root_);
            user->SetControlDir(control_dir_);
            user->SetSessionRoot(session_root_);
            user->SetStrictSession(strict_session);
            /* add helper to poll for finished jobs */
            std::string cmd_ = nordugrid_libexec_loc();
            make_escaped_string(control_dir_);
            cmd_+="/scan-"+default_lrms+"-job";
            make_escaped_string(cmd_);
            cmd_+=" --config ";
            cmd_+=nordugrid_config_loc();
            cmd_+=" ";
            cmd_+=control_dir_;
            user->add_helper(cmd_);
            /* creating empty list of jobs */
            JobsList *jobs = new JobsList(*user,plugins); 
            (*user)=jobs; /* back-associate jobs with user :) */
          };
        };
      };
      last_control_dir = control_dir;
    }
    else if(command == "helper") {
      std::string helper_user = config_next_arg(rest);
      if(helper_user.length() == 0) {
        logger.msg(Arc::ERROR,"user for helper program is missing"); goto exit;
      };
      if(rest.length() == 0) {
        logger.msg(Arc::ERROR,"helper program is missing"); goto exit;
      };
      if(helper_user == "*") {  /* go through all configured users */
        for(JobUsers::iterator user=users.begin();user!=users.end();++user) {
          if(!(user->has_helpers())) {
            std::string rest_=rest;
            user->substitute(rest_);
            user->add_helper(rest_);
          };    
        };
      }
      else if(helper_user == ".") { /* special helper */
        std::string session_root_ = session_root;
        std::string control_dir_ = last_control_dir;
        my_user.SetLRMS(default_lrms,default_queue);
        my_user.SetKeepFinished(default_ttl);
        my_user.SetKeepDeleted(default_ttr);
        my_user.substitute(session_root_);
        my_user.substitute(control_dir_);
        my_user.SetSessionRoot(session_root_);
        my_user.SetControlDir(control_dir_);
        std::string my_helper=rest;
        users.substitute(my_helper);
        my_user.substitute(my_helper);
        my_user.add_helper(my_helper);
      }
      else {
        /* look for that user */
        JobUsers::iterator user=users.find(helper_user);
        if(user == users.end()) {
          logger.msg(Arc::ERROR,"%s user for helper program is not configured",helper_user);
          goto exit;
        };
        user->substitute(rest);
        user->add_helper(rest);
      };
    };
  };
  delete cf;
  config_close(cfile);
  if(infosys_user.length()) {
    struct passwd pw_;
    struct passwd *pw;
    char buf[BUFSIZ];
    getpwnam_r(infosys_user.c_str(),&pw_,buf,BUFSIZ,&pw);
    if(pw != NULL) {
      if(pw->pw_uid != 0) {
        for(JobUsers::iterator user=users.begin();user!=users.end();++user) {
          if(pw->pw_uid != user->get_uid()) {
            if(pw->pw_gid != user->get_gid()) {
              user->SetShareLevel(JobUser::jobinfo_share_group);
            } else {
              user->SetShareLevel(JobUser::jobinfo_share_all);
            };
          };
        };
      };
    };
  };
  if(daemon) {
    if(jobreport_key.empty()) jobreport_key = daemon->keypath();
    if(jobreport_cert.empty()) jobreport_cert = daemon->certpath();
    if(jobreport_cadir.empty()) jobreport_cadir = daemon->cadirpath();
  }
  job_log.set_credentials(jobreport_key,jobreport_cert,jobreport_cadir);
  return true;
exit:
  delete cf;
  config_close(cfile);
  return false;
}

bool configure_serviced_users(Arc::XMLNode cfg,JobUsers &users,uid_t my_uid,const std::string &my_username,JobUser &my_user) {
  Arc::XMLNode tmp_node;
  bool superuser = (my_uid == 0);
  std::string default_lrms;
  std::string default_queue;
  std::string last_control_dir;
  std::string last_session_root;
  /*
   Currently we have everything running inside same arched.
   So we do not need any special treatment for infosys.
    std::string infosys_user("");
  */
  /*
  jobLogPath

  jobReport
    destination
    expiration
    type
    parameters
    keyPath
    certificatePath
    CACertificatesDir
  */
  tmp_node = cfg["jobLogPath"];
  if(tmp_node) {
    std::string fname = tmp_node;
    job_log.SetOutput(fname.c_str());
  };
  tmp_node = cfg["jobReport"];
  if(tmp_node) {
    std::string url = tmp_node["destination"];
    if(!url.empty()) {
      // destination is required
      job_log.SetReporter(url.c_str());
      unsigned int i;
      if(Arc::stringto(tmp_node["expiration"],i)) job_log.SetExpiration(i);
      std::string parameters = tmp_node["parameters"];
      if(!parameters.empty()) job_log.set_options(parameters);
      std::string jobreport_key = tmp_node["keyPath"];
      std::string jobreport_cert = tmp_node["certificatePath"];
      std::string jobreport_cadir = tmp_node["CACertificatesDir"];
      job_log.set_credentials(jobreport_key,jobreport_cert,jobreport_cadir);
    };
  };

  /*
  loadLimits
    maxJobsTracked
    maxJobsRun
    maxJobsTransfered
    maxJobsTransferedAdditional
    maxFilesTransfered
    wakeupPeriod
  */
  tmp_node = cfg["loadLimits"];
  if(tmp_node) {
    int max_jobs, max_jobs_running = -1;
    int max_jobs_processing, max_jobs_processing_emergency, max_downloads = -1;
    unsigned int wakeup_period;
    elementtoint(tmp_node,"maxJobsTracked",max_jobs,&logger);
    elementtoint(tmp_node,"maxJobsRun",max_jobs_running,&logger);
    JobsList::SetMaxJobs(max_jobs,max_jobs_running);
    elementtoint(tmp_node,"maxJobsTransfered",max_jobs_processing,&logger);
    elementtoint(tmp_node,"maxJobsTransferedAdditional",max_jobs_processing_emergency,&logger);
    elementtoint(tmp_node,"maxFilesTransfered",max_downloads,&logger);
    JobsList::SetMaxJobsLoad(max_jobs_processing,
                             max_jobs_processing_emergency,
                             max_downloads);
    if(elementtoint(tmp_node,"wakeupPeriod",wakeup_period,&logger)) {
      JobsList::SetWakeupPeriod(wakeup_period);
    };
  }

  /*
  dataTransfer
    secureTransfer
    passiveTransfer
    localTransfer
    timeouts
      minSpeed
      minSpeedTime
      minAverageSpeed
      maxInactivityTime
    maxRetries
    mapURL (link)
      from
      to
    Globus
      gridmapfile
      cadir
      certpath
      keypath
      TCPPortRange
      UDPPortRange
    httpProxy
  */
  tmp_node = cfg["dataTransfer"];
  if(tmp_node) {
    int min_speed=0;
    int min_speed_time=300;
    int min_average_speed=0;
    int max_inactivity_time=300;
    bool use_secure_transfer = false;
    bool use_passive_transfer = true;
    bool use_local_transfer = false;
    Arc::XMLNode to_node = tmp_node["timeouts"];
    if(to_node) {
      elementtoint(tmp_node,"minSpeed",min_speed,&logger);
      elementtoint(tmp_node,"minSpeedTime",min_speed_time,&logger);
      elementtoint(tmp_node,"minAverageSpeed",min_average_speed,&logger);
      elementtoint(tmp_node,"maxInactivityTime",max_inactivity_time,&logger);
      JobsList::SetSpeedControl(min_speed,min_speed_time,
                                min_average_speed,max_inactivity_time);
    };
    elementtobool(tmp_node,"passiveTransfer",use_passive_transfer,&logger);
    JobsList::SetPassiveTransfer(use_passive_transfer);
    elementtobool(tmp_node,"secureTransfer",use_secure_transfer,&logger);
    JobsList::SetSecureTransfer(use_secure_transfer);
    elementtobool(tmp_node,"localTransfer",use_local_transfer,&logger);
    JobsList::SetLocalTransfer(use_local_transfer);


  };
  /*
  serviceMail
  */
  tmp_node = cfg["serviceMail"];
  if(tmp_node) {
    support_mail_address((std::string)tmp_node);
    if(support_mail_address().empty()) {
      logger.msg(Arc::ERROR,"serviceMail is empty");
      return false;
    };
  }

  /*
  LRMS
    type
    defaultShare
  */
  tmp_node = cfg["LRMS"];
  if(tmp_node) {
    default_lrms = (std::string)(tmp_node["type"]);
    if(default_lrms.empty()) {
      logger.msg(Arc::ERROR,"type in LRMS is missing"); return false;
    };
    default_queue = (std::string)(tmp_node["defaultShare"]);
    check_lrms_backends(default_lrms);
    runtime_config_dir((std::string)(tmp_node["runtimeDir"]));
  } else {
    logger.msg(Arc::ERROR,"LRMS is missing"); return false;
  }

  /*
  authPlugin (timeout,onSuccess=PASS,FAIL,LOG,onFailure=FAIL,PASS,LOG,onTimeout=FAIL,PASS,LOG)
    state
    command
  */
  tmp_node = cfg["authPlugin"];
  for(;tmp_node;++tmp_node) {
    std::string state_name = tmp_node["state"];
    if(state_name.empty()) {
      logger.msg(Arc::ERROR,"state name for authPlugin is missing");
      return false;
    };
    std::string command = tmp_node["command"];
    if(state_name.empty()) {
      logger.msg(Arc::ERROR,"command for authPlugin is missing");
      return false;
    };
    std::string options;
    Arc::XMLNode onode;
    onode = tmp_node.Attribute("timeout");
    if(onode) options+="timeout="+(std::string)onode;
    onode = tmp_node.Attribute("onSuccess");
    if(onode) options+="onsuccess="+Arc::lower((std::string)onode);
    onode = tmp_node.Attribute("onFailure");
    if(onode) options+="onfailure="+Arc::lower((std::string)onode);
    onode = tmp_node.Attribute("onTimeout");
    if(onode) options+="ontimeout="+Arc::lower((std::string)onode);
    if(!plugins.add(state_name.c_str(),options.c_str(),command.c_str())) {
      logger.msg(Arc::ERROR,"Failed to register plugin for state %s",state_name);
      return false;
    };
  };

  /*
  localCred (timeout)
    command
  */
  tmp_node = cfg["localCred"];
  if(tmp_node) {
    std::string command = tmp_node["command"];
    if(command.empty()) {
      logger.msg(Arc::ERROR,"command for localCred is missing");
      return false;
    };
    std::string options;
    Arc::XMLNode onode;
    onode = tmp_node.Attribute("timeout");
    if(!onode) {
      logger.msg(Arc::ERROR,"timeout for localCred is missing");
      return false;
    };
    int to;
    if(!elementtoint(onode,NULL,to,&logger)) {
      logger.msg(Arc::ERROR,"timeout for localCred is incorrect number");
      return false;
    };
    cred_plugin = command;
    cred_plugin.timeout(to);
  }

  /*
  control
    username
    controlDir
    sessionRootDir
    cache
      location
        path
        link
      highWatermark
      lowWatermark
    defaultTTL
    defaultTTR
    maxReruns
    noRootPower
    diskSpace
  */

  tmp_node = cfg["control"];
  if(!tmp_node) {
    logger.msg(Arc::ERROR,"At least one control element must be present");
    return false;
  };
  for(;tmp_node;++tmp_node) {
    std::string control_dir = tmp_node["controlDir"];
    if(control_dir.empty()) {
      logger.msg(Arc::ERROR,"controlDir is missing"); return false;
    };
    if(control_dir == "*") control_dir="";
    std::string session_root = tmp_node["sessionRootDir"];
    if(session_root.empty()) {
      logger.msg(Arc::ERROR,"sessionRootDir is missing"); return false;
    };
    last_control_dir = control_dir;
    last_session_root = session_root;
    bool strict_session = false;
    if(!elementtobool(tmp_node,"noRootPower",strict_session,&logger)) return false;
    unsigned int default_reruns = DEFAULT_JOB_RERUNS;
    unsigned int default_ttl = DEFAULT_KEEP_FINISHED;
    unsigned int default_ttr = DEFAULT_KEEP_DELETED;
    int default_diskspace = DEFAULT_DISKSPACE;
    if(!elementtoint(tmp_node,"maxReruns",default_reruns,&logger)) return false;
    if(!elementtoint(tmp_node,"defaultTTL",default_ttl,&logger)) return false;
    if(!elementtoint(tmp_node,"defaultTTR",default_ttr,&logger)) return false;
    if(!elementtoint(tmp_node,"defaultDiskSpace",default_diskspace,&logger)) return false;
    Arc::XMLNode unode = tmp_node["username"];
    std::list<std::string> userlist;
    for(;unode;++unode) {
      std::string username = unode;
      if(username.empty()) {
        logger.msg(Arc::ERROR,"username in control is empty"); return false;
      };
      if(username == "*") {  /* add all gridmap users */
        if(!gridmap_user_list(userlist)) {
          logger.msg(Arc::ERROR,"Can't read users in gridmap file %s",globus_gridmap());
          return false;
        };
        continue;
      };
      if(username == ".") {  /* accept all users in this control directory */
         /* !!!!!!! substitutions involving user names won't work !!!!!!!  */
         if(superuser) { username=""; }
         else { username=my_username; };
      };
      userlist.push_back(username);
    };
    if(userlist.size() == 0) {
      logger.msg(Arc::ERROR,"No username entries in control"); return false;
    };
    for(std::list<std::string>::iterator username = userlist.begin();
                   username != userlist.end();++username) {
      /* add new user to list */
      if(superuser || (my_username == *username)) {
        if(users.HasUser(*username)) { /* first is best */
          continue;
        };
        JobUsers::iterator user=users.AddUser(*username,&cred_plugin,
                                     control_dir,session_root);
        if(user == users.end()) { /* bad bad user */
          logger.msg(Arc::WARNING,"Warning: creation of user \"%s\" failed",*username);
        }
        else {
          // get cache parameters for this user
          try {
            CacheConfig * cache_config = new CacheConfig(tmp_node);
            std::list<std::string> cache_info = cache_config->getCacheDirs();
            for (std::list<std::string>::iterator i = cache_info.begin();
                 i != cache_info.end(); i++) {
              user->substitute(*i);
            }
            cache_config->setCacheDirs(cache_info);
            user->SetCacheParams(cache_config);
          }
          catch (CacheConfigException e) {
            logger.msg(Arc::ERROR, "Error with cache configuration: %s", e.what());
            logger.msg(Arc::ERROR, "Caching is disabled");
          }
          std::string control_dir_ = control_dir;
          std::string session_root_ = session_root;
          user->SetLRMS(default_lrms,default_queue);
          user->SetKeepFinished(default_ttl);
          user->SetKeepDeleted(default_ttr);
          user->SetReruns(default_reruns);
          user->SetDiskSpace(default_diskspace);
          user->substitute(control_dir_);
          user->substitute(session_root_);
          user->SetControlDir(control_dir_);
          user->SetSessionRoot(session_root_);
          user->SetStrictSession(strict_session);
          /* add helper to poll for finished jobs */
          std::string cmd_ = nordugrid_libexec_loc();
          make_escaped_string(control_dir_);
          cmd_+="/scan-"+default_lrms+"-job";
          make_escaped_string(cmd_);
          cmd_+=" ";
          cmd_+=control_dir_;
          user->add_helper(cmd_);
          /* creating empty list of jobs */
          JobsList *jobs = new JobsList(*user,plugins);
          (*user)=jobs; /* back-associate jobs with user :) */
        };
      };
    };
  };
  /*
  helperUtility
    username
    command
  */
  tmp_node = cfg["helperUtility"];
  for(;tmp_node;++tmp_node) {
    std::string command = tmp_node["command"];
    if(command.empty()) {
      logger.msg(Arc::ERROR,"command in helperUtility is missing");
      return false;
    };
    Arc::XMLNode unode = tmp_node["username"];
    for(;unode;++unode) {
      std::string username = unode;
      if(username.empty()) {
        logger.msg(Arc::ERROR,"username in helperUtility is empty");
        return false;
      };
      if(username == "*") {  /* go through all configured users */
        for(JobUsers::iterator user=users.begin();user!=users.end();++user) {
          if(!(user->has_helpers())) {
            std::string command_=command;
            user->substitute(command_);
            user->add_helper(command_);
          };
        };
      }
      else if(username == ".") { /* special helper */
        // Take parameters of last control
        std::string session_root_ = last_session_root;
        std::string control_dir_ = last_control_dir;
        my_user.SetLRMS(default_lrms,default_queue);
        my_user.substitute(session_root_);
        my_user.substitute(control_dir_);
        my_user.SetSessionRoot(session_root_);
        my_user.SetControlDir(control_dir_);
        std::string command_=command;
        users.substitute(command_);
        my_user.substitute(command_);
        my_user.add_helper(command_);
      }
      else {
        /* look for that user */
        JobUsers::iterator user=users.find(username);
        if(user == users.end()) {
          logger.msg(Arc::ERROR,"User %s for helperUtility is not configured",
                     username);
          return false;
        };
        std::string command_=command;
        user->substitute(command_);
        user->add_helper(command_);
      };
    };
  };
  return true;
}

bool print_serviced_users(const JobUsers &users) {
  for(JobUsers::const_iterator user = users.begin();user!=users.end();++user) {
    logger.msg(Arc::INFO,"Added user : %s",user->UnixName());
    logger.msg(Arc::INFO,"\tSession root dir : %s",user->SessionRoot());
    logger.msg(Arc::INFO,"\tControl dir      : %s",user->ControlDir());
    logger.msg(Arc::INFO,"\tdefault LRMS     : %s",user->DefaultLRMS());
    logger.msg(Arc::INFO,"\tdefault queue    : %s",user->DefaultQueue());
    logger.msg(Arc::INFO,"\tdefault ttl      : %u",user->KeepFinished());

    CacheConfig * cache_config = user->CacheParams();

    if(!cache_config) {
      logger.msg(Arc::INFO,"No cache directory found in configuration, caching is disabled");
      continue;
    }

    std::list<std::string> conf_caches = cache_config->getCacheDirs();

    if(conf_caches.empty()) {
      logger.msg(Arc::INFO,"No cache directory found in configuration, caching is disabled");
      continue;
    }
    // list each cache
    for (std::list<std::string>::iterator i = conf_caches.begin(); i != conf_caches.end(); i++) {
      logger.msg(Arc::INFO, "\tCache            : %s", (*i).substr(0, (*i).find(" ")));
      if ((*i).find(" ") != std::string::npos) {
        logger.msg(Arc::INFO, "\tCache link dir   : %s", (*i).substr((*i).find_last_of(" ")+1, (*i).length()-(*i).find_last_of(" ")+1));
      }
    }
    if (cache_config->cleanCache()) logger.msg(Arc::INFO, "\tCache cleaning enabled");
    else logger.msg(Arc::INFO, "\tCache cleaning disabled");
  };
  return true;
}


// -*- indent-tabs-mode: nil -*-

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <iostream>
#include <list>
#include <string>

#include <arc/ArcConfig.h>
#include <arc/ArcLocation.h>
#include <arc/IString.h>
#include <arc/Logger.h>
#include <arc/XMLNode.h>
#include <arc/OptionParser.h>
#include <arc/StringConv.h>
#include <arc/URL.h>
#include <arc/client/JobController.h>
#include <arc/client/JobSupervisor.h>
#include <arc/client/TargetGenerator.h>
#include <arc/UserConfig.h>
#include <arc/client/Job.h>
#include <arc/DateTime.h>
#include <arc/FileLock.h>
#include <arc/Utils.h>
#include <arc/client/Submitter.h>
#include <arc/client/Broker.h>
#include <arc/credential/Credential.h>

#ifdef TEST
#define RUNMIGRATE(X) test_arcmigrate_##X
#else
#define RUNMIGRATE(X) X
#endif
int RUNMIGRATE(main)(int argc, char **argv) {

  setlocale(LC_ALL, "");

  Arc::Logger logger(Arc::Logger::getRootLogger(), "arcmigrate");
  Arc::LogStream logcerr(std::cerr);
  logcerr.setFormat(Arc::ShortFormat);
  Arc::Logger::getRootLogger().addDestination(logcerr);
  Arc::Logger::getRootLogger().setThreshold(Arc::WARNING);

  Arc::ArcLocation::Init(argv[0]);

  Arc::OptionParser options(istring("[job ...]"),
                            istring("The arcmigrate command is used for "
                                    "migrating queued jobs to another resource.\n"
                                    "Note that migration is only supported "
                                    "between A-REX powered resources."));

  bool all = false;
  options.AddOption('a', "all",
                    istring("all jobs"),
                    all);

  bool forcemigration = false;
  options.AddOption('f', "force",
                    istring("force migration, ignore kill failure"),
                    forcemigration);

  std::string joblist;
  options.AddOption('j', "joblist",
                    istring("the file storing information about active jobs (default ~/.arc/jobs.xml)"),
                    istring("filename"),
                    joblist);
                  
  std::string jobidfileout;
  options.AddOption('o', "jobids-to-file",
                    istring("the IDs of the submitted jobs will be appended to this file"),
                    istring("filename"),
                    jobidfileout);
                                    
  std::list<std::string> jobidfiles;
  options.AddOption('i', "jobids-from-file",
                    istring("a file containing a list of jobIDs"),
                    istring("filename"),
                    jobidfiles);                  

  std::list<std::string> clusters;
  options.AddOption('c', "cluster",
                    istring("explicitly select or reject a resource holding queued jobs"),
                    istring("[-]name"),
                    clusters);

  std::list<std::string> qlusters;
  options.AddOption('q', "qluster",
                    istring("explicitly select or reject a resource to migrate to"),
                    istring("[-]name"),
                    qlusters);

  std::list<std::string> indexurls;
  options.AddOption('g', "index",
                    istring("explicitly select or reject an index server"),
                    istring("[-]name"),
                    indexurls);

  int timeout = -1;
  options.AddOption('t', "timeout", istring("timeout in seconds (default 20)"),
                    istring("seconds"), timeout);

  std::string conffile;
  options.AddOption('z', "conffile",
                    istring("configuration file (default ~/.arc/client.conf)"),
                    istring("filename"), conffile);

  std::string debug;
  options.AddOption('d', "debug",
                    istring("FATAL, ERROR, WARNING, INFO, VERBOSE or DEBUG"),
                    istring("debuglevel"), debug);

  bool version = false;
  options.AddOption('v', "version", istring("print version information"),
                    version);

  std::string broker;
  options.AddOption('b', "broker",
                    istring("select broker method (Random (default), FastestQueue, or custom)"),
                    istring("broker"), broker);

  std::list<std::string> jobs = options.Parse(argc, argv);
  
  for (std::list<std::string>::const_iterator it = jobidfiles.begin(); it != jobidfiles.end(); it++) {
    if (!Arc::Job::ReadJobIDsFromFile(*it, jobs)) {
      logger.msg(Arc::WARNING, "Cannot read specified jobid file: %s", *it);
    }
  }

  if (version) {
    std::cout << Arc::IString("%s version %s", "arcmigrate", VERSION)
              << std::endl;
    return 0;
  }

  // If debug is specified as argument, it should be set before loading the configuration.
  if (!debug.empty())
    Arc::Logger::getRootLogger().setThreshold(Arc::string_to_level(debug));

  Arc::UserConfig usercfg(conffile, joblist);
  if (!usercfg) {
    logger.msg(Arc::ERROR, "Failed configuration initialization");
    return 1;
  }

  if (!usercfg.ProxyPath().empty() ) {
    if (!usercfg.CACertificatesDirectory().empty() ){
      Arc::Credential holder(usercfg.ProxyPath(), "", usercfg.CACertificatesDirectory(), "");
      if(holder.IsValid() ){
        if (holder.GetEndTime() < Arc::Time()){
          std::cout << Arc::IString("Proxy expired. Job submission aborted. Please run 'arcproxy'!") << std::endl;
          return 1;
        }
        else if (holder.GetVerification()) {
          logger.msg(Arc::INFO, "Proxy successfully verified.");
        }
      }
      else {
          std::cout << Arc::IString("Proxy not valid. Job submission aborted. Please run 'arcproxy'!") << std::endl;
          return 1;
        }
      }
      else {
      std::cout << Arc::IString("Cannot find CA certificates directory. Please specify the location to the directory in the client configuration file.") << std::endl;
      return 1;
    }
  }
  else {
    std::cout << Arc::IString("Cannot find any proxy. arcmigrate currently cannot run without a proxy.\n"
                              "  If you have the proxy file in a non-default location,\n"
                              "  please make sure the path is specified in the client configuration file.\n"
                              "  If you don't have a proxy yet, please run 'arcproxy'!") << std::endl;
    return 1;
  }

  if (debug.empty() && !usercfg.Verbosity().empty())
    Arc::Logger::getRootLogger().setThreshold(Arc::string_to_level(usercfg.Verbosity()));

  if (timeout > 0)
    usercfg.Timeout(timeout);

  if (!broker.empty())
    usercfg.Broker(broker);

  if (!joblist.empty() && jobs.empty() && clusters.empty())
    all = true;

  if (jobs.empty() && clusters.empty() && !all) {
    logger.msg(Arc::ERROR, "No jobs given");
    return 1;
  }

  // Different selected services are needed in two different context,
  // so the two copies of UserConfig objects will contain different
  // selected services.
  Arc::UserConfig usercfg2 = usercfg;

  if (!jobs.empty() || all)
    usercfg.ClearSelectedServices();

  if (!clusters.empty()) {
    usercfg.ClearSelectedServices();
    usercfg.AddServices(clusters, Arc::COMPUTING);
  }

  if (!qlusters.empty() || !indexurls.empty())
    usercfg2.ClearSelectedServices();

  if (!qlusters.empty())
    usercfg2.AddServices(qlusters, Arc::COMPUTING);

  if (!indexurls.empty())
    usercfg2.AddServices(indexurls, Arc::INDEX);

  // If the user specified a joblist on the command line joblist equals
  // usercfg.JobListFile(). If not use the default, ie. usercfg.JobListFile().
  Arc::JobSupervisor jobmaster(usercfg, jobs);
  if (!jobmaster.JobsFound()) {
    std::cout << "No jobs" << std::endl;
    return 0;
  }
  std::list<Arc::JobController*> jobcont = jobmaster.GetJobControllers();

  if (jobcont.empty()) {
    logger.msg(Arc::ERROR, "No job controller plugins loaded");
    return 1;
  }

  Arc::TargetGenerator targetGen(usercfg2);
  targetGen.RetrieveExecutionTargets();

  if (targetGen.GetExecutionTargets().empty()) {
    std::cout << Arc::IString("Job migration aborted because no resource returned any information") << std::endl;
    return 1;
  }

  Arc::BrokerLoader loader;
  Arc::Broker *chosenBroker = loader.load(usercfg.Broker().first, usercfg);
  if (!chosenBroker) {
    logger.msg(Arc::ERROR, "Unable to load broker %s", usercfg.Broker().first);
    return 1;
  }
  logger.msg(Arc::INFO, "Broker %s loaded", usercfg.Broker().first);

  std::list<Arc::URL> migratedJobIDs;

  int retval = 0;
  // Loop over job controllers
  for (std::list<Arc::JobController*>::iterator itJobCont = jobcont.begin(); itJobCont != jobcont.end(); itJobCont++) {
    if (!(*itJobCont)->Migrate(targetGen, chosenBroker, usercfg, forcemigration, migratedJobIDs)) {
      retval = 1;
    }
    for (std::list<Arc::URL>::iterator it = migratedJobIDs.begin();
         it != migratedJobIDs.end(); it++) {
      std::string jobid = it->str();
      if (!jobidfileout.empty())
        if (!Arc::Job::WriteJobIDToFile(jobid, jobidfileout))
          logger.msg(Arc::WARNING, "Cannot write jobid (%s) to file (%s)", jobid, jobidfileout);
      std::cout << Arc::IString("Job migrated with jobid: %s", jobid) << std::endl;
    }
  } // Loop over job controllers

  return retval;
}

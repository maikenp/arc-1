// -*- indent-tabs-mode: nil -*-

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <arc/FileUtils.h>
#include <arc/Logger.h>
#include <arc/StringConv.h>
#include <arc/Utils.h>

#include "JobInformationStorageSQLite.h"

namespace Arc {

  Logger JobInformationStorageSQLite::logger(Logger::getRootLogger(), "JobInformationStorageSQLite");

  static const std::string sql_special_chars("'#\r\n\b\0",6);
  static const char sql_escape_char('%');
  static const Arc::escape_type sql_escape_type(Arc::escape_hex);

  inline static std::string sql_escape(const std::string& str) {
    return Arc::escape_chars(str, sql_special_chars, sql_escape_char, false, sql_escape_type);
  }

  inline static std::string sql_unescape(const std::string& str) {
    return Arc::unescape_chars(str, sql_escape_char,sql_escape_type);
  }

  int sqlite3_exec_nobusy(sqlite3* db, const char *sql, int (*callback)(void*,int,char**,char**), void *arg, char **errmsg) {
    int err;
    while((err = sqlite3_exec(db, sql, callback, arg, errmsg)) == SQLITE_BUSY) {
      // Access to database is designed in such way that it should not block for long time.
      // So it should be safe to simply wait for lock to be released without any timeout.
      struct timespec delay = { 0, 10000000 }; // 0.01s - should be enough for most cases
      (void)::nanosleep(&delay, NULL);
    };
    return err;
  }

  JobInformationStorageSQLite::JobDB::JobDB(const std::string& name, bool create): jobDB(NULL)
  {
    int err;
    int flags = SQLITE_OPEN_READWRITE; // it will open read-only if access is protected
    if(create) flags |= SQLITE_OPEN_CREATE;

    while((err = sqlite3_open_v2(name.c_str(), &jobDB, flags, NULL)) == SQLITE_BUSY) {
      // In case something prevents database from open right now - retry
      if(jobDB) (void)sqlite3_close(jobDB);
      jobDB = NULL;
      struct timespec delay = { 0, 10000000 }; // 0.01s - should be enough for most cases
      (void)::nanosleep(&delay, NULL);
    }
    if(err != SQLITE_OK) {
      handleError(NULL, err);
      tearDown();
      throw SQLiteException(IString("Unable to create data base (%s)", name).str(), err);
    }

    if(create) {
      err = sqlite3_exec_nobusy(jobDB,
          "CREATE TABLE IF NOT EXISTS jobs("
            "id, idfromendpoint, name, statusinterface, statusurl, "
            "managementinterfacename, managementurl, "
            "serviceinformationinterfacename, serviceinformationurl, serviceinformationhost, "
            "sessiondir, stageindir, stageoutdir, "
            "descriptiondocument, localsubmissiontime, delegationid, UNIQUE(id))",
           NULL, NULL, NULL);   
      // TODO: index for serviceinformationhost
      if(err != SQLITE_OK) {
        handleError(NULL, err);
        tearDown();
        throw SQLiteException(IString("Unable to create jobs table in data base (%s)", name).str(), err);
      }
    } else {
      // SQLite opens database in lazy way. But we still want to know if it is good database.
      err = sqlite3_exec_nobusy(jobDB, "PRAGMA schema_version;", NULL, NULL, NULL);
      if(err != SQLITE_OK) {
        handleError(NULL, err);
        tearDown();
        throw SQLiteException(IString("Failed checking database (%s)", name).str(), err);
      }
    }

    JobInformationStorageSQLite::logger.msg(DEBUG, "Job database created successfully (%s)", name);
  }

  void JobInformationStorageSQLite::JobDB::tearDown() {
    if (jobDB) {
      (void)sqlite3_close(jobDB);
      jobDB = NULL;
    }
  }

  JobInformationStorageSQLite::JobDB::~JobDB() {
    tearDown();
  }

  void JobInformationStorageSQLite::JobDB::handleError(const char* errpfx, int err) {
#ifdef HAVE_SQLITE3_ERRSTR
    std::string msg = sqlite3_errstr(err);
#else
    std::string msg = "error code "+Arc::tostring(err);
#endif
    if (errpfx) {
      JobInformationStorageSQLite::logger.msg(DEBUG, "Error from SQLite: %s: %s", errpfx, msg);
    }
    else {
      JobInformationStorageSQLite::logger.msg(DEBUG, "Error from SQLite: %s", msg);
    }
  }

  JobInformationStorageSQLite::SQLiteException::SQLiteException(const std::string& msg, int ret, bool writeLogMessage) throw() : message(msg), returnvalue(ret) {
    if (writeLogMessage) {
      JobInformationStorageSQLite::logger.msg(VERBOSE, msg);
      JobInformationStorageSQLite::logErrorMessage(ret);
    }
  }


  JobInformationStorageSQLite::JobInformationStorageSQLite(const std::string& name, unsigned nTries, unsigned tryInterval)
    : JobInformationStorage(name, nTries, tryInterval) {
    isValid = false;
    isStorageExisting = false;

    if (!Glib::file_test(name, Glib::FILE_TEST_EXISTS)) {
      const std::string joblistdir = Glib::path_get_dirname(name);
      // Check if the parent directory exist.
      if (!Glib::file_test(joblistdir, Glib::FILE_TEST_EXISTS)) {
        logger.msg(ERROR, "Job list file cannot be created: The parent directory (%s) doesn't exist.", joblistdir);
        return;
      }
      else if (!Glib::file_test(joblistdir, Glib::FILE_TEST_IS_DIR)) {
        logger.msg(ERROR, "Job list file cannot be created: %s is not a directory", joblistdir);
        return;
      }
      isValid = true;
      return;
    }
    else if (!Glib::file_test(name, Glib::FILE_TEST_IS_REGULAR)) {
      logger.msg(ERROR, "Job list file (%s) is not a regular file", name);
      return;
    }

    try {
      JobDB db(name);
    } catch (const SQLiteException& e) {
      isValid = false;
      return;
    }
    isStorageExisting = isValid = true;
  }

  bool JobInformationStorageSQLite::Write(const std::list<Job>& jobs, const std::set<std::string>& prunedServices, std::list<const Job*>& newJobs) {
    std::string empty_string;
    if (!isValid) {
      return false;
    }
    if (jobs.empty()) return true;
    
    try {
      JobDB db(name, true);
      for (std::set<std::string>::const_iterator itPruned = prunedServices.begin();
           itPruned != prunedServices.end(); ++itPruned) {
        std::string sqlcmd = "DELETE FROM jobs WHERE (serviceinformationhost = '" + *itPruned + "')";
        (void)sqlite3_exec_nobusy(db.handle(), sqlcmd.c_str(), NULL, NULL, NULL);
      }
      for (std::list<Job>::const_iterator it = jobs.begin();
           it != jobs.end(); ++it) {
        std::string sqlcmd = "INSERT OR REPLACE INTO jobs("
          "id, idfromendpoint, name, statusinterface, statusurl, "
          "managementinterfacename, managementurl, "
          "serviceinformationinterfacename, serviceinformationurl, serviceinformationhost, "
          "sessiondir, stageindir, stageoutdir, "
          "descriptiondocument, localsubmissiontime, delegationid "
          ") VALUES ('"+
             sql_escape(it->JobID)+"', '"+
             sql_escape(it->IDFromEndpoint)+"', '"+
             sql_escape(it->Name)+"', '"+
             sql_escape(it->JobStatusInterfaceName)+"', '"+
             sql_escape(it->JobStatusURL.fullstr())+"', '"+
             sql_escape(it->JobManagementInterfaceName)+"', '"+
             sql_escape(it->JobManagementURL.fullstr())+"', '"+
             sql_escape(it->ServiceInformationInterfaceName)+"', '"+
             sql_escape(it->ServiceInformationURL.fullstr())+"', '"+
             sql_escape(it->ServiceInformationURL.Host())+"', '"+
             sql_escape(it->SessionDir.fullstr())+"', '"+
             sql_escape(it->StageInDir.fullstr())+"', '"+
             sql_escape(it->StageOutDir.fullstr())+"', '"+
             sql_escape(it->JobDescriptionDocument)+"', '"+
             sql_escape(tostring(it->LocalSubmissionTime.GetTime()))+"', '"+
             sql_escape(it->DelegationID.size()>0?*(it->DelegationID.begin()):empty_string)+"')";
        int err = sqlite3_exec_nobusy(db.handle(), sqlcmd.c_str(), NULL, NULL, NULL);
        if(err != SQLITE_OK) {
          logger.msg(VERBOSE, "Unable to write records into job database (%s): Id \"%s\"", name, it->JobID);
          logErrorMessage(err);
          return false;
        }
        if(sqlite3_changes(db.handle()) != 1) {
          logger.msg(VERBOSE, "Unable to write records into job database (%s): Id \"%s\"", name, it->JobID);
          logErrorMessage(err);
          return false;
        }
        // TODO: newJobs
      }
    } catch (const SQLiteException& e) {
      return false;
    }

    return true;
  }

  struct ReadJobsCallbackArg {
    std::list<Job>& jobs;
    std::list<std::string>* jobIdentifiers;
    const std::list<std::string>* endpoints;
    const std::list<std::string>* rejectEndpoints;
    ReadJobsCallbackArg(std::list<Job>& jobs, 
                        std::list<std::string>* jobIdentifiers,
                        const std::list<std::string>* endpoints,
                        const std::list<std::string>* rejectEndpoints):
       jobs(jobs), jobIdentifiers(jobIdentifiers), endpoints(endpoints), rejectEndpoints(rejectEndpoints) {};
  };

  static int ReadJobsCallback(void* arg, int colnum, char** texts, char** names) {
    ReadJobsCallbackArg& carg = *reinterpret_cast<ReadJobsCallbackArg*>(arg);
    carg.jobs.push_back(Job());
    bool drop = false;
    for(int n = 0; n < colnum; ++n) {
      if(drop) break;
      if(names[n] && texts[n]) {
        if(strcmp(names[n], "id") == 0) {
          carg.jobs.back().JobID = sql_unescape(texts[n]);
          if(carg.jobIdentifiers) {
            bool found = false;
            for(std::list<std::string>::iterator it = carg.jobIdentifiers->begin();
                           it != carg.jobIdentifiers->end(); ++it) {
              if(*it == carg.jobs.back().JobID) {
                found = true;
                carg.jobIdentifiers->erase(it);
                break;
              }
            }
            if(!found) drop = true;
            // TODO: other id options
          }
        } else if(strcmp(names[n], "idfromendpoint") == 0) {
          carg.jobs.back().IDFromEndpoint = sql_unescape(texts[n]);
        } else if(strcmp(names[n], "name") == 0) {
          carg.jobs.back().Name = sql_unescape(texts[n]);
        } else if(strcmp(names[n], "statusinterface") == 0) {
          carg.jobs.back().JobStatusInterfaceName = sql_unescape(texts[n]);
        } else if(strcmp(names[n], "statusurl") == 0) {
          carg.jobs.back().JobStatusURL = sql_unescape(texts[n]);
        } else if(strcmp(names[n], "managementinterfacename") == 0) {
          carg.jobs.back().JobManagementInterfaceName = sql_unescape(texts[n]);
        } else if(strcmp(names[n], "managementurl") == 0) {
          carg.jobs.back().JobManagementURL = sql_unescape(texts[n]);
          if(carg.rejectEndpoints) {
            for (std::list<std::string>::const_iterator it = carg.rejectEndpoints->begin();
                     it != carg.rejectEndpoints->end(); ++it) {
              if (carg.jobs.back().JobManagementURL.StringMatches(*it)) {
                drop = true;
                break;
              }
            }
          }
        } else if(strcmp(names[n], "serviceinformationinterfacename") == 0) {
          carg.jobs.back().ServiceInformationInterfaceName = sql_unescape(texts[n]);
        } else if(strcmp(names[n], "serviceinformationurl") == 0) {
          carg.jobs.back().ServiceInformationURL = sql_unescape(texts[n]);
        } else if(strcmp(names[n], "sessiondir") == 0) {
          carg.jobs.back().SessionDir = sql_unescape(texts[n]);
        } else if(strcmp(names[n], "stageindir") == 0) {
          carg.jobs.back().StageInDir = sql_unescape(texts[n]);
        } else if(strcmp(names[n], "stageoutdir") == 0) {
          carg.jobs.back().StageOutDir = sql_unescape(texts[n]);
        } else if(strcmp(names[n], "descriptiondocument") == 0) {
          carg.jobs.back().JobDescriptionDocument = sql_unescape(texts[n]);
        } else if(strcmp(names[n], "localsubmissiontime") == 0) {
          carg.jobs.back().LocalSubmissionTime.SetTime(stringtoi(sql_unescape(texts[n])));
        } else if(strcmp(names[n], "delegationid") == 0) {
          carg.jobs.back().DelegationID.push_back(sql_unescape(texts[n]));
        }
      }
    }
    if(drop) {
      carg.jobs.pop_back();
    }
    // TODO: endpoints
/*
      for (std::list<std::string>::const_iterator it = endpoints.begin();
           it != endpoints.end(); ++it) {
        // Extract hostname from iterator.
        URL u(*it);
        if (u.Protocol() == "file") {
          u = URL("http://" + *it); // Only need to extract hostname. Prefix with "http://".
        }
        if (u.Host().empty()) continue;

        Dbt key((char *)u.Host().c_str(), u.Host().size()), pkey, data;
        ret = cursor->pget(&key, &pkey, &data, DB_SET);
        if (ret != 0) {
          continue;
        }
        std::string tmpEndpoint;
        deserialiseNthJobAttribute(tmpEndpoint, data, 7);
        URL jobManagementURL(tmpEndpoint);
        if (jobManagementURL.StringMatches(*it)) {
          addJobFromDB(pkey, data, jobs, idsOfAddedJobs, rejectEndpoints);
        }
        while ((ret = cursor->pget(&key, &pkey, &data, DB_NEXT_DUP)) == 0) {
          deserialiseNthJobAttribute(tmpEndpoint, data, 7);
          URL jobManagementURL(tmpEndpoint);
          if (jobManagementURL.StringMatches(*it)) {
           addJobFromDB(pkey, data, jobs, idsOfAddedJobs, rejectEndpoints);
         }
        }
      }
*/




    return 0;
  }

  bool JobInformationStorageSQLite::ReadAll(std::list<Job>& jobs, const std::list<std::string>& rejectEndpoints) {
    if (!isValid) {
      return false;
    }
    jobs.clear();

    try {
      int ret;
      JobDB db(name);
      std::string sqlcmd = "SELECT * FROM jobs";
      ReadJobsCallbackArg carg(jobs, NULL, NULL, &rejectEndpoints);
      int err = sqlite3_exec_nobusy(db.handle(), sqlcmd.c_str(), &ReadJobsCallback, &carg, NULL);
      if(err != SQLITE_OK) {
        // handle error ??
        return false;
      }
    } catch (const SQLiteException& e) {
      return false;
    }
    
    return true;
  }
  
  bool JobInformationStorageSQLite::Read(std::list<Job>& jobs, std::list<std::string>& jobIdentifiers,
                                      const std::list<std::string>& endpoints,
                                      const std::list<std::string>& rejectEndpoints) {
    if (!isValid) {
      return false;
    }
    jobs.clear();
    
    try {
      int ret;
      JobDB db(name);
      std::string sqlcmd = "SELECT * FROM jobs";
      ReadJobsCallbackArg carg(jobs, &jobIdentifiers, &endpoints, &rejectEndpoints);
      int err = sqlite3_exec_nobusy(db.handle(), sqlcmd.c_str(), &ReadJobsCallback, &carg, NULL);
      if(err != SQLITE_OK) {
        // handle error ??
        return false;
      }
    } catch (const SQLiteException& e) {
      return false;
    }

    return true;
  }

  bool JobInformationStorageSQLite::Clean() {
    if (!isValid) {
      return false;
    }

    if (remove(name.c_str()) != 0) {
      if (errno == ENOENT) return true; // No such file. DB already cleaned.
      logger.msg(VERBOSE, "Unable to truncate job database (%s)", name);
      perror("Error");
      return false;
    }
    
    return true;
  }
  
  bool JobInformationStorageSQLite::Remove(const std::list<std::string>& jobids) {
    if (!isValid) {
      return false;
    }

    try {
      JobDB db(name, true);
      for (std::list<std::string>::const_iterator it = jobids.begin();
           it != jobids.end(); ++it) {
        std::string sqlcmd = "DELETE FROM jobs WHERE (id = '"+sql_escape(*it)+"')";
        int err = sqlite3_exec_nobusy(db.handle(), sqlcmd.c_str(), NULL, NULL, NULL); 
        if(err != SQLITE_OK) {
        } else if(sqlite3_changes(db.handle()) < 1) {
        }
      }
    } catch (const SQLiteException& e) {
      return false;
    }
    
    return true;
  }
  
  void JobInformationStorageSQLite::logErrorMessage(int err) {
    switch (err) {
    default:
      logger.msg(DEBUG, "Unable to determine error (%d)", err);
    }
  }

} // namespace Arc

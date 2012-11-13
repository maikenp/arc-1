#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
/*
  Download files specified in job.ID.input and check if user uploaded files.
  Additionally check if this is a migrated job and if so kill the job on old cluster.
  result: 0 - ok, 1 - unrecoverable error, 2 - potentially recoverable,
  3 - certificate error, 4 - should retry.
*/
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <arc/XMLNode.h>
#include <arc/compute/Job.h>
#include <arc/compute/JobControllerPlugin.h>
#include <arc/UserConfig.h>
#include <arc/CheckSum.h>
#include <arc/data/FileCache.h>
#include <arc/data/DataHandle.h>
#include <arc/data/DataMover.h>
#include <arc/message/MCC.h>
#include <arc/StringConv.h>
#include <arc/Thread.h>
#include <arc/URL.h>
#include <arc/Utils.h>
#include <arc/FileUtils.h>

#include "../jobs/GMJob.h"
#include "../files/ControlFileContent.h"
#include "../files/ControlFileHandling.h"
#include "../files/Delete.h"
#include "../misc/proxy.h"
#include "../conf/UrlMapConfig.h"
#include "../conf/CacheConfig.h"
#include "../conf/GMConfig.h"

using namespace ARex;

static Arc::Logger logger(Arc::Logger::getRootLogger(), "Downloader");

/* check for user uploaded files every 60 seconds */
#define CHECK_PERIOD 60
/* check for user uploaded files every 5 seconds if no checksum is involved */
#define CHECK_PERIOD_FAST 5
/* maximum number of retries (for every source/destination) */
#define MAX_RETRIES 5
/* maximum number simultaneous downloads */
#define MAX_DOWNLOADS 5
/* maximum time for user files to upload (per file) */
#define MAX_USER_TIME 600

class PointPair;

static void CollectCredentials(std::string& proxy,std::string& cert,std::string& key,std::string& cadir) {
  proxy=Arc::GetEnv("X509_USER_PROXY");
  if(proxy.empty()) {
    cert=Arc::GetEnv("X509_USER_CERT");
    key=Arc::GetEnv("X509_USER_KEY");
  };
  if(proxy.empty() && cert.empty()) {
    proxy="/tmp/x509_up"+Arc::tostring(getuid());
  };
  cadir=Arc::GetEnv("X509_CERT_DIR");
  if(cadir.empty()) cadir="/etc/grid-security/certificates";
}

class FileDataEx : public FileData {
 public:
  typedef std::list<FileDataEx>::iterator iterator;
  Arc::DataStatus res;
  PointPair* pair;
  /* Times are string to eliminate the need to convert
   * string to time_t while reading from local file
   */
  std::string starttime; /* time of transfer started */
  std::string endtime; /* time of transfer finished */
  /* if the file was retrieved from cache:
   *  - "yes";
   *  - "no";
   */
  std::string fromcache;
  time_t mtime;
  FileDataEx(const FileData& f)
    : FileData(f),
      res(Arc::DataStatus::Success),
      pair(NULL),
      mtime((time_t)(-1)) {}
  FileDataEx(const FileData& f, Arc::DataStatus r)
    : FileData(f),
      res(r),
      pair(NULL),
      mtime((time_t)(-1)) {}
};

static std::list<FileData> job_files_;
static std::list<FileDataEx> job_files;
static std::list<FileDataEx> processed_files;
static std::list<FileDataEx> failed_files;
static Arc::SimpleCondition pair_condition;
static int pairs_initiated = 0;

class SimpleConditionLock {
 private:
  Arc::SimpleCondition& cond_;
 public:
  SimpleConditionLock(Arc::SimpleCondition& cond):cond_(cond) {
    cond_.lock();
  };
  ~SimpleConditionLock(void) {
    cond_.unlock();
  };
};

static int clean_files(std::list<FileData> &job_files,char* session_dir) {
  std::string session(session_dir);
  /* delete only downloadable files, let user manage his/hers files */
  std::list<FileData> tmp;
  if(delete_all_files(session,job_files,false,0,0) != 2) return 0;
  return 1;
}

/*
   Check for existence of user uploadable file
   returns 0 if file exists
           1 - it is not proper file or other error
           2 - not here yet
*/
static int user_file_exists(FileDataEx &dt,char* session_dir,std::list<std::string>* have_files,std::string* error = NULL) {
  struct stat st;
  const char *str = dt.lfn.c_str();
  if(strcmp(str,"*.*") == 0) return 0; /* do not wait for this file */
  std::string fname=std::string(session_dir) + '/' + dt.pfn;
  /* check if file does exist at all */
  // TODO: FileAccess?
  if(lstat(fname.c_str(),&st) != 0) return 2;
  dt.mtime = st.st_mtime;
  /* check for misconfiguration */
  /* parse files information */
  char *str_;
  unsigned long long int fsize;
  unsigned long long int fsum = (unsigned long long int)(-1);
  bool have_size = false;
  bool have_checksum = false;
  errno = 0;
  fsize = strtoull(str,&str_,10);
  if((*str_) == '.') {
    if(str_ != str) have_size=true;
    str=str_+1;
    fsum = strtoull(str,&str_,10);
    if((*str_) != 0) {
      logger.msg(Arc::ERROR, "Invalid checksum in %s for %s", dt.lfn, dt.pfn);
      if(error) (*error)="Bad information about file: checksum can't be parsed.";
      return 1;
    };
    have_checksum=true;
  }
  else {
    if(str_ != str) have_size=true;
    if((*str_) != 0) {
      logger.msg(Arc::ERROR, "Invalid file size in %s for %s ", dt.lfn, dt.pfn);
      if(error) (*error)="Bad information about file: size can't be parsed.";
      return 1;
    };
  };
  if(S_ISDIR(st.st_mode)) {
    if(have_size || have_checksum) {
      if(error) (*error)="Expected file. Directory found.";
      return 1;
    };
    return 0;
  };
  if(!S_ISREG(st.st_mode)) {
    if(error) (*error)="Expected ordinary file. Special object found.";
    return 1;
  };
  /* now check if proper size */
  if(have_size) {
    if(st.st_size < fsize) return 2;
    if(st.st_size > fsize) {
      logger.msg(Arc::ERROR, "Invalid file: %s is too big.", dt.pfn);
      if(error) (*error)="Delivered file is bigger than specified.";
      return 1; /* too big file */
    };
  };
  if(have_files) {
    std::list<std::string>::iterator f = have_files->begin();
    for(;f!=have_files->end();++f) {
      if(dt.pfn == *f) break;
    };
    if(f == have_files->end()) return 2;
  } else if(have_checksum) {
    int h=::open(fname.c_str(),O_RDONLY);
    if(h==-1) { /* if we can't read that file job won't too */
      logger.msg(Arc::ERROR, "Error accessing file %s", dt.pfn);
      if(error) (*error)="Delivered file is unreadable.";
      return 1;
    };
    Arc::CRC32Sum crc;
    char buffer[1024];
    ssize_t l;
    for(;;) {
      if((l=read(h,buffer,1024)) == -1) {
        logger.msg(Arc::ERROR, "Error reading file %s", dt.pfn);
        if(error) (*error)="Could not read file to compute checksum.";
        return 1;
      };
      if(l==0) break;
      crc.add(buffer,l);
    };
    close(h);
    crc.end();
    if(fsum != crc.crc()) {
      if(have_size) { /* size was checked - it is an error to have wrong crc */
        logger.msg(Arc::ERROR, "File %s has wrong CRC.", dt.pfn);
        if(error) (*error)="Delivered file has wrong checksum.";
        return 1;
      };
      return 2; /* just not uploaded yet */
    };
  };
  return 0; /* all checks passed - file is ok */
}

class PointPair {
 public:
  Arc::DataHandle source;
  Arc::DataHandle destination;
  PointPair(const std::string& source_str, const std::string& destination_str,
      const Arc::UserConfig& usercfg)
    : source(source_str, usercfg),
      destination(destination_str, usercfg) {};
  ~PointPair(void) {};
  static void callback(Arc::DataMover*,Arc::DataStatus res,void* arg) {
    FileDataEx::iterator &it = *((FileDataEx::iterator*)arg);
    pair_condition.lock();
    if(!res.Passed()) {
      it->res=res;
      logger.msg(Arc::ERROR, "Failed downloading file %s - %s", it->lfn, std::string(res));
      if((it->pair->source->GetTries() <= 0) || (it->pair->destination->GetTries() <= 0)) {
        delete it->pair; it->pair=NULL;
        failed_files.push_back(*it);
      } else {
        job_files.push_back(*it);
        logger.msg(Arc::ERROR, "Retrying");
      };
    } else {
      logger.msg(Arc::INFO, "Downloaded file %s", it->lfn);
      delete it->pair; it->pair=NULL;
      it->endtime=Arc::Time().str(Arc::UTCTime);
      if (res == Arc::DataStatus::SuccessCached)
        it->fromcache="yes";
      else
        it->fromcache="no";
      processed_files.push_back(*it);
    };
    job_files.erase(it);
    --pairs_initiated;
    pair_condition.signal_nonblock();
    pair_condition.unlock();
    delete &it;
  };
};

int main(int argc,char** argv) {
  Arc::LogStream logcerr(std::cerr);
  Arc::Logger::getRootLogger().addDestination(logcerr);
  Arc::Logger::getRootLogger().setThreshold(Arc::VERBOSE);
  int res=0;
  bool not_uploaded;
  time_t files_changed = 0;
  bool first_loop = true;
  int n_threads = 1;
  int n_files = MAX_DOWNLOADS;
  // Final owner of downloaded files. Modified by -U or -u options
  Arc::User user;
  bool use_conf_cache=false;
  unsigned long long int min_speed = 0;
  time_t min_speed_time = 300;
  unsigned long long int min_average_speed = 0;
  time_t max_inactivity_time = 300;
  bool secure = true;
  bool userfiles_only = false;
  bool passive = false;
  std::string preferred_pattern("");
  std::string failure_reason("");
  std::string x509_proxy, x509_cert, x509_key, x509_cadir;
  srand(time(NULL) + getpid());
  GMConfig config;

  // process optional arguments
  for(;;) {
    opterr=0;
    int optc=getopt(argc,argv,"+hclpfC:n:t:n:u:U:s:S:a:i:d:r:");
    if(optc == -1) break;
    switch(optc) {
      case 'h': {
        std::cerr<<"Usage: downloader [-hclpf] [-C conf_file] [-n files] [-t threads] [-U uid]"<<std::endl;
        std::cerr<<"                  [-u username] [-s min_speed] [-S min_speed_time]"<<std::endl;
        std::cerr<<"                  [-a min_average_speed] [-i min_activity_time]"<<std::endl;
        std::cerr<<"                  [-d debug_level] [-r preferred_pattern] job_id"<<std::endl;
        std::cerr<<"                  control_directory session_directory [cache options]"<<std::endl;
        exit(1);
      };
      case 'c': {
        secure=false;
      }; break;
      case 'C': {
        config.SetConfigFile(optarg);
      }; break;
      case 'l': {
        userfiles_only=true;
      }; break;
      case 'p': {
        passive=true;
      }; break;
      case 'f': {
        use_conf_cache=true;
      }; break;
      case 'd': {
        Arc::Logger::getRootLogger().setThreshold(Arc::string_to_level(optarg));
      }; break;
      case 'r': {
        preferred_pattern = optarg;
      }; break;
      case 't': {
        n_threads=atoi(optarg);
        if(n_threads < 1) {
          logger.msg(Arc::ERROR, "Wrong number of threads: %s", optarg); exit(1);
        };
      }; break;
      case 'n': {
        n_files=atoi(optarg);
        if(n_files < 1) {
          logger.msg(Arc::ERROR, "Wrong number of files: %s", optarg); exit(1);
        };
      }; break;
      case 'U': {
        unsigned int tuid;
        if(!Arc::stringto(std::string(optarg),tuid)) {
          logger.msg(Arc::ERROR, "Bad number: %s", optarg); exit(1);
        };
        user = Arc::User(tuid);
        if(!user) {
          logger.msg(Arc::ERROR, "Specified user can't be handled"); exit(1);
        };
      }; break;
      case 'u': {
        user = Arc::User(optarg);
        if(!user) {
          logger.msg(Arc::ERROR, "Specified user can't be handled"); exit(1);
        };
      }; break;
      case 's': {
        unsigned int tmpi;
        if(!Arc::stringto(std::string(optarg),tmpi)) {
          logger.msg(Arc::ERROR, "Bad number: %s", optarg); exit(1);
        };
        min_speed=tmpi;
      }; break;
      case 'S': {
        unsigned int tmpi;
        if(!Arc::stringto(std::string(optarg),tmpi)) {
          logger.msg(Arc::ERROR, "Bad number: %s", optarg); exit(1);
        };
        min_speed_time=tmpi;
      }; break;
      case 'a': {
        unsigned int tmpi;
        if(!Arc::stringto(std::string(optarg),tmpi)) {
          logger.msg(Arc::ERROR, "Bad number: %s", optarg); exit(1);
        };
        min_average_speed=tmpi;
      }; break;
      case 'i': {
        unsigned int tmpi;
        if(!Arc::stringto(std::string(optarg),tmpi)) {
          logger.msg(Arc::ERROR, "Bad number: %s", optarg); exit(1);
        };
        max_inactivity_time=tmpi;
      }; break;
      case '?': {
        logger.msg(Arc::ERROR, "Unsupported option: %c", (char)optopt);
        exit(1);
      };
      case ':': {
        logger.msg(Arc::ERROR, "Missing parameter for option %c", (char)optopt);
        exit(1);
      };
      default: {
        logger.msg(Arc::ERROR, "Undefined processing error");
        exit(1);
      };
    };
  };
  // process required arguments
  char * id = argv[optind+0];
  if(!id) { logger.msg(Arc::ERROR, "Missing job id"); return 1; };
  char* control_dir = argv[optind+1];
  if(!control_dir) { logger.msg(Arc::ERROR, "Missing control directory"); return 1; };
  char* session_dir = argv[optind+2];
  if(!session_dir) { logger.msg(Arc::ERROR, "Missing session directory"); return 1; };

  config.SetControlDir(control_dir);
  GMJob job(id,user,session_dir);
  Arc::FileCache * cache = NULL;

  if (use_conf_cache) {
    try {
      CacheConfig cache_config(config);
      cache_config.substitute(config, user);
      cache = new Arc::FileCache(cache_config.getCacheDirs(),
                                 cache_config.getRemoteCacheDirs(),
                                 cache_config.getDrainingCacheDirs(),
                                 std::string(id), user.get_uid(), user.get_gid());
      if (!(cache_config.getCacheDirs().empty()) && !(*cache)) {
        logger.msg(Arc::ERROR, "Error creating cache");
        delete cache;
        exit(1);
      }
    }
    catch (CacheConfigException& e) {
      logger.msg(Arc::ERROR, "Error with cache configuration: %s", e.what());
      delete cache;
      exit(1);
    }
  }
  else if(argv[optind+3]) {
    std::string cache_path = argv[optind+3];
    if(argv[optind+4])
      cache_path += " "+std::string(argv[optind+4]);
    cache = new Arc::FileCache(cache_path, std::string(id), user.get_uid(), user.get_gid());
    if (!(*cache)) {
      logger.msg(Arc::ERROR, "Error creating cache");
      delete cache;
      exit(1);
    }
  }
  else {
    // if no cache defined, use null cache
    cache = new Arc::FileCache();
  }

  if(min_speed != 0)
    logger.msg(Arc::VERBOSE, "Minimal speed: %llu B/s during %i s", min_speed, min_speed_time);
  if(min_average_speed != 0)
    logger.msg(Arc::VERBOSE, "Minimal average speed: %llu B/s", min_average_speed);
  if(max_inactivity_time != 0)
    logger.msg(Arc::VERBOSE, "Maximal inactivity time: %i s", max_inactivity_time);

  CollectCredentials(x509_proxy,x509_cert,x509_key,x509_cadir);

  if(n_threads > 10) {
    logger.msg(Arc::WARNING, "Won't use more than 10 threads");
    n_threads=10;
  };
/*
 !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!  Add this to DataMove !!!!!!!!!!!!
*/
  UrlMapConfig url_map(config);
  logger.msg(Arc::INFO, "Downloader started");

  Arc::initializeCredentialsType cred_type(Arc::initializeCredentialsType::SkipCredentials);
  Arc::UserConfig usercfg(cred_type);
  usercfg.UtilsDirPath(control_dir);
  usercfg.SetUser(user);
  usercfg.CACertificatesDirectory(x509_cadir);

  Arc::DataMover mover;
  mover.retry(true);
  mover.secure(secure);
  mover.passive(passive);
  mover.verbose(true); // statistics will be shown if logging is higher than VERBOSE
  mover.set_preferred_pattern(preferred_pattern);
  if(min_speed != 0)
    mover.set_default_min_speed(min_speed,min_speed_time);
  if(min_average_speed != 0)
    mover.set_default_min_average_speed(min_average_speed);
  if(max_inactivity_time != 0)
    mover.set_default_max_inactivity_time(max_inactivity_time);
  bool transferred = true;
  bool credentials_expired = false;
  std::list<FileData> output_files;

  if(!job_input_read_file(job.get_id(),config,job_files_)) {
    failure_reason+="Internal error in downloader\n";
    logger.msg(Arc::ERROR, "Can't read list of input files"); res=1; goto exit;
  };
  // check for duplicates (see bug 1285)
  for (std::list<FileData>::iterator i = job_files_.begin(); i != job_files_.end(); i++) {
    for (std::list<FileData>::iterator j = job_files_.begin(); j != job_files_.end(); j++) {
      if (i != j && j->pfn == i->pfn) {
        failure_reason+="Duplicate input files\n";
        logger.msg(Arc::ERROR, "Error: duplicate file in list of input files: %s", i->pfn);
        res=1;
        goto exit;
      }
    }
  }
  // check if any input files are also output files (bug 1387 and 2793)
  if(job_output_read_file(job.get_id(),config,output_files)) {
    for (std::list<FileData>::iterator j = output_files.begin(); j != output_files.end(); j++) {
      for (std::list<FileData>::iterator i = job_files_.begin(); i != job_files_.end(); i++) {
        if (i->pfn == j->pfn && i->lfn.find(':') != std::string::npos) {
          Arc::URL u(i->lfn);
          std::string opt = u.Option("cache");
          // don't add copy option if exists or current option is "no" or "renew"
          if (opt.empty() || !(opt == "no" || opt == "renew" || opt == "copy")) {
            u.AddOption("cache", "copy", true);
            i->lfn = u.fullstr();
          }
        }
      }
    }
  }
  else
    logger.msg(Arc::WARNING, "Can't read list of output files");

  // remove bad files
  if(clean_files(job_files_,session_dir) != 0) {
    failure_reason+="Internal error in downloader\n";
    logger.msg(Arc::ERROR, "Can't remove junk files"); res=1; goto exit;
  };
  for(std::list<FileData>::iterator i = job_files_.begin();i!=job_files_.end();++i) {
    job_files.push_back(*i);
  };

  if(!job.GetLocalDescription(config)) {
    failure_reason+="Internal error in downloader\n";
    logger.msg(Arc::ERROR, "Can't read job local description"); res=1; goto exit;
  };

  // initialize structures to handle download
  /* TODO: add threads=# to all urls if n_threads!=1 */
  // Main download cycle
  if(!userfiles_only) for(;;) {
    // Initiate transfers
    int n = 0;
    SimpleConditionLock local_lock(pair_condition);
    for(FileDataEx::iterator i=job_files.begin();i!=job_files.end();++i) {
      if(i->lfn.find(":") != std::string::npos) { /* is it lfn ? */
        ++n;
        if(n <= pairs_initiated) continue; // skip files being processed
        if(n > n_files) break; // quit if not allowed to process more
        /* have place and file to download */
        std::string destination=std::string("file://") + session_dir + i->pfn;
        std::string source = i->lfn;
        if(i->pair == NULL) {
          /* define place to store */
          if(strncasecmp(source.c_str(),"file:/",6) == 0) {
            failure_reason+=std::string("User requested local input file ")+source.c_str()+"\n";
            logger.msg(Arc::ERROR, "Local source for download: %s", source); res=1; goto exit;
          };
          if(i->cred.empty()) {
            usercfg.ProxyPath(x509_proxy);
            usercfg.CertificatePath(x509_cert);
            usercfg.KeyPath(x509_key);
          } else {
            usercfg.ProxyPath(i->cred);
            usercfg.CertificatePath("");
            usercfg.KeyPath("");
          };
          PointPair* pair = new PointPair(source,destination,usercfg);
          if(!(pair->source)) {
            failure_reason+=std::string("Can't accept URL ")+source.c_str()+"\n";
            logger.msg(Arc::ERROR, "Can't accept URL: %s", source); delete pair; res=1; goto exit;
          };
          if(!(pair->destination)) {
            failure_reason+=std::string("Can't accept URL ")+destination.c_str()+"\n";
            logger.msg(Arc::ERROR, "Can't accept URL: %s", destination); delete pair; res=1; goto exit;
          };
          i->pair=pair;
        };
        FileDataEx::iterator* it = new FileDataEx::iterator(i);
        std::string prefix = i->pfn;
        if (prefix.find('/') != std::string::npos) prefix.erase(0, prefix.find('/')+1);
        i->starttime=Arc::Time().str(Arc::UTCTime);
        Arc::DataStatus dres = mover.Transfer(*(i->pair->source), *(i->pair->destination), *cache,
                                              url_map, min_speed, min_speed_time,
                                              min_average_speed, max_inactivity_time,
                                              &PointPair::callback, it,
                                              prefix.c_str());
        if (!dres.Passed()) {
          failure_reason+=std::string("Failed to initiate file transfer: ")+source.c_str()+" - "+std::string(dres)+"\n";
          logger.msg(Arc::ERROR, "Failed to initiate file transfer: %s - %s", source, std::string(dres));
          delete it; res=1; goto exit;
        };
        ++pairs_initiated;
      };
    };
    if(pairs_initiated <= 0) break; // Looks like no more files to process
    // Processing initiated - now wait for event
    pair_condition.wait_nonblock();
  };

  {
    std::list<std::string> transfer_stats;
    transfer_stats.clear(); // paranoid initialization
    std::string transfer_parameters;
    // Print download summary and transfer accounting information
    for(FileDataEx::iterator i=processed_files.begin();i!=processed_files.end();++i) {
      logger.msg(Arc::INFO, "Downloaded %s", i->lfn);
      struct stat st;
      Arc::FileStat(job.SessionDir() + i->pfn, &st, true);
      transfer_parameters = "inputfile:";
      transfer_parameters += "url=" + i->lfn + ',';
      transfer_parameters += "size=" + Arc::tostring(st.st_size) + ',';
      transfer_parameters += "starttime=" + i->starttime + ',';
      transfer_parameters += "endtime=" + i->endtime + ',';
      transfer_parameters += "fromcache=" + i->fromcache;
      transfer_stats.push_back(transfer_parameters);
      if(Arc::URL(i->lfn).Option("exec") == "yes") {
        fix_file_permissions(session_dir+i->pfn,true);
      };
    };

    std::string fname = config.ControlDir() + "/job." + job.get_id() + ".statistics";
    std::ofstream f(fname.c_str(),std::ios::out | std::ios::app);
    if(f.is_open() ) {
  	  for (std::list<std::string>::iterator it=transfer_stats.begin(); it != transfer_stats.end(); ++it)
  	    f << *it << std::endl;
      f.close();
    }
  }
  for(FileDataEx::iterator i=failed_files.begin();i!=failed_files.end();++i) {
    if (i->res.Retryable() || i->res == Arc::DataStatus::TransferError) {
      logger.msg(Arc::ERROR, "Failed to download (but may be retried) %s",i->lfn);
      job_files.push_back(*i);
      res = 4;
      continue;
    }
    logger.msg(Arc::ERROR, "Failed to download %s", i->lfn);
    failure_reason+="Input file: "+i->lfn+" - "+(std::string)(i->res)+"\n";
    if(i->res == Arc::DataStatus::CredentialsExpiredError)
      credentials_expired=true;
    transferred=false;
  };
  files_changed = ::time(NULL);
  // Check if all files have been properly downloaded
  if(!transferred) {
    logger.msg(Arc::INFO, "Some downloads failed"); res=2;
    if(credentials_expired) res=3;
    goto exit;
  };
  if(res == 4) logger.msg(Arc::INFO, "Some downloads failed, but may be retried");
  job_files_.clear();
  for(FileDataEx::iterator i = job_files.begin();i!=job_files.end();++i) job_files_.push_back(*i);
  if(!job_input_write_file(job,config,job_files_)) {
    logger.msg(Arc::WARNING, "Failed writing changed input file");
  };
  // check for user uploadable files
  // run cycle waiting for uploaded files
  for(;;) {
    not_uploaded=false;
    std::list<std::string> uploaded_files;
    std::list<std::string>* uploaded_files_ = NULL;
    if(job_input_status_read_file(job.get_id(),config,uploaded_files)) uploaded_files_=&uploaded_files;
    for(FileDataEx::iterator i=job_files.begin();i!=job_files.end();) {
      if(i->lfn.find(":") == std::string::npos) { /* is it lfn ? */
        /* process user uploadable file */
        if(first_loop) logger.msg(Arc::INFO, "Checking user uploadable file: %s", i->pfn);
        std::string error;
        time_t mtime = i->mtime;
        int err=user_file_exists(*i,session_dir,uploaded_files_,&error);
        if(mtime != i->mtime) files_changed = ::time(NULL);
        if(err == 0) { /* file is uploaded */
          logger.msg(Arc::INFO, "User has uploaded file %s", i->pfn);
          i=job_files.erase(i);
          job_files_.clear();
          for(FileDataEx::iterator i = job_files.begin();i!=job_files.end();++i) job_files_.push_back(*i);
          if(!job_input_write_file(job,config,job_files_)) {
            logger.msg(Arc::WARNING, "Failed writing changed input file.");
          };
        }
        else if(err == 1) { /* critical failure */
          logger.msg(Arc::ERROR, "Critical error for uploadable file %s", i->pfn);
          failure_reason+="User file: "+i->pfn+" - "+error+"\n";
          res=1; goto exit;
        }
        else {
          not_uploaded=true; ++i;
        };
      }
      else {
        ++i;
      };
    };
    first_loop = false;
    if(!not_uploaded) break;
    // check for timeout
    unsigned int time_passed = (unsigned int)(time(NULL) - files_changed);
    if(time_passed > max_inactivity_time) {
      logger.msg(Arc::INFO, "No changes in uploadable files for %u seconds",time_passed);
      logger.msg(Arc::ERROR, "Uploadable files timed out"); res=2; break;
    };
    if(uploaded_files_) {
      sleep(CHECK_PERIOD_FAST);
    } else {
      sleep(CHECK_PERIOD);
    };
  };
  job_files_.clear();
  for(FileDataEx::iterator i = job_files.begin();i!=job_files.end();++i) job_files_.push_back(*i);
  if(!job_input_write_file(job,config,job_files_)) {
    logger.msg(Arc::WARNING, "Failed writing changed input file.");
  };

  // Job migration functionality
  if (res == 0) {
    if(job.get_local()->migrateactivityid != "") {
      // Complete the migration.
      const size_t found = job.get_local()->migrateactivityid.rfind("/");

      if (found != std::string::npos) {
        Arc::Job arc_job;
        arc_job.JobID = Arc::URL(job.get_local()->migrateactivityid);
        arc_job.JobManagementURL = Arc::URL(job.get_local()->migrateactivityid.substr(0, found));
        std::list<Arc::Job*> jobs(1, &arc_job);

        Arc::UserConfig usercfg(arc_job.JobManagementURL.Protocol() == "https" ?
                                Arc::initializeCredentialsType() :
                                Arc::initializeCredentialsType(Arc::initializeCredentialsType::SkipCredentials));
        if (arc_job.JobManagementURL.Protocol() != "https" ||
            (arc_job.JobManagementURL.Protocol() == "https" && usercfg.CredentialsFound())) {
          Arc::JobControllerPluginLoader loader;
          Arc::JobControllerPlugin *jobctrl = loader.load("ARC1", usercfg);
          if (jobctrl) {
            jobctrl->UpdateJobs(jobs);
            if ((arc_job.State != Arc::JobState::QUEUING || !jobctrl->CancelJobs(jobs)) && !job.get_local()->forcemigration) {
              res = 1;
              failure_reason = "FATAL ERROR: Migration failed attempting to kill old job \"" + job.get_local()->migrateactivityid + "\".";
            }
          }
          else {
            res = 1;
            failure_reason = "FATAL ERROR: Migration failed, could not locate ARC1 JobControllerPlugin plugin. Maybe it is not installed?";
          }
        }
        else {
          res = 1;
          failure_reason = "FATAL ERROR: Migration failed, unable to find credentials.";
        }
      }
    }
  }

exit:
  // clean unfinished files here
  job_files_.clear();
  for(FileDataEx::iterator i = job_files.begin();i!=job_files.end();++i) job_files_.push_back(*i);
  clean_files(job_files_,session_dir);
  // release cache just in case
  if(res != 0 && res != 4) {
    cache->Release();
  };
  delete cache;
  if(res != 0 && res != 4) {
    job_failed_mark_add(job,config,failure_reason);
  };
  logger.msg(Arc::INFO, "Leaving downloader (%i)", res);
  return res;
}

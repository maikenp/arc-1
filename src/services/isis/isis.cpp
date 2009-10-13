#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <sstream>
#include <map>
#include <math.h>
#include <algorithm>

#include <arc/loader/Loader.h>
#include <arc/data/FileCacheHash.h>
#include <arc/message/PayloadSOAP.h>
#include <arc/message/MCCLoader.h>
#include <arc/client/ClientInterface.h>
#include <arc/Thread.h>
#include <arc/Utils.h>
#include <arc/StringConv.h>

#include "security.h"

#include "isis.h"

#ifdef WIN32
#include <arc/win32.h>
#endif

namespace ISIS
{
static Arc::Logger thread_logger(Arc::Logger::rootLogger, "ISIS_Thread");

// Current time calculation and convert to the UTC time format.
std::string Current_Time( time_t parameter_time = time(NULL) ){

    time_t rawtime;
    if ( parameter_time == time(NULL) ){
        time ( &rawtime );    //current time
    } else {
        rawtime = parameter_time;
    }
    tm * ptm;
    ptm = gmtime ( &rawtime );

    std::string mon_prefix = (ptm->tm_mon+1 < 10)?"0":"";
    std::string day_prefix = (ptm->tm_mday < 10)?"0":"";
    std::string hour_prefix = (ptm->tm_hour < 10)?"0":"";
    std::string min_prefix = (ptm->tm_min < 10)?"0":"";
    std::string sec_prefix = (ptm->tm_sec < 10)?"0":"";
    std::stringstream out;
    if ( parameter_time == time(NULL) ){
        out << ptm->tm_year+1900<<"-"<<mon_prefix<<ptm->tm_mon+1<<"-"<<day_prefix<<ptm->tm_mday<<"T"<<hour_prefix<<ptm->tm_hour<<":"<<min_prefix<<ptm->tm_min<<":"<<sec_prefix<<ptm->tm_sec;
    } else {
        out << ptm->tm_year+1900<<mon_prefix<<ptm->tm_mon+1<<day_prefix<<ptm->tm_mday<<"."<<hour_prefix<<ptm->tm_hour<<min_prefix<<ptm->tm_min<<sec_prefix<<ptm->tm_sec;
    }
    return out.str();
}

class Service_data {
    public:
        std::string serviceID;
        Arc::ISIS_description service;
        std::string peerID;
};

class Thread_data {
    public:
        std::vector<Arc::ISIS_description> isis_list;
        Arc::XMLNode node;
        std::vector<std::string>* not_av_neighbors;
};

static void message_send_thread(void *arg) {
    Arc::AutoPointer<ISIS::Thread_data> data((ISIS::Thread_data*)arg);
    if(!data) return;
    if ( data->isis_list.empty() ) {
       thread_logger.msg(Arc::ERROR, "Empty URL list add to the thread.");
       return;
    }
    if ( !bool(((ISIS::Thread_data *)data)->node) ) {
       thread_logger.msg(Arc::ERROR, "Empty message add to the thread.");
       return;
    }
    std::vector<std::string>* not_availables_neighbors  = data->not_av_neighbors;

    for ( int i=0; i<data->isis_list.size(); i++ ){
        std::string url = data->isis_list[i].url;
        //Send SOAP message to the neighbor.
        Arc::PayloadSOAP *response = NULL;
        Arc::MCCConfig mcc_cfg;
        mcc_cfg.AddPrivateKey(((ISIS::Thread_data *)data)->isis_list[i].key);
        mcc_cfg.AddCertificate(((ISIS::Thread_data *)data)->isis_list[i].cert);
        mcc_cfg.AddProxy(((ISIS::Thread_data *)data)->isis_list[i].proxy);
        mcc_cfg.AddCADir(((ISIS::Thread_data *)data)->isis_list[i].cadir);
        mcc_cfg.AddCAFile(((ISIS::Thread_data *)data)->isis_list[i].cafile);

        Arc::ClientSOAP client_entry(mcc_cfg, url, 60);

        // Create and send "Register/RemoveRegistrations" request
        Arc::NS message_ns;
        //message_ns[""] = "http://www.nordugrid.org/schemas/isis/2007/06";
        message_ns["wsa"] = "http://www.w3.org/2005/08/addressing";
        message_ns["glue2"] = GLUE2_D42_NAMESPACE;
        message_ns["isis"] = ISIS_NAMESPACE;
        Arc::PayloadSOAP req(message_ns);

        req.NewChild(((ISIS::Thread_data *)data)->node);
        Arc::MCC_Status status;
        thread_logger.msg(Arc::DEBUG, "Sending \"Register/RemoveRegistrations\" message to %s and waiting for the response.", url );
        status= client_entry.process(&req,&response);

        if ( (!status.isOk()) || (!response) || (response->IsFault()) ) {
           if ( find(not_availables_neighbors->begin(),not_availables_neighbors->end(),url)
                == not_availables_neighbors->end() && i == 0)
              not_availables_neighbors->push_back(url);
           thread_logger.msg(Arc::ERROR, "Status (%s): Failed", url);
        } else {
           std::vector<std::string>::iterator it;
           it = find(not_availables_neighbors->begin(),not_availables_neighbors->end(),url);
           if ( it != not_availables_neighbors->end() )
              not_availables_neighbors->erase(it);
           thread_logger.msg(Arc::DEBUG, "Status (%s): OK",url );
           if(response) delete response;
           break;
        };
        if(response) delete response;
    }

}

void SendToNeighbors(Arc::XMLNode& node, std::vector<Arc::ISIS_description> neighbors_,
                     Arc::Logger& logger_, Arc::ISIS_description isis_desc, std::vector<std::string>* not_availables_neighbors,
                     std::string endpoint, std::multimap<std::string,Arc::ISIS_description>& hash_table) {
    if ( !bool(node) ) {
       logger_.msg(Arc::WARNING, "Empty message won't be send to the neighbors.");
       return;
    }

    for (std::vector<Arc::ISIS_description>::iterator it = neighbors_.begin(); it < neighbors_.end(); it++) {
        if ( isis_desc.url != (*it).url ) {
           //thread creation
           ISIS::Thread_data* data;
           data = new ISIS::Thread_data;
           std::string url = (*it).url;
           std::string next_url = endpoint;
           if ( it+1 < neighbors_.end() ) {
               next_url = (*(it+1)).url;
           }

           // find neighbor's place in the hash table
           std::multimap<std::string,Arc::ISIS_description>::const_iterator it_hash;
           for (it_hash = hash_table.begin(); it_hash!=hash_table.end(); it_hash++) {
               if ( (it_hash->second).url == url )
                   break;
           }
           // add isis into the list until the next neighbor
           while ( (it_hash->second).url != next_url ){
               if ( 0 < data->isis_list.size() && (it_hash->second).url == url)
                   break;
               Arc::ISIS_description isis(it_hash->second);
               isis.key = isis_desc.key;
               isis.cert = isis_desc.cert;
               isis.proxy = isis_desc.proxy;
               isis.cadir = isis_desc.cadir;
               isis.cafile = isis_desc.cafile;
               data->isis_list.push_back(isis);
               it_hash++;
               if ( it_hash == hash_table.end() )
                   it_hash = hash_table.begin();
           }
           node.New(data->node);
           data->not_av_neighbors = not_availables_neighbors;
           Arc::CreateThreadFunction(&message_send_thread, data);
        }
    }

    return;
}

class Soft_State {
    public:
        std::string function;
        int sleep;
        std::string query;
        Arc::XmlDatabase* database;
        ISIS::ISIService* isis;
        bool* kill_thread;
        int* threads_count;
        bool* available_provider_;
        bool* neighbors_update_needed_;
        std::vector<Arc::ISIS_description>* providers;
};


static void soft_state_thread(void *data) {
    Arc::AutoPointer<Soft_State> self((Soft_State *)data);
    if(!self) return;
    std::string method = self->function;
    unsigned int sleep_time = self->sleep; //seconds
    std::string query_string = self->query;
    Arc::XmlDatabase* db_ = self->database;
    bool* available_providers = self->available_provider_;
    std::vector<Arc::ISIS_description>* providers_ = self->providers;

    (*(self->threads_count))++;

    // "sleep_period" is the time, when the thread wakes up and checks the "KillTread" variable's value and then sleep away.
    unsigned int sleep_period = 60;
    while (true){
        thread_logger.msg(Arc::DEBUG, "%s: %d seconds to the next database cleaning.", method, sleep_time);

        // "sleep_time" is comminuted to some little period
        unsigned int tmp_sleep_time = sleep_time;
        while ( tmp_sleep_time > 0 ) {
            // Whether ISIS's destructor called or not
            if( *(self->kill_thread) ) {
               (*(self->threads_count))--;
               thread_logger.msg(Arc::DEBUG, "%s: Soft-State thread is finished.", method);
               return;
            }

            if( tmp_sleep_time > sleep_period ) {
               tmp_sleep_time = tmp_sleep_time - sleep_period;
               sleep(sleep_period);
            }
            else {
               sleep(tmp_sleep_time);
               tmp_sleep_time = 0;
            }
        }

        // Database cleaning
        std::vector<std::string> service_ids;

        // Query from the database
        std::map<std::string, Arc::XMLNodeList> result;
        db_->queryAll(query_string, result);
        std::map<std::string, Arc::XMLNodeList>::iterator it;
        for (it = result.begin(); it != result.end(); it++) {
            if (it->second.size() == 0 || it->first == "" ) {
                continue;
            }

            // If add better XPath for ETValid, then this block can be remove
            if ( method == "ETValid" ){
               Arc::XMLNode data;
               //The next function calling is db_->get(ServiceID, RegistrationEntry);
               db_->get(it->first, data);
               Arc::Time gentime( (std::string)data["MetaSrcAdv"]["GenTime"]);
               Arc::Period expiration((std::string)data["MetaSrcAdv"]["Expiration"]);

               Arc::Time current_time(Current_Time());

                if ( (gentime.GetTime() + 2* expiration.GetPeriod()) > current_time.GetTime() ) {
                    // Now the information is not expired
                    continue;
                }

                std::string type = (std::string)data["SrcAdv"]["Type"];
                if ( type == "org.nordugrid.infosys.isis") {
                    *(self->neighbors_update_needed_) = true;
                    std::string isis_url = (std::string)data["SrcAdv"]["EPR"]["Address"];
                    // the remove service is my provider or not
                    for ( int j=0; j < providers_->size(); j++ ) {
                        if ( (*providers_)[j].url == isis_url ) {
                            *available_providers = false;
                            break;
                        }
                    }
                }

            }
            // end of the block

            service_ids.push_back(it->first);
        }

        // Remove all old datas
        std::vector<std::string>::iterator id_it;
        for (id_it = service_ids.begin(); id_it != service_ids.end(); id_it++) {
            db_->del(*id_it);
        }
    }
}

    ISIService::ISIService(Arc::Config *cfg):RegisteredService(cfg),logger_(Arc::Logger::rootLogger, "ISIS"),log_stream(NULL),db_(NULL),valid("PT1D"),remove("PT1D"),neighbors_lock(false),neighbors_count(0), available_provider(false), neighbors_update_needed(false) {

        // Set up custom logger if there is any in the configuration
        Arc::XMLNode logger_node = (*cfg)["Logger"];
        if ((bool) logger_node) {
            Arc::LogLevel threshold = Arc::string_to_level((std::string)logger_node.Attribute("level"));
            log_destination.open(((std::string)logger_node).c_str());
            log_stream = new Arc::LogStream(log_destination);
            thread_logger.addDestination(*log_stream);
            logger_.addDestination(*log_stream);
            logger_.setThreshold(threshold);
        }

        logger_.msg(Arc::DEBUG, "Parsing configuration parameters");
        // Endpoint url from the configuration
        endpoint_=(std::string)((*cfg)["endpoint"]);
        logger_.msg(Arc::DEBUG, "Endpoint: %s", endpoint_);
        if ( endpoint_.empty()){
           logger_.msg(Arc::ERROR, "Empty endpoint element in the configuration!");
           return;
        }
        // Key from the configuration
        my_key=(std::string)((*cfg)["KeyPath"]);
        logger_.msg(Arc::DEBUG, "KeyPath: %s", my_key);
        if (my_key.empty()){
           logger_.msg(Arc::WARNING, "Empty KeyPath element in the configuration!");
        }
        // Cert from the configuration
        my_cert=(std::string)((*cfg)["CertificatePath"]);
        logger_.msg(Arc::DEBUG, "CertificatePath: %s", my_cert);
        if (my_cert.empty()){
           logger_.msg(Arc::WARNING, "Empty CertificatePath element in the configuration!");
        }
        // Proxy from the configuration
        my_proxy=(std::string)((*cfg)["ProxyPath"]);
        logger_.msg(Arc::DEBUG, "ProxyPath: %s", my_proxy);
        if (my_proxy.empty()){
           logger_.msg(Arc::WARNING, "Empty ProxyPath element in the configuration!");
        }
        // CaDir from the configuration
        my_cadir=(std::string)((*cfg)["CACertificatesDir"]);
        logger_.msg(Arc::DEBUG, "CACertificatesDir: %s", my_cadir);
        if (my_cadir.empty()){
           logger_.msg(Arc::WARNING, "Empty CACertificatesDir element in the configuration!");
        }

        my_cafile=(std::string)((*cfg)["CACertificatePath"]);
        logger_.msg(Arc::DEBUG, "CACertficatePath: %s", my_cafile);
        if (my_cafile.empty()){
           logger_.msg(Arc::WARNING, "Empty CACertificatePath element in the configuration!");
        }

        // Assigning service description - Glue2 document should go here.
        infodoc_.Assign(Arc::XMLNode(
        "<?xml version=\"1.0\"?><Domains xmlns=\"http://schemas.ogf.org/glue/2008/05/spec_2.0_d41_r01\"><AdminDomain Distributed=\"\"><Services><Service Name=\"\" ID=\"\" Validity=\"\" OtherInfo=\"\"><Associations /><Capability>information.provenance</Capability></Service></Services></AdminDomain></Domains>"
        ),true);


        if ((bool)(*cfg)["retry"]) {
            if (!((std::string)(*cfg)["retry"]).empty()) {
                if(EOF == sscanf(((std::string)(*cfg)["retry"]).c_str(), "%d", &retry) || retry < 1)
                {
                    logger_.msg(Arc::ERROR, "Configuration error. Retry: \"%s\" is not a valid value. Default value will be used.",(std::string)(*cfg)["retry"]);
                    retry = 5;
                }
            } else retry = 5;
        } else retry = 5;

        logger_.msg(Arc::DEBUG, "Retry: %d", retry);

        if ((bool)(*cfg)["sparsity"]) {
            if (!((std::string)(*cfg)["sparsity"]).empty()) {
                if(EOF == sscanf(((std::string)(*cfg)["sparsity"]).c_str(), "%d", &sparsity) || sparsity < 2)
                {
                    logger_.msg(Arc::ERROR, "Configuration error. Sparsity: \"%s\" is not a valid value. Default value will be used.",(std::string)(*cfg)["sparsity"]);
                    sparsity = 2;
                }
            } else sparsity = 2;
        } else sparsity = 2;

        logger_.msg(Arc::DEBUG, "Sparsity: %d", sparsity);

        ThreadsCount = 0;
        KillThread = false;

        // Set up ETValid if there is any in the configuration
        if ((bool)(*cfg)["ETValid"]) {
            if (!((std::string)(*cfg)["ETValid"]).empty()) {
                Arc::Period validp((std::string)(*cfg)["ETValid"]);
                if(validp.GetPeriod() <= 0) {
                    logger_.msg(Arc::ERROR, "Configuration error. ETValid: \"%s\" is not a valid value. Default value will be used.",(std::string)(*cfg)["ETValid"]);
                } else {
                    valid.SetPeriod( validp.GetPeriod() );
                }
            } else logger_.msg(Arc::ERROR, "Configuration error. ETValid is empty. Default value will be used.");
        } else logger_.msg(Arc::DEBUG, "ETValid: Default value will be used.");

        logger_.msg(Arc::DEBUG, "ETValid: %d seconds", valid.GetPeriod());

        // Set up ETRemove if there is any in the configuration
        if ((bool)(*cfg)["ETRemove"]) {
            if (!((std::string)(*cfg)["ETRemove"]).empty()) {
                Arc::Period removep((std::string)(*cfg)["ETRemove"]);
                if(removep.GetPeriod() <= 0) {
                    logger_.msg(Arc::ERROR, "Configuration error. ETRemove: \"%s\" is not a valid value. Default value will be used.",(std::string)(*cfg)["ETRemove"]);
                } else {
                    remove.SetPeriod( removep.GetPeriod() );
                }
            } else logger_.msg(Arc::ERROR, "Configuration error. ETRemove is empty. Default value will be used.");
        } else logger_.msg(Arc::DEBUG, "ETRemove: Default value will be used.");

        logger_.msg(Arc::DEBUG, "ETRemove: %d seconds", remove.GetPeriod());

        ns_["isis"] = "http://www.nordugrid.org/schemas/isis/2008/08";

        std::string db_path = (std::string)(*cfg)["DBPath"];
        if (db_path.empty()) {
            logger_.msg(Arc::ERROR, "Invalid database path definition");
            return;
        }

        // Create ServiceURL hash
        FileCacheHash md5;
        // calculate my hash from the endpoint URL
        my_hash = md5.getHash(endpoint_);

        // Init database
        db_ = new Arc::XmlDatabase(db_path, "isis");

        // -DB cleaning
        std::map<std::string, Arc::XMLNodeList> result;
        db_->queryAll("/*", result);
        std::map<std::string, Arc::XMLNodeList>::iterator it;
        for (it = result.begin(); it != result.end(); it++) {
             if (it->second.size() == 0) {
                continue;
             }
             db_->del(it->first);
        }

        // Connection to the cloud in 6 steps.
        // 1. step: Put it's own EndpoingURL(s) from configuration in the set of neighbors for testing purpose.
        int i=0;
        while ((bool)(*cfg)["InfoProvider"][i]) {
            if ( endpoint_ != (std::string)(*cfg)["InfoProvider"][i]["URL"] ) {
               Arc::ISIS_description isisdesc;
               isisdesc.url = (std::string)(*cfg)["InfoProvider"][i]["URL"];
               infoproviders_.push_back(isisdesc);
            }
            i++;
        }
        // 2.-6. steps are in the BootStrap function.
        BootStrap(retry);

        // Create Soft-State database threads
        // Valid thread creation
        Soft_State* valid_data;
        valid_data = new Soft_State();
        valid_data->function = "ETValid";
        valid_data->sleep = ((int)valid.GetPeriod())/2;

        time_t rawtime;
        time ( &rawtime );    //current time

        time_t valid_time(rawtime - (int)valid.GetPeriod());

        // Current this is the Query
        //"//RegEntry/MetaSrcAdv[count(Expiration)=1 and number(translate(GenTime,'TZ:-','.')) < number('20090420.082903')]/ServiceID"

        // This Query is better, but it is not working now
        //"//RegEntry/MetaSrcAdv[count(Expiration)=1 and ( (years-from-duration(Expiration)*1000) +(months-from-duration(Expiration)*10) + (days-from-duration(Expiration)) + (hours-from-duration(Expiration)*0.01) + (minutes-from-duration(Expiration)*0.0001) + (seconds-from-duration(Expiration)*0.000001) + number(translate(GenTime,'TZ:-','.'))) < number('20090420.132903')]/ServiceID"
        std::string valid_query("//RegEntry/MetaSrcAdv[count(Expiration)=1 and number(translate(GenTime,'TZ:-','.')) < number('");
        valid_query += Current_Time(rawtime);
        valid_query += "')]/ServiceID";

        valid_data->query = valid_query;
        valid_data->database = db_;
        valid_data->kill_thread = &KillThread;
        valid_data->threads_count = &ThreadsCount;
        valid_data->available_provider_ = &available_provider;
        valid_data->providers = &infoproviders_;
        valid_data->neighbors_update_needed_ = &neighbors_update_needed;
        Arc::CreateThreadFunction(&soft_state_thread, valid_data);


        // Remove thread creation
        Soft_State* remove_data;
        remove_data = new Soft_State();
        remove_data->function = "ETRemove";
        remove_data->sleep = ((int)remove.GetPeriod())/2;

        time_t remove_time(rawtime - (int)remove.GetPeriod());
        std::string remove_query("/RegEntry/MetaSrcAdv[count(Expiration)=0 and number(translate(GenTime,'TZ:-','.')) < number('");
        remove_query += Current_Time(rawtime);
        remove_query += "')]/ServiceID";

        remove_data->query = remove_query;
        remove_data->database = db_;
        remove_data->kill_thread = &KillThread;
        remove_data->threads_count = &ThreadsCount;
        Arc::CreateThreadFunction(&soft_state_thread, remove_data);

    }

    ISIService::~ISIService(void){
        // RemoveRegistration message send to neighbors with in my serviceID.
        std::map<std::string, Arc::XMLNodeList> result;
        std::string query_string = "/RegEntry/SrcAdv/EPR[ Address = '";
        query_string += endpoint_;
        query_string += "']";
        db_->queryAll(query_string, result);
        std::map<std::string, Arc::XMLNodeList>::iterator it;
        for (it = result.begin(); it != result.end(); it++) {
            if (it->second.size() == 0 || it->first == "" ) {
                continue;
            }
            Arc::XMLNode data;
            //The next function calling is db_->get(ServiceID, RegistrationEntry);
            db_->get(it->first, data);
            std::string serviceid((std::string)data["MetaSrcAdv"]["ServiceID"]);
            if ( !serviceid.empty() ) {
               Arc::NS reg_ns;
               reg_ns["isis"] = ISIS_NAMESPACE;

               Arc::XMLNode remove_message(reg_ns,"isis:RemoveRegistrations");
               remove_message.NewChild("ServiceID") = serviceid;
               remove_message.NewChild("MessageGenerationTime") = Current_Time();
               Arc::ISIS_description isis;
               isis.url = endpoint_;
               isis.key = my_key;
               isis.cert = my_cert;
               isis.proxy = my_proxy;
               isis.cadir = my_cadir;
               isis.cafile = my_cafile;
               std::multimap<std::string,Arc::ISIS_description> local_hash_table;
               local_hash_table = hash_table;
               logger_.msg(Arc::DEBUG, "RemoveRegistrations message sent to neighbors.");
               SendToNeighbors(remove_message, neighbors_, logger_, isis, &not_availables_neighbors_,endpoint_,local_hash_table);
            }
            break;
        }

        KillThread = true;
        //Waiting until the all RemoveRegistration message send to neighbors.
        sleep(10);
        for (int i=0; i< garbage_collector.size(); i++) {
            delete garbage_collector[i];
        }
        while (ThreadsCount > 0){
            logger_.msg(Arc::DEBUG, "ISIS (%s) has %d more thread%s", endpoint_, ThreadsCount, ThreadsCount>1?"s.":".");
            sleep(10);
        }

        logger_.removeDestinations();
        thread_logger.removeDestinations();
        if (log_stream) delete log_stream;
        log_destination.close();
        if (db_ != NULL) {
            delete db_;
        }
        logger_.msg(Arc::DEBUG, "ISIS (%s) destroyed.", endpoint_);
    }

    bool ISIService::RegistrationCollector(Arc::XMLNode &doc) {
          // RegEntry element generation
          Arc::XMLNode empty(ns_, "RegEntry");
          empty.New(doc);

          doc.NewChild("SrcAdv");
          doc.NewChild("MetaSrcAdv");

          doc["SrcAdv"].NewChild("Type") = "org.nordugrid.infosys.isis";
          Arc::XMLNode peerID = doc["SrcAdv"].NewChild("SSPair");
          peerID.NewChild("Name") = "peerID";
          peerID.NewChild("Value") = my_hash;

          return true;
    }

    Arc::MCC_Status ISIService::Query(Arc::XMLNode &request, Arc::XMLNode &response) {
        std::string querystring_ = request["QueryString"];
        logger_.msg(Arc::DEBUG, "Query received: %s", querystring_);
        if (querystring_.empty()) {
            Arc::SOAPEnvelope fault(ns_, true);
            if (fault) {
                fault.Fault()->Code(Arc::SOAPFault::Sender);
                fault.Fault()->Reason("Invalid query (It is empty.)");
                response.Replace(fault.Child());
            }
            return Arc::MCC_Status();
        }

        std::map<std::string, Arc::XMLNodeList> result;
        db_->queryAll(querystring_, result);
        std::map<std::string, Arc::XMLNodeList>::iterator it;
        for (it = result.begin(); it != result.end(); it++) {
            if (it->second.size() == 0) {
                continue;
            }
            Arc::XMLNode data_;
            //The next function calling is db_->get(ServiceID, RegistrationEntry);
            try {
                db_->get(it->first, data_);
            } catch ( std::exception &e ) {
                Arc::SOAPEnvelope fault(ns_, true);
                if (fault) {
                    fault.Fault()->Code(Arc::SOAPFault::Sender);
                    fault.Fault()->Reason("Invalid query (not supported expression)");
                    fault.Fault()->Reason(e.what());
                    response.Replace(fault.Child());
                }
                return Arc::MCC_Status();
            }
            // add data to output
            response.NewChild(data_);
        }
        return Arc::MCC_Status(Arc::STATUS_OK);
    }

    Arc::MCC_Status ISIService::Register(Arc::XMLNode &request, Arc::XMLNode &response) {
        int i=0;
        while ((bool) request["RegEntry"][i]) {
            Arc::XMLNode regentry_ = request["RegEntry"][i++];
            logger_.msg(Arc::DEBUG, "Register received: ID=%s; EPR=%s; MsgGenTime=%s",
                (std::string) regentry_["MetaSrcAdv"]["ServiceID"], (std::string) regentry_["SrcAdv"]["EPR"]["Address"],
                (std::string) request["Header"]["MessageGenerationTime"]);

            //search and check in the database
            Arc::XMLNode db_regentry;
            //The next function calling is db_->get(ServiceID, RegistrationEntry);
            db_->get((std::string) regentry_["MetaSrcAdv"]["ServiceID"], db_regentry);

            Arc::Time new_gentime((std::string) regentry_["MetaSrcAdv"]["GenTime"]);
            if ( !bool(db_regentry) ||
                ( bool(db_regentry) && Arc::Time((std::string)db_regentry["MetaSrcAdv"]["GenTime"]) < new_gentime ) ) {
               Arc::XMLNode regentry_xml;
               regentry_.New(regentry_xml);
               db_->put((std::string) regentry_["MetaSrcAdv"]["ServiceID"], regentry_xml);
            }
            else {
               regentry_.Destroy();
            }
        }
        //Send to neighbors the Registration(s).
        Arc::ISIS_description isis;
        isis.url = endpoint_;
        isis.key = my_key;
        isis.cert = my_cert;
        isis.proxy = my_proxy;
        isis.cadir = my_cadir;
        isis.cafile = my_cafile;
        if ( bool(request["RegEntry"]) ) {
            std::multimap<std::string,Arc::ISIS_description> local_hash_table;
            local_hash_table = hash_table;
            SendToNeighbors(request, neighbors_, logger_, isis,&not_availables_neighbors_,endpoint_,local_hash_table);
            for (int i=0; bool(request["RegEntry"][i]); i++) {
                Arc::XMLNode regentry = request["RegEntry"][i];
                if ( (std::string)regentry["SrcAdv"]["Type"] == "org.nordugrid.infosys.isis" ) {
                    // Search the hash value in the request message
                    Neighbors_Update( PeerID(regentry) );
                }
           }
        }

        return Arc::MCC_Status(Arc::STATUS_OK);
    }

    Arc::MCC_Status ISIService::RemoveRegistrations(Arc::XMLNode &request, Arc::XMLNode &response) {
        int i=0;
        while ((bool) request["ServiceID"][i]) {
            std::string service_id = (std::string) request["ServiceID"][i];
            logger_.msg(Arc::DEBUG, "RemoveRegistrations received: ID=%s", service_id);

            //search and check in the database
            Arc::XMLNode regentry;
            //The next function calling is db_->get(ServiceID, RegistrationEntry);
            db_->get(service_id, regentry);
            if ( bool(regentry) ) {
               std::string type = (std::string)regentry["SrcAdv"]["Type"];
               if ( type == "org.nordugrid.infosys.isis") {
                  std::string url = (std::string)regentry["SrcAdv"]["EPR"]["Address"];
                  // the remove service is my provider or not
                  for ( int j=0; j < infoproviders_.size(); j++ ) {
                     if ( infoproviders_[j].url == url ) {
                        available_provider = false;
                        break;
                     }
                  }
                  neighbors_update_needed = true;
               }
               Arc::Time old_gentime((std::string)regentry["MetaSrcAdv"]["GenTime"]);
               Arc::Time new_gentime((std::string)request["MessageGenerationTime"]);
               if ( old_gentime >= new_gentime &&  !bool(regentry["MetaSrcAdv"]["Expiration"])) {
                  //Removed the ServiceID from the RemoveRegistrations message.
                  request["ServiceID"][i].Destroy();
               }
               else {
                  //update the database
                  Arc::XMLNode new_data(ns_, "RegEntry");
                  new_data.NewChild("MetaSrcAdv").NewChild("ServiceID") = service_id;
                  new_data["MetaSrcAdv"].NewChild("GenTime") = (std::string)request["MessageGenerationTime"];
                  db_->put(service_id, new_data);
               }
            }
            else {
               //add this element in the database
               Arc::XMLNode new_data(ns_, "RegEntry");
               new_data.NewChild("MetaSrcAdv").NewChild("ServiceID") = service_id;
               new_data["MetaSrcAdv"].NewChild("GenTime") = (std::string)request["MessageGenerationTime"];
               db_->put(service_id, new_data);
            }
            i++;
        }

        // Send RemoveRegistration message to the other(s) neighbors ISIS.
        Arc::ISIS_description isis;
        isis.url = endpoint_;
        isis.key = my_key;
        isis.cert = my_cert;
        isis.proxy = my_proxy;
        isis.cadir = my_cadir;
        isis.cafile = my_cafile;
        if ( bool(request["ServiceID"]) ){
           std::multimap<std::string,Arc::ISIS_description> local_hash_table;
           local_hash_table = hash_table;
           SendToNeighbors(request, neighbors_, logger_, isis, &not_availables_neighbors_,endpoint_,local_hash_table);
           for (int i=0; bool(request["ServiceID"][i]); i++) {
              // Search the hash value in my database
              Arc::XMLNode data;
              //The next function calling is db_->get(ServiceID, RegistrationEntry);
              db_->get((std::string)request["ServiceID"][i], data);
           }
        }
        return Arc::MCC_Status(Arc::STATUS_OK);
    }

    Arc::MCC_Status ISIService::GetISISList(Arc::XMLNode &request, Arc::XMLNode &response) {
        logger_.msg(Arc::DEBUG, "GetISISList received");
        // If the neighbors_ vector is empty, then return with the own
        // address else with the list of neighbors.
        if (neighbors_.size() == 0 ) {
            response.NewChild("EPR") = endpoint_;
        }
        for (std::vector<Arc::ISIS_description>::iterator it = neighbors_.begin(); it < neighbors_.end(); it++) {
            response.NewChild("EPR") = (*it).url;
        }
        return Arc::MCC_Status(Arc::STATUS_OK);
    }

    Arc::MCC_Status ISIService::Connect(Arc::XMLNode &request, Arc::XMLNode &response) {
        logger_.msg(Arc::DEBUG, "Connect received");

        // Database Dump
        response.NewChild("Database");
        std::map<std::string, Arc::XMLNodeList> result;
        db_->queryAll("/RegEntry", result);
        std::map<std::string, Arc::XMLNodeList>::iterator it;
        for (it = result.begin(); it != result.end(); it++) {
            if (it->second.size() == 0) {
               continue;
            }
            Arc::XMLNode data_;
            //The next function calling is db_->get(ServiceID, RegistrationEntry);
            db_->get(it->first, data_);
            // add data to output
            response["Database"].NewChild(data_);
        }

        response.NewChild("Config");
        response.NewChild("EndpointURL") = endpoint_;

        return Arc::MCC_Status(Arc::STATUS_OK);
    }

    bool ISIService::CheckAuth(const std::string& action, Arc::Message &inmsg, Arc::Message &outmsg) {
        inmsg.Auth()->set("ISIS",new ISISSecAttr(action));
        if(!ProcessSecHandlers(inmsg,"incoming")) {
            logger_.msg(Arc::ERROR, "Security check failed in ISIS for incoming message");
            make_soap_fault(outmsg, "Not allowed");
            return false;
        };
        return true;
    }

    bool ISIService::CheckAuth(const std::string& action, Arc::Message &inmsg,Arc::XMLNode &response) {
        inmsg.Auth()->set("ISIS",new ISISSecAttr(action));
        if(!ProcessSecHandlers(inmsg,"incoming")) {
            logger_.msg(Arc::ERROR, "Security check failed in ISIS for incoming message");
            make_soap_fault(response, "Not allowed");
            return false;
        };
        return true;
    }

    #define RegisterXMLPath "RegEntry/MetaSrcAdv/ServiceID"
    #define RemoveXMLPath   "ServiceID"
    static bool IsOwnID(Arc::XMLNode node,const std::string& path,const std::string& id) {
        if(id.empty()) return false;
        Arc::XMLNodeList ids = node.Path(path);
        if(ids.empty()) return false;
        if(ids.size() > 1) return false;
        std::string node_id = *(ids.begin());
        if(node_id != id) return false;
        return true;
    }

    Arc::MCC_Status ISIService::process(Arc::Message &inmsg, Arc::Message &outmsg) {

        // Return with fault if the service wasn't initialized and configured properly.
        if ( db_ == NULL ) return make_soap_fault(outmsg);

        if ( neighbors_count == 0 || !available_provider) {
            BootStrap(1);
            neighbors_update_needed = false;
        } else if ( neighbors_count > 0 && neighbors_.size() == not_availables_neighbors_.size() ){
            // Reposition itself in the peer-to-peer network
            // if disconnected from every neighbors then reconnect to
            // the network
            FileCacheHash md5;
            my_hash = md5.getHash(my_hash);
            BootStrap(retry);
            neighbors_update_needed = false;
        } else if (neighbors_update_needed) {
            Neighbors_Update(my_hash, false);
            neighbors_update_needed = false;
        }

        // Both input and output are supposed to be SOAP
        // Extracting payload
        Arc::PayloadSOAP* inpayload = NULL;
        try {
            inpayload = dynamic_cast<Arc::PayloadSOAP*>(inmsg.Payload());
        } catch(std::exception& e) { };
        if(!inpayload) {
            logger_.msg(Arc::ERROR, "Communication error: input is not SOAP");
            return make_soap_fault(outmsg);
        }

        Arc::PayloadSOAP* outpayload = new Arc::PayloadSOAP(ns_);
        Arc::PayloadSOAP& res = *outpayload;
        Arc::MCC_Status ret = Arc::MCC_Status(Arc::STATUS_OK);
        // TODO: needed agereement on what is service id
        std::string client_id = inmsg.Attributes()->get("TLS:IDENTITYDN");

        // If the requested operation was: Register
        if (MatchXMLName((*inpayload).Child(0), "Register")) {
            Arc::XMLNode r = res.NewChild("isis:RegisterResponse");
            Arc::XMLNode register_ = (*inpayload).Child(0);
            if(CheckAuth(IsOwnID(register_,RegisterXMLPath,client_id)?"service":"isis", inmsg, r)) {
                ret = Register(register_, r);
            }
        }
        // If the requested operation was: Query
        else if (MatchXMLName((*inpayload).Child(0), "Query")) {
            Arc::XMLNode r = res.NewChild("isis:QueryResponse");
            if(CheckAuth("client", inmsg, r)) {
                Arc::XMLNode query_ = (*inpayload).Child(0);
                ret = Query(query_, r);
            }
        }
        // If the requested operation was: RemoveRegistrations
        else if (MatchXMLName((*inpayload).Child(0), "RemoveRegistrations")) {
            Arc::XMLNode r = res.NewChild("isis:RemoveRegistrationsResponse");
            Arc::XMLNode remove_ = (*inpayload).Child(0);
            if(CheckAuth(IsOwnID(remove_,RemoveXMLPath,client_id)?"service":"isis", inmsg, r)) {
                ret = RemoveRegistrations(remove_, r);
            }
        }
        // If the requested operation was: GetISISList
        else if (MatchXMLName((*inpayload).Child(0), "GetISISList")) {
            Arc::XMLNode r = res.NewChild("isis:GetISISListResponse");
            Arc::XMLNode isislist_= (*inpayload).Child(0);
            if(CheckAuth("client", inmsg, r)) {
                Arc::XMLNode isislist_= (*inpayload).Child(0);
                ret = GetISISList(isislist_, r);
            }
        }
        // If the requested operation was: Connect
        else if (MatchXMLName((*inpayload).Child(0), "Connect")) {
            Arc::XMLNode r = res.NewChild("isis:ConnectResponse");
            Arc::XMLNode connect_= (*inpayload).Child(0);
            if(CheckAuth("isis", inmsg, r)) {
                ret = Connect(connect_, r);
            }
        }

        else if(MatchXMLNamespace((*inpayload).Child(0),"http://docs.oasis-open.org/wsrf/rp-2")) {
            if(CheckAuth("client", inmsg, outmsg)) {
                // Update infodoc_
                Arc::XMLNode workingcopy;
                infodoc_.Acquire().New(workingcopy);
                workingcopy["AdminDomain"].Attribute("Distributed") = "True";
                workingcopy["AdminDomain"]["Services"]["Service"].Attribute("Name") = "ISIS";
                workingcopy["AdminDomain"]["Services"]["Service"].Attribute("ID") = endpoint_;
                workingcopy["AdminDomain"]["Services"]["Service"].Attribute("CreationTime") = Current_Time();
                workingcopy["AdminDomain"]["Services"]["Service"].Attribute("Validity") = "600";
                char * otherInfo;
                sprintf(otherInfo, "isis_sparsity=%d", sparsity);
                workingcopy["AdminDomain"]["Services"]["Service"].Attribute("OtherInfo") = (std::string) otherInfo;
                // TODO: Update infodoc_
                infodoc_.Release();
                infodoc_.Assign(workingcopy, true);
                // TODO: do not copy out_ to outpayload.
                Arc::SOAPEnvelope* out_ = infodoc_.Process(*inpayload);
                if(out_) {
                    *outpayload=*out_;
                    delete out_;
                } else {
                    delete outpayload;
                    return make_soap_fault(outmsg);
                }
            }
        }

        outmsg.Payload(outpayload);
        return ret;
    }

    Arc::MCC_Status ISIService::make_soap_fault(Arc::Message &outmsg,const std::string& reason) {
        Arc::PayloadSOAP* outpayload = new Arc::PayloadSOAP(ns_,true);
        Arc::SOAPFault* fault = outpayload?outpayload->Fault():NULL;
        if(fault) {
            fault->Code(Arc::SOAPFault::Receiver);
            if(reason.empty()) {
                fault->Reason("Failed processing request");
            } else {
                fault->Reason(reason);
            }
        }

        outmsg.Payload(outpayload);
        return Arc::MCC_Status(Arc::STATUS_OK);
    }

    void ISIService::make_soap_fault(Arc::XMLNode &response,const std::string& reason) {
        Arc::SOAPEnvelope fault(ns_,true);
        if(fault) {
            fault.Fault()->Code(Arc::SOAPFault::Receiver);
            if(reason.empty()) {
                fault.Fault()->Reason("Failed processing request");
            } else {
                fault.Fault()->Reason(reason);
            }
            response.Replace(fault.Child());
        }
    }

    static Arc::Plugin *get_service(Arc::PluginArgument* arg) {
        Arc::ServicePluginArgument* srvarg = arg?dynamic_cast<Arc::ServicePluginArgument*>(arg):NULL;
        if(!srvarg) return NULL;
        return new ISIService((Arc::Config*)(*srvarg));
    }

    void ISIService::Neighbors_Update(std::string hash, bool remove ) {
        // wait until the neighbors list in used
        while ( neighbors_lock ) {
           //sleep(10);
        }

        // neighbors lock start
        neighbors_lock = true;

        // Examine if the neighbor update is necessary or not
        if ( (remove && hash_table.find(hash) == hash_table.end()) ||
             (!remove && hash_table.find(hash) != hash_table.end())) {
            neighbors_lock = false;
            return;
        }
        // -hash_table recalculate
        hash_table.clear();
        std::map<std::string, Arc::XMLNodeList> result;
        db_->queryAll("/RegEntry/SrcAdv[ Type = 'org.nordugrid.infosys.isis']", result);
        std::map<std::string, Arc::XMLNodeList>::iterator query_it;
        for (query_it = result.begin(); query_it != result.end(); query_it++) {
             if (query_it->second.size() == 0) {
                continue;
             }
             Arc::XMLNode data_;
             //The next function calling is db_->get(ServiceID, RegistrationEntry);
             db_->get(query_it->first, data_);
             Arc::XMLNode regentry = data_;
             Arc::ISIS_description service;
             service.url = (std::string)data_["SrcAdv"]["EPR"]["Address"];
             if ( service.url.empty() )
                service.url = query_it->first;
             hash_table.insert( std::pair<std::string,Arc::ISIS_description>( PeerID(regentry), service) );
        }

        // neighbors count update
        // log(2)x = (log(10)x)/(log(10)2)
        int new_neighbors_count = (int)ceil(log10(hash_table.size())/log10(sparsity));
        logger_.msg(Arc::DEBUG, "Neighbors count recalculate from %d to %d (at ISIS %s)", neighbors_count, new_neighbors_count, endpoint_);

        // neighbors vector filling
        std::multimap<std::string,Arc::ISIS_description>::const_iterator it = hash_table.upper_bound(my_hash);
        Neighbors_Calculate(it, new_neighbors_count);
        neighbors_count = new_neighbors_count;

        // neighbors lock end
        neighbors_lock = false;
        return;
    }

    void ISIService::Neighbors_Calculate(std::multimap<std::string, Arc::ISIS_description>::const_iterator it, int count) {
        int sum_step = 1;
        neighbors_.clear();
        for (int i=0; i<count; i++) {
            if (it == hash_table.end())
               it = hash_table.begin();
            neighbors_.push_back(it->second);
            //calculate the next neighbors
            for (int step=0; step<sum_step; step++){
               it++;
               if (it == hash_table.end())
                  it = hash_table.begin();
            }
            sum_step = sum_step*sparsity;
        }
        return;
    }

    std::string ISIService::PeerID( Arc::XMLNode& regentry){
        std::string peerid;
        for (int j=0; bool(regentry["SrcAdv"]["SSPair"][j]); j++ ){
            if ("peerID" == (std::string)regentry["SrcAdv"]["SSPair"][j]["Name"]){
               peerid = (std::string)regentry["SrcAdv"]["SSPair"][j]["Value"];
               break;
            } else {
               continue;
            }
        }

        if ( peerid.empty() ){
            FileCacheHash md5;
            // calculate hash from the endpoint URL or serviceID
            if ( bool(regentry["SrcAdv"]["EPR"]["Address"]) ){
               peerid = md5.getHash((std::string)regentry["SrcAdv"]["EPR"]["Address"]);
            } else {
               peerid = md5.getHash((std::string)regentry["MetaSrcAdv"]["ServiceID"]);
            }
        }
        return peerid;
    }

    std::string ISIService::Cert( Arc::XMLNode& regentry){
        std::string cert;
        for (int j=0; bool(regentry["SrcAdv"]["SSPair"][j]); j++ ){
            if ("Cert" == (std::string)regentry["SrcAdv"]["SSPair"][j]["Name"]){
               cert = (std::string)regentry["SrcAdv"]["SSPair"][j]["Value"];
               break;
            } else {
               continue;
            }
        }
        return cert;
    }

    std::string ISIService::Key( Arc::XMLNode& regentry){
        std::string key;
        for (int j=0; bool(regentry["SrcAdv"]["SSPair"][j]); j++ ){
            if ("Key" == (std::string)regentry["SrcAdv"]["SSPair"][j]["Name"]){
               key = (std::string)regentry["SrcAdv"]["SSPair"][j]["Value"];
               break;
            } else {
               continue;
            }
        }
        return key;
    }

    std::string ISIService::Proxy( Arc::XMLNode& regentry){
        std::string proxy;
        for (int j=0; bool(regentry["SrcAdv"]["SSPair"][j]); j++ ){
            if ("Proxy" == (std::string)regentry["SrcAdv"]["SSPair"][j]["Name"]){
               proxy = (std::string)regentry["SrcAdv"]["SSPair"][j]["Value"];
               break;
            } else {
               continue;
            }
        }
        return proxy;
    }

    std::string ISIService::CaDir( Arc::XMLNode& regentry){
        std::string cadir;
        for (int j=0; bool(regentry["SrcAdv"]["SSPair"][j]); j++ ){
            if ("CaDir" == (std::string)regentry["SrcAdv"]["SSPair"][j]["Name"]){
               cadir = (std::string)regentry["SrcAdv"]["SSPair"][j]["Value"];
               break;
            } else {
               continue;
            }
        }
        return cadir;
    }

    void ISIService::BootStrap( int retry_count){
        // 2. step: goto InfoProviderISIS (one of the list)
        if ( infoproviders_.size() != 0 ){
            std::srand(time(NULL));
            Arc::ISIS_description rndProvider = infoproviders_[std::rand() % infoproviders_.size()];

            std::map<std::string,int> retry_;
            for( int i=0; i< infoproviders_.size(); i++ ) {
               retry_[ infoproviders_[i].url ] = retry_count;
            }

            // 3. step: Send Query SOAP message to the providerISIS with Filter
            Arc::PayloadSOAP *response = NULL;
            Arc::MCCConfig mcc_cfg;
            mcc_cfg.AddPrivateKey(my_key);
            mcc_cfg.AddCertificate(my_cert);
            mcc_cfg.AddProxy(my_proxy);
            mcc_cfg.AddCADir(my_cadir);
            mcc_cfg.AddCAFile(my_cafile);
            // Create and send "Query" request
            Arc::NS message_ns;
            message_ns[""] = "http://www.nordugrid.org/schemas/isis/2007/06";
            message_ns["wsa"] = "http://www.w3.org/2005/08/addressing";
            message_ns["glue2"] = GLUE2_D42_NAMESPACE;
            message_ns["isis"] = ISIS_NAMESPACE;
            Arc::PayloadSOAP req(message_ns);

            req.NewChild("Query");
            req["Query"].NewChild("QueryString") = "/RegEntry/SrcAdv[ Type = 'org.nordugrid.infosys.isis']";
            Arc::MCC_Status status;

            std::vector<Arc::ISIS_description> temporary_provider;
            temporary_provider = infoproviders_;
            bool isavailable = false;
            while ( !isavailable && retry_.size() > 0 ) {
                Arc::ClientSOAP client_entry(mcc_cfg, rndProvider.url, 60);
                logger_.msg(Arc::DEBUG, "Sending Query message to the InfoProvider (%s) and waiting for the response.", rndProvider.url );
                status= client_entry.process(&req,&response);

                if ( (!status.isOk()) || (!response) || (response->IsFault()) ) {
                   logger_.msg(Arc::INFO, "Query failed at %s, choosing new InfoProvider.", rndProvider.url);
                   retry_[rndProvider.url]--;
                   if ( retry_[rndProvider.url] < 1 ) {
                      retry_.erase(rndProvider.url);
                      for (int i=0; i<temporary_provider.size(); i++){
                          if (temporary_provider[i].url == rndProvider.url){
                             temporary_provider.erase(temporary_provider.begin()+i);
                             break;
                          }
                      }
                      logger_.msg(Arc::INFO, "Remove ISIS (%s) from the list of InfoProviders.", rndProvider.url);
                   }
                   // new provider search
                   if ( temporary_provider.size() > 0 )
                      rndProvider = temporary_provider[std::rand() % temporary_provider.size()];
                } else {
                   logger_.msg(Arc::DEBUG, "Status (%s): OK", rndProvider.url );
                   isavailable = true;
                   bootstrapISIS = rndProvider.url;
                };
            }
            available_provider = isavailable;

            // 4. step: Hash table and neighbors filling
            std::vector<Service_data> find_servicedatas;
            for ( int i=0; bool( (*response)["QueryResponse"]["RegEntry"][i]); i++ ) {
                std::string serviceid = (std::string)(*response)["QueryResponse"]["RegEntry"][i]["MetaSrcAdv"]["ServiceID"];
                if ( serviceid.empty() )
                    continue;
                std::string serviceurl = (std::string)(*response)["QueryResponse"]["RegEntry"][i]["SrcAdv"]["EPR"]["Address"];
                if ( serviceurl.empty() )
                    serviceurl = serviceid;
                Service_data sdata;
                sdata.serviceID = serviceid;
                sdata.service.url = serviceurl;
                Arc::XMLNode regentry = (*response)["QueryResponse"]["RegEntry"][i];
                /*sdata.service.cert = Cert(regentry);
                sdata.service.key = Key(regentry);
                sdata.service.proxy = Proxy(regentry);
                sdata.service.cadir = CaDir(regentry);*/
                sdata.peerID = PeerID(regentry);
                find_servicedatas.push_back( sdata );
            }
            if(response) delete response;

            if ( available_provider )
                hash_table.clear();
            for (int i=0; i < find_servicedatas.size(); i++) {
                // add the hash and the service info into the hash table
                hash_table.insert( std::pair<std::string,Arc::ISIS_description>( find_servicedatas[i].peerID, find_servicedatas[i].service) );
            }

            neighbors_count = 0;
            if ( !isavailable) {
               logger_.msg(Arc::DEBUG, "No InfoProvider is available." );
            }
            else if ( hash_table.size() == 0 ) {
               logger_.msg(Arc::DEBUG, "The hash table is empty. New cloud has been created." );
            } else {
               // log(2)x = (log(10)x)/(log(10)2)
               // and the largest integral value that is not greater than x.
               neighbors_count = (int)ceil(log10(hash_table.size())/log10(sparsity));
               if (neighbors_count == 0)
                  neighbors_count = 1;

               // neighbors vector filling
               std::multimap<std::string,Arc::ISIS_description>::const_iterator it = hash_table.upper_bound(my_hash);
               Neighbors_Calculate(it, neighbors_count);
               logger_.msg(Arc::DEBUG, "Neighbors count: %d", neighbors_.size() );

               // 5. step: Connect message send to one ISIS of the neighbors
               Arc::PayloadSOAP connect_req(message_ns);
               connect_req.NewChild("Connect").NewChild("URL") = endpoint_;

               bool isavailable_connect = false;
               bool no_more_isis = false;
               int current = 0;
               Arc::PayloadSOAP *response_c = NULL;
               while ( !isavailable_connect && !no_more_isis) {
                   int retry_connect = retry;
                   // Try to connect one ISIS of the neighbors list
                   while ( !isavailable_connect && retry_connect>0) {
                       Arc::ClientSOAP connectclient_entry(mcc_cfg, neighbors_[current].url, 60);
                       logger_.msg(Arc::DEBUG, "Sending Connect request to the ISIS(%s) and waiting for the response.", neighbors_[current].url );

                       status= connectclient_entry.process(&connect_req,&response_c);
                       if ( (!status.isOk()) || (!response_c) || (response_c->IsFault()) ) {
                          logger_.msg(Arc::INFO, "Connect status (%s): Failed", neighbors_[current].url );
                          retry_connect--;
                       } else {
                          logger_.msg(Arc::DEBUG, "Connect status (%s): OK", neighbors_[current].url );
                          isavailable_connect = true;
                       };
                   }

                   if ( current+1 == neighbors_.size() ) {
                      no_more_isis = true;
                      not_availables_neighbors_.push_back(neighbors_[current].url);
                      logger_.msg(Arc::DEBUG, "No more available ISIS in the neighbors list." );
                   } else if (!isavailable_connect) {
                      if ( find(not_availables_neighbors_.begin(),not_availables_neighbors_.end(),neighbors_[current].url)
                           == not_availables_neighbors_.end() )
                         not_availables_neighbors_.push_back(neighbors_[current].url);
                      current++;
                   }
               }

               if ( isavailable_connect ){
                  // 6. step: response data processing (DB sync, Config saving)
                  std::vector<std::string>::iterator it;
                  it = find(not_availables_neighbors_.begin(),not_availables_neighbors_.end(),neighbors_[current].url);
                  if ( it != not_availables_neighbors_.end() )
                     not_availables_neighbors_.erase(it);

                  // -DB syncronisation
                  // serviceIDs in my DB
                  std::vector<std::string> ids;
                  std::map<std::string, Arc::XMLNodeList> result;
                  db_->queryAll("/RegEntry", result);
                  std::map<std::string, Arc::XMLNodeList>::iterator it_db;
                  for (it_db = result.begin(); it_db != result.end(); it_db++) {
                      if (it_db->second.size() == 0) {
                          continue;
                      }
                      ids.push_back(it_db->first);
                  }

                  Arc::NS reg_ns;
                  reg_ns["isis"] = ISIS_NAMESPACE;

                  Arc::XMLNode sync_datas(reg_ns,"isis:Register");
                  Arc::XMLNode header = sync_datas.NewChild("isis:Header");

                  header.NewChild("MessageGenerationTime") = Current_Time();

                  for ( int i=0; bool((*response_c)["ConnectResponse"]["Database"]["RegEntry"][i]); i++ ){
                      Arc::XMLNode regentry_xml;
                      (*response_c)["ConnectResponse"]["Database"]["RegEntry"][i].New(regentry_xml);
                      std::string id = regentry_xml["MetaSrcAdv"]["ServiceID"];
                      if ( find(ids.begin(), ids.end(), id) == ids.end() ){
                         db_->put( id, regentry_xml);
                      } else {
                         // ID is in the DataBase.
                         Arc::XMLNode data_;
                         //The next function calling is db_->get(ServiceID, RegistrationEntry);
                         db_->get(id, data_);
                         if ( Arc::Time((std::string)data_["MetaSrcAdv"]["GenTime"]) <=
                              Arc::Time((std::string)regentry_xml["MetaSrcAdv"]["GenTime"])){
                            db_->put( id, regentry_xml);
                         } else {
                            // add data to syncronisation data
                            sync_datas.NewChild(data_);
                         }
                          ids.erase(find(ids.begin(),ids.end(),id));
                      }
                  }

                  // with almost one probability the neighbor update will necessary after connection
                  neighbors_update_needed = true;

                  for (int i=0; i<ids.size(); i++){
                      Arc::XMLNode data_;
                      //The next function calling is db_->get(ServiceID, RegistrationEntry);
                      db_->get(ids[i], data_);
                      sync_datas.NewChild(data_);
                  }
                  if ( bool(sync_datas["RegEntry"]) ){
                     Arc::ISIS_description isis;
                     isis.url = endpoint_;
                     isis.key = my_key;
                     isis.cert = my_cert;
                     isis.proxy = my_proxy;
                     isis.cadir = my_cadir;
                     isis.cafile = my_cafile;
                     std::multimap<std::string,Arc::ISIS_description> local_hash_table;
                     local_hash_table = hash_table;
                     SendToNeighbors(sync_datas, neighbors_, logger_, isis, &not_availables_neighbors_,endpoint_,local_hash_table);
                  }
                  logger_.msg(Arc::DEBUG, "Database mass updated." );
               }
               if (response_c) delete response_c;
            }
        }
        return;
    }
} // namespace

Arc::PluginDescriptor PLUGINS_TABLE_NAME[] = {
    { "isis", "HED:SERVICE", 0, &ISIS::get_service },
    { NULL, NULL, 0, NULL }
};


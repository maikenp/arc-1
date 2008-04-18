// Generated by wsdl2hed 
#ifndef __ARC_CENTRALISI_H__
#define __ARC_CENTRALISI_H__

#include <arc/delegation/DelegationInterface.h>
#include <arc/message/Service.h>
#include <arc/infosys/InformationInterface.h>
#include <arc/infosys/InfoCache.h>
#include <arc/infosys/InfoRegister.h>

namespace CentralISI {

class CentralISIService: public Arc::Service
{

    public:
        CentralISIService(Arc::Config *cfg);
        virtual ~CentralISIService(void);
        virtual Arc::MCC_Status process(Arc::Message &inmsg, Arc::Message &outmsg);
        void InformationCollector(void);
        Arc::InfoRegister *reg;
    private:
        long int reg_period;
        std::string service_id;
        std::string db_path;
        Arc::NS ns;
        Arc::Logger logger;
        Arc::DelegationContainerSOAP delegation;
        Arc::InfoCacheInterface *icache;
        Arc::InfoCache *mcache;

        Arc::MCC_Status make_soap_fault(Arc::Message &outmsg);
        // Operations from WSDL
        Arc::MCC_Status Register(Arc::XMLNode &in, Arc::XMLNode &out);
        Arc::MCC_Status RemoveRegistrations(Arc::XMLNode &in, Arc::XMLNode &out);
        Arc::MCC_Status GetRegistrationStatuses(Arc::XMLNode &in, Arc::XMLNode &out);
        Arc::MCC_Status GetISISList(Arc::XMLNode &in, Arc::XMLNode &out);
        Arc::MCC_Status Get(Arc::XMLNode &in, Arc::XMLNode &out);
        // Info

}; // class CentralISI

} // namespace CentralISI

#endif // __ARC_CENTRALISI_H__

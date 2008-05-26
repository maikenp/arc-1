// cream_client.cpp

#include "cream_client.h"
#include "openSSLFunctions.h"

namespace Arc{
    namespace Cream{
        CREAMClientError::CREAMClientError(const std::string& what) : std::runtime_error(what){  }
        Arc::Logger CREAMClient::logger(Arc::Logger::rootLogger, "CREAM-Client");
        
        static void set_cream_namespaces(Arc::NS& ns) {
            ns["SOAP-ENV"]="http://schemas.xmlsoap.org/soap/envelope/";
            ns["SOAP-ENC"]="http://schemas.xmlsoap.org/soap/encoding/";
            ns["xsi"]="http://www.w3.org/2001/XMLSchema-instance";
            ns["xsd"]="http://www.w3.org/2001/XMLSchema";
            ns["ns1"]="http://www.gridsite.org/namespaces/delegation-2";
            ns["ns2"]="http://glite.org/2007/11/ce/cream/types";
            ns["ns3"]="http://glite.org/2007/11/ce/cream";
        }
                
        CREAMClient::CREAMClient(const Arc::URL& url, const Arc::MCCConfig& cfg) throw(CREAMClientError):client(NULL) {
            logger.msg(Arc::INFO, "Creating a CREAM client.");
            Arc::MCCConfig modified_cfg = cfg;
            std::stringstream uid;
            uid << "/tmp/x509up_u" << getuid() ;
            modified_cfg.AddProxy(uid.str());
            client = new Arc::ClientSOAP(modified_cfg,url.Host(),url.Port(),url.Protocol() == "https",url.Path());
            set_cream_namespaces(cream_ns);
        }
        
        CREAMClient::~CREAMClient() {
            if(client) delete client;
        }
        
        std::string CREAMClient::stat(const std::string& jobid) throw(CREAMClientError) {
            logger.msg(Arc::INFO, "Creating and sending a status request.");
            
            Arc::PayloadSOAP req(cream_ns);
            Arc::NS ns2;
            ns2["ns2"]="http://glite.org/2007/11/ce/cream/types";
            Arc::XMLNode jobStatusRequest = req.NewChild("ns2:JobStatusRequest", ns2);
            Arc::XMLNode jobId = jobStatusRequest.NewChild("ns2:jobId", ns2);
            Arc::XMLNode id = jobId.NewChild("ns2:id", ns2);
            id.Set(jobid);
            if (this->delegationId != "") {
                Arc::XMLNode delegId = jobStatusRequest.NewChild("ns2:delegationProxyId", ns2);
                delegId.Set(this->delegationId);
            }
    
            // Send status request
            Arc::PayloadSOAP* resp = NULL;
            
            // Testing: write the outgoing SOAP message
            std::string test;
            req.GetDoc(test,true);
            std::cout << test << std::endl;

            if(client) {
                Arc::MCC_Status status = client->process("http://glite.org/2007/11/ce/cream/JobStatus",&req,&resp);
                if(resp == NULL) {
                    logger.msg(Arc::ERROR,"There was no SOAP response.");
                    throw CREAMClientError("There was no SOAP response.");
                }
            } else throw CREAMClientError("There is no connection chain configured.");
            
            // Testing: write the incoming SOAP message
            std::cout << "jobStatusResponse:" << std::endl;
            (*resp).GetDoc(test,true);
            std::cout << test << std::endl;
            
            Arc::XMLNode name, failureReason, fault;
            (*resp)["JobStatusResponse"]["result"]["jobStatus"]["name"].New(name);
            if ((*resp)["JobStatusResponse"]["result"]["jobStatus"]["failureReason"]) (*resp)["JobStatusResponse"]["result"]["jobStatus"]["failureReason"].New(failureReason);
            std::string statusarc;
            std::string status = (std::string)name;
            std::string faultstring = (std::string)failureReason;
            
            std::string result = (std::string)name;
            if ((*resp)["JobStatusResponse"]["result"]["JobUnknownFault"]) (*resp)["JobStatusResponse"]["result"]["JobUnknownFault"].New(fault);
            if ((*resp)["JobStatusResponse"]["result"]["JobStatusInvalidFault"]) (*resp)["JobStatusResponse"]["result"]["JobStatusInvalidFault"].New(fault);
            if ((*resp)["JobStatusResponse"]["result"]["DelegationIdMismatchFault"]) (*resp)["JobStatusResponse"]["result"]["DelegationIdMismatchFault"].New(fault);
            if ((*resp)["JobStatusResponse"]["result"]["DateMismatchFault"]) (*resp)["JobStatusResponse"]["result"]["DateMismatchFault"].New(fault);
            if ((*resp)["JobStatusResponse"]["result"]["LeaseIdMismatchFault"]) (*resp)["JobStatusResponse"]["result"]["LeaseIdMismatchFault"].New(fault);
            if ((*resp)["JobStatusResponse"]["result"]["GenericFault"]) (*resp)["JobStatusResponse"]["result"]["GenericFault"].New(fault);
            delete resp;
            if ((bool)fault) throw CREAMClientError((std::string)(fault["Description"]));
            
            //translate to ARC terminology
            if (!status.compare("REGISTERED")) statusarc.assign("ACCEPTING");
            if (!status.compare("PENDING")) statusarc.assign("SUBMITTING");
            if (!status.compare("IDLE")) statusarc.assign("INLRMS:Q");
            if (!status.compare("RUNNING")) statusarc.assign("INLRMS:R");
            if (!status.compare("REALLY-RUNNING")) statusarc.assign("INLRMS:R");
            if (!status.compare("HELD")) statusarc.assign("INLRMS:S");
            if (!status.compare("CANCELLED")) statusarc.assign("KILLED");
            if (!status.compare("DONE-OK")) statusarc.assign("FINISHED");
            if (!status.compare("DONE-FAILED")) statusarc.assign("FAILED"); // Job failure
            if (!status.compare("ABORTED")) statusarc.assign("FAILES"); //failure at LRMS level
            if (!status.compare("UNKNOWN")) statusarc.assign("EXECUTED"); //Chosen because this seems to be the default ARC behaviour
            
            if (status=="") throw CREAMClientError("The job status could not be retrieved.");
            else return statusarc + " (" + faultstring + ")";
            
        }  // CREAMClient::stat()
        
        void CREAMClient::cancel(const std::string& jobid) throw(CREAMClientError) {
            logger.msg(Arc::INFO, "Creating and sending request to terminate a job.");
    
            Arc::PayloadSOAP req(cream_ns);
            Arc::NS ns2;
            ns2["ns2"]="http://glite.org/2007/11/ce/cream/types";
            Arc::XMLNode jobCancelRequest = req.NewChild("ns2:JobCancelRequest", ns2);
            Arc::XMLNode jobId = jobCancelRequest.NewChild("ns2:jobId", ns2);
            Arc::XMLNode id = jobId.NewChild("ns2:id", ns2);
            id.Set(jobid);
            
            // Testing: write the outgoing SOAP message
            std::string test;
            req.GetDoc(test,true);
            std::cout << test << std::endl;
            
            // Send cancel request
            Arc::PayloadSOAP* resp = NULL;
            if(client) {
                Arc::MCC_Status status = client->process("http://glite.org/2007/11/ce/cream/JobCancel",&req,&resp);
                if(resp == NULL) {
                    logger.msg(Arc::ERROR,"There was no SOAP response.");
                    throw CREAMClientError("There was no SOAP response.");
                }
            } else throw CREAMClientError("There is no connection chain configured.");

            // Testing: write the incoming SOAP message
            std::cout << "jobCancelResponse:" << std::endl;
            (*resp).GetDoc(test,true);
            std::cout << test << std::endl;
            
            Arc::XMLNode cancelled, fault;
            (*resp)["JobCancelResponse"]["result"]["jobId"]["id"].New(cancelled);
            std::string result = (std::string)cancelled;
            if ((*resp)["JobCancelResponse"]["result"]["JobUnknownFault"]) (*resp)["JobCancelResponse"]["result"]["JobUnknownFault"].New(fault);
            if ((*resp)["JobCancelResponse"]["result"]["JobStatusInvalidFault"]) (*resp)["JobCancelResponse"]["result"]["JobStatusInvalidFault"].New(fault);
            if ((*resp)["JobCancelResponse"]["result"]["DelegationIdMismatchFault"]) (*resp)["JobCancelResponse"]["result"]["DelegationIdMismatchFault"].New(fault);
            if ((*resp)["JobCancelResponse"]["result"]["DateMismatchFault"]) (*resp)["JobCancelResponse"]["result"]["DateMismatchFault"].New(fault);
            if ((*resp)["JobCancelResponse"]["result"]["LeaseIdMismatchFault"]) (*resp)["JobCancelResponse"]["result"]["LeaseIdMismatchFault"].New(fault);
            if ((*resp)["JobCancelResponse"]["result"]["GenericFault"]) (*resp)["JobCancelResponse"]["result"]["GenericFault"].New(fault);
            delete resp;
            if ((bool)fault) throw CREAMClientError((std::string)(fault["Description"]));
            if (result=="") throw CREAMClientError("Job termination failed.");
        }  // CREAMClient::cancel()
        
        void CREAMClient::purge(const std::string& jobid) throw(CREAMClientError) {
            logger.msg(Arc::INFO, "Creating and sending request to clean a job.");
            
            Arc::PayloadSOAP req(cream_ns);
            Arc::NS ns2;
            ns2["ns2"]="http://glite.org/2007/11/ce/cream/types";
            Arc::XMLNode jobStatusRequest = req.NewChild("ns2:JobPurgeRequest", ns2);
            Arc::XMLNode jobId = jobStatusRequest.NewChild("ns2:jobId", ns2);
            Arc::XMLNode id = jobId.NewChild("ns2:id", ns2);
            id.Set(jobid);
            Arc::XMLNode creamURL = jobId.NewChild("ns2:creamURL", ns2);
            
            // Testing: write the outgoing SOAP message
            std::string test;
            req.GetDoc(test,true);
            std::cout << test << std::endl;
            
            // Send clean request
            Arc::PayloadSOAP* resp = NULL;
            if(client) {
                Arc::MCC_Status status = client->process("http://glite.org/2007/11/ce/cream/JobPurge",&req,&resp);
                if(resp == NULL) {
                    logger.msg(Arc::ERROR,"There was no SOAP response.");
                    throw CREAMClientError("There was no SOAP response.");
                }
            } else throw CREAMClientError("There is no connection chain configured.");
        
            // Testing: write the incoming SOAP message
            std::cout << "jobPurgeResponse:" << std::endl;
            (*resp).GetDoc(test,true);
            std::cout << test << std::endl;
            
            Arc::XMLNode cancelled, fault;
            (*resp)["JobPurgeResponse"]["result"]["jobId"]["id"].New(cancelled);
            std::string result = (std::string)cancelled;
            if ((*resp)["JobPurgeResponse"]["result"]["JobUnknownFault"]) (*resp)["JobPurgeResponse"]["result"]["JobUnknownFault"].New(fault);
            if ((*resp)["JobPurgeResponse"]["result"]["JobStatusInvalidFault"]) (*resp)["JobPurgeResponse"]["result"]["JobStatusInvalidFault"].New(fault);
            if ((*resp)["JobPurgeResponse"]["result"]["DelegationIdMismatchFault"]) (*resp)["JobPurgeResponse"]["result"]["DelegationIdMismatchFault"].New(fault);
            if ((*resp)["JobPurgeResponse"]["result"]["DateMismatchFault"]) (*resp)["JobPurgeResponse"]["result"]["DateMismatchFault"].New(fault);
            if ((*resp)["JobPurgeResponse"]["result"]["LeaseIdMismatchFault"]) (*resp)["JobPurgeResponse"]["result"]["LeaseIdMismatchFault"].New(fault);
            if ((*resp)["JobPurgeResponse"]["result"]["GenericFault"]) (*resp)["JobPurgeResponse"]["result"]["GenericFault"].New(fault);
            delete resp;
            if ((bool)fault) throw CREAMClientError((std::string)(fault["Description"]));
            if (result=="") throw CREAMClientError("Job cleaning failed.");
        }  // CREAMClient::purge()
       
        std::string CREAMClient::registerJob(std::string& jsdl_text) throw(CREAMClientError) {
            logger.msg(Arc::INFO, "Creating and sending job register request.");

            // JSDL -> JDL conversion should be here
            std::string jdl_text = jsdl_text;
            
            Arc::PayloadSOAP req(cream_ns);
            Arc::NS ns2;
            ns2["ns2"]="http://glite.org/2007/11/ce/cream/types";
            Arc::XMLNode jobRegisterRequest = req.NewChild("ns2:JobRegisterRequest", ns2);
            Arc::XMLNode act_job = jobRegisterRequest.NewChild("ns2:JobDescriptionList", ns2);
            Arc::XMLNode jdl_node = act_job.NewChild("ns2:JDL", ns2);
            jdl_node.Set(jdl_text);
            if (this->delegationId != "") {
                Arc::XMLNode delegId = act_job.NewChild("ns2:delegationId", ns2);
                delegId.Set(this->delegationId);
            }
            Arc::XMLNode autostart_node = act_job.NewChild("ns2:autoStart", ns2);
            autostart_node.Set("false");
            Arc::PayloadSOAP* resp = NULL;
            
            logger.msg(Arc::VERBOSE, "Job description to be sent: %s",jdl_text);
            
            // Testing: write the outgoing SOAP message
            std::string test;
            req.GetDoc(test,true);
            std::cout << test << std::endl;
            
            // Send job request
            if(client) {
                Arc::MCC_Status status = client->process("http://glite.org/2007/11/ce/cream/JobRegister", &req,&resp);
                if(!status) {
                    logger.msg(Arc::ERROR, "Submission request failed.");
                    throw CREAMClientError("Submission request failed.");
                }
                if(resp == NULL) {
                    logger.msg(Arc::ERROR,"There was no SOAP response.");
                    throw CREAMClientError("There was no SOAP response.");
                };
            } else throw CREAMClientError("There is no connection chain configured.");
            Arc::XMLNode id, fault;
            (*resp)["JobRegisterResponse"]["result"]["jobId"]["id"].New(id);

            // Testing: write the incoming SOAP message
            std::cout << "jobRegisterResponse:" << std::endl;
            (*resp).GetDoc(test,true);
            std::cout << test << std::endl;

            std::string result = (std::string)id;
            if ((*resp)["JobRegisterResponse"]["result"]["JobUnknownFault"]) (*resp)["JobRegisterResponse"]["result"]["JobUnknownFault"].New(fault);
            if ((*resp)["JobRegisterResponse"]["result"]["JobStatusInvalidFault"]) (*resp)["JobRegisterResponse"]["result"]["JobStatusInvalidFault"].New(fault);
            if ((*resp)["JobRegisterResponse"]["result"]["DelegationIdMismatchFault"]) (*resp)["JobRegisterResponse"]["result"]["DelegationIdMismatchFault"].New(fault);
            if ((*resp)["JobRegisterResponse"]["result"]["DateMismatchFault"]) (*resp)["JobRegisterResponse"]["result"]["DateMismatchFault"].New(fault);
            if ((*resp)["JobRegisterResponse"]["result"]["LeaseIdMismatchFault"]) (*resp)["JobRegisterResponse"]["result"]["LeaseIdMismatchFault"].New(fault);
            if ((*resp)["JobRegisterResponse"]["result"]["GenericFault"]) (*resp)["JobRegisterResponse"]["result"]["GenericFault"].New(fault);
            delete resp;
            if ((bool)fault) throw CREAMClientError((std::string)(fault["Description"]));
            if (result=="") throw CREAMClientError("No job ID has been received");
        } // CREAMClient::registerJob()
       
        void CREAMClient::startJob(const std::string& jobid) throw(CREAMClientError) {
            logger.msg(Arc::INFO, "Creating and sending job start request.");
            
            Arc::PayloadSOAP req(cream_ns);
            Arc::NS ns2;
            ns2["ns2"]="http://glite.org/2007/11/ce/cream/types";
            Arc::XMLNode jobStartRequest = req.NewChild("ns2:JobStartRequest", ns2);
            Arc::XMLNode jobId = jobStartRequest.NewChild("ns2:jobId", ns2);
            Arc::XMLNode id_node = jobId.NewChild("ns2:id", ns2);
            id_node.Set(jobid);
            if (this->delegationId != "") {
                Arc::XMLNode delegId = jobStartRequest.NewChild("ns2:delegationId", ns2);
                delegId.Set(this->delegationId);
            }
            Arc::PayloadSOAP* resp = NULL;
            
            // Testing: write the outgoing SOAP message
            std::string test;
            req.GetDoc(test,true);
            std::cout << test << std::endl;
            
            // Send job request
            if(client) {
                Arc::MCC_Status status = client->process("http://glite.org/2007/11/ce/cream/JobStart", &req,&resp);
                if(!status) {
                    logger.msg(Arc::ERROR, "Submission request failed.");
                    throw CREAMClientError("Submission request failed.");
                }
                if(resp == NULL) {
                    logger.msg(Arc::ERROR,"There was no SOAP response.");
                    throw CREAMClientError("There was no SOAP response.");
                };
            } else throw CREAMClientError("There is no connection chain configured.");
            Arc::XMLNode id, fault;
            (*resp)["JobStartResponse"]["result"]["jobId"]["id"].New(id);

            // Testing: write the incoming SOAP message
            std::cout << "jobStartResponse:" << std::endl;
            (*resp).GetDoc(test,true);
            std::cout << test << std::endl;
            
            std::string result = (std::string)id;
            if ((*resp)["JobStartResponse"]["result"]["JobUnknownFault"]) (*resp)["JobStartResponse"]["result"]["JobUnknownFault"].New(fault);
            if ((*resp)["JobStartResponse"]["result"]["JobStatusInvalidFault"]) (*resp)["JobStartResponse"]["result"]["JobStatusInvalidFault"].New(fault);
            if ((*resp)["JobStartResponse"]["result"]["DelegationIdMismatchFault"]) (*resp)["JobStartResponse"]["result"]["DelegationIdMismatchFault"].New(fault);
            if ((*resp)["JobStartResponse"]["result"]["DateMismatchFault"]) (*resp)["JobStartResponse"]["result"]["DateMismatchFault"].New(fault);
            if ((*resp)["JobStartResponse"]["result"]["LeaseIdMismatchFault"]) (*resp)["JobStartResponse"]["result"]["LeaseIdMismatchFault"].New(fault);
            if ((*resp)["JobStartResponse"]["result"]["GenericFault"]) (*resp)["JobStartResponse"]["result"]["GenericFault"].New(fault);
            delete resp;
            if ((bool)fault) throw CREAMClientError((std::string)(fault["Description"]));
            if (result=="") throw CREAMClientError("Job starting failed.");
        } // CREAMClient::startJob()
       
        std::string CREAMClient::submit(std::string& jsdl_text) throw(CREAMClientError) {
            std::string jobid;
            
            // Register the new job
            try {
                jobid = this->registerJob(jsdl_text);
            } catch (CREAMClientError cce) {
                throw cce;
            }
            
            // File submission should be here
            
            // Start executing of the job
            try {
                this->startJob(jobid);
            } catch (CREAMClientError cce) {
                throw cce;
            }
            
            return jobid;
        }
        
        void CREAMClient::createDelegation(std::string& delegation_id) throw(CREAMClientError) {
            logger.msg(Arc::INFO, "Creating delegation.");
            
            Arc::PayloadSOAP req(cream_ns);
            Arc::NS ns1;
            ns1["ns1"]="http://www.gridsite.org/namespaces/delegation-2";
            Arc::XMLNode getProxyReqRequest = req.NewChild("ns1:getProxyReq", ns1);
            Arc::XMLNode delegid = getProxyReqRequest.NewChild("delegationID", ns1);
            delegid.Set(delegation_id);
            Arc::PayloadSOAP* resp = NULL;
            
            // Testing: write the outgoing SOAP message
            std::string test;
            req.GetDoc(test,true);
            std::cout << test << std::endl;
            
            // Send job request
            if(client) {
                Arc::MCC_Status status = client->process("", &req,&resp);
                if(!status) {
                    logger.msg(Arc::ERROR, "Submission request failed.");
                    throw CREAMClientError("Submission request failed.");
                }
                if(resp == NULL) {
                    logger.msg(Arc::ERROR,"There was no SOAP response.");
                    throw CREAMClientError("There was no SOAP response.");
                };
            } else throw CREAMClientError("There is no connection chain configured.");

            // Testing: write the incoming SOAP message
            std::cout << "getProxyReqResponse:" << std::endl;
            (*resp).GetDoc(test,true);
            std::cout << test << std::endl;
                        
            std::string getProxyReqReturnValue;

            if ((bool)(*resp) && (bool)((*resp)["getProxyReqResponse"]["getProxyReqReturn"]) && ((std::string)(*resp)["getProxyReqResponse"]["getProxyReqReturn"] != "")) {
                getProxyReqReturnValue = (std::string)(*resp)["getProxyReqResponse"]["getProxyReqReturn"];
            }
            else {
                throw CREAMClientError("Delegation creating failed.");
            }
            delete resp;

            std::string proxy = getProxy();
            std::string signedcert;
            char *cert=NULL; 
            int timeleft = getCertTimeLeft(proxy);
            
            if (makeProxyCert(&cert,(char*) getProxyReqReturnValue.c_str(),(char*) proxy.c_str(),(char *) proxy.c_str(),timeleft)) throw CREAMClientError("DelegateProxy failed.");
            signedcert.assign(cert);
  
            Arc::PayloadSOAP req2(cream_ns);
            Arc::XMLNode putProxyRequest = req2.NewChild("ns1:putProxy", ns1);
            Arc::XMLNode delegid_node = putProxyRequest.NewChild("delegationID", ns1);
            delegid_node.Set(delegation_id);
            Arc::XMLNode proxy_node = putProxyRequest.NewChild("proxy", ns1);
            proxy_node.Set(signedcert);
            resp = NULL;
            
            // Testing: write the outgoing SOAP message
            req2.GetDoc(test,true);
            std::cout << test << std::endl;
            
            // Send job request
            if(client) {
                Arc::MCC_Status status = client->process("", &req2,&resp);
                if(!status) {
                    logger.msg(Arc::ERROR, "Submission request failed.");
                    throw CREAMClientError("Submission request failed.");
                }
                if(resp == NULL) {
                    logger.msg(Arc::ERROR,"There was no SOAP response.");
                    throw CREAMClientError("There was no SOAP response.");
                };
            } else throw CREAMClientError("There is no connection chain configured.");

            // Testing: write the incoming SOAP message
            std::cout << "putProxyResponse:" << std::endl;
            (*resp).GetDoc(test,true);
            std::cout << test << std::endl;
            
            if (!(bool)(*resp) || !(bool)((*resp)["putProxyResponse"])) throw CREAMClientError("Delegation creating failed.");
            delete resp;
            
        } // CREAMClient::createDelegation()
        
        void CREAMClient::destroyDelegation(std::string& delegation_id) throw(CREAMClientError) {
            logger.msg(Arc::INFO, "Creating delegation.");
            
            Arc::PayloadSOAP req(cream_ns);
            Arc::NS ns1;
            ns1["ns1"]="http://www.gridsite.org/namespaces/delegation-2";
            Arc::XMLNode getProxyReqRequest = req.NewChild("ns1:destroy", ns1);
            Arc::XMLNode delegid = getProxyReqRequest.NewChild("delegationID", ns1);
            delegid.Set(delegation_id);
            Arc::PayloadSOAP* resp = NULL;
            
            // Testing: write the outgoing SOAP message
            std::string test;
            req.GetDoc(test,true);
            std::cout << test << std::endl;
            
            // Send job request
            if(client) {
                Arc::MCC_Status status = client->process("", &req,&resp);
                if(!status) {
                    logger.msg(Arc::ERROR, "Submission request failed.");
                    throw CREAMClientError("Submission request failed.");
                }
                if(resp == NULL) {
                    logger.msg(Arc::ERROR,"There was no SOAP response.");
                    throw CREAMClientError("There was no SOAP response.");
                };
            } else throw CREAMClientError("There is no connection chain configured.");

            // Testing: write the incoming SOAP message
            std::cout << "destroyProxyResponse:" << std::endl;
            (*resp).GetDoc(test,true);
            std::cout << test << std::endl;
                        
            std::string getProxyReqReturnValue;

            if (!(bool)(*resp) || !(bool)((*resp)["destroyResponse"])) throw CREAMClientError("Delegation destroying failed.");
            delete resp;
            
        } // CREAMClient::destroyDelegation()
    } // namespace cream
} // namespace arc

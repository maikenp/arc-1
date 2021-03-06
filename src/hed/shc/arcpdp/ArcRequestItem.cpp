#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "ArcRequestItem.h"
#include "ArcAttributeFactory.h"
#include <arc/security/ArcPDP/attr/RequestAttribute.h>

using namespace Arc;
using namespace ArcSec;

ArcRequestItem::ArcRequestItem(XMLNode& node, AttributeFactory* attrfactory) : RequestItem(node, attrfactory) {
  //Parse the XMLNode structure, and generate the RequestAttribute object
  XMLNode nd;

  //Parse the <Subject> part
  for ( int i=0;; i++ ){
    std::string type;
    nd = node["Subject"][i];
    if(!nd) break;

    if(!((std::string)(nd.Attribute("Type"))).empty())
      type = (std::string)(nd.Attribute("Type"));
 
    //if the "Subject" is like this: 
    //<Subject AttributeId="urn:arc:subject:dn" Type="X500DN">/O=NorduGrid/OU=UIO/CN=test</Subject>
    if(!(type.empty())&&(nd.Size()==0)){
      Subject sub;
      sub.push_back(new RequestAttribute(nd, attrfactory));
      subjects.push_back(sub);
    }
    //else if like this:
    /*  <Subject>
          <Attribute AttributeId="urn:arc:subject:voms-attribute" Type="xsd:string">administrator</Attribute>
          <Attribute AttributeId="urn:arc:subject:voms-attribute" Type="X500DN">/O=NorduGrid/OU=UIO/CN=admin</Attribute>
        </Subject> */
    else if((type.empty())&&(nd.Size()>0)){
      Subject sub;
      for(int j=0;;j++){
        XMLNode tnd = nd.Child(j);
        if(!tnd) break;
        sub.push_back(new RequestAttribute(tnd, attrfactory));
      }
      subjects.push_back(sub);
    }
    //else if like this:
    /*  <Subject Type="xsd:string">
          <Attribute AttributeId="urn:arc:subject:voms-attribute">administrator</Attribute>
          <Attribute AttributeId="urn:arc:subject:voms-attribute">/O=NorduGrid/OU=UIO/CN=admin</Attribute>
        </Subject> */
    else if(!(type.empty())&&(nd.Size()>0)){
      Subject sub;
      for(int j=0;;j++){
        XMLNode tnd = nd.Child(j);
        if(!tnd) break;
        std::string type_fullname = (nd.Attribute("Type")).Prefix();
        type_fullname = type_fullname + ":Type";
        XMLNode type_prop = tnd.NewAttribute(type_fullname.c_str());
        type_prop = type;
        sub.push_back(new RequestAttribute(tnd, attrfactory));
      }
      subjects.push_back(sub);
    }
    //else if like this: <Subject/>
    else if((type.empty()) && (nd.Size()==0) && (((std::string)nd).empty())) {}
    else {std::cerr <<"Error definition in RequestItem:Subject"<<std::endl;}
  }

  //Parse the <Resource>
  for ( int i=0;; i++ ){
    std::string type;
    nd = node["Resource"][i];
    if(!nd) break;
    if(!((std::string)(nd.Attribute("Type"))).empty())
      type = (std::string)(nd.Attribute("Type"));
    if(!(type.empty())&&(nd.Size()==0)){
      Resource res;
      res.push_back(new RequestAttribute(nd, attrfactory));
      resources.push_back(res);
    }
    else if((type.empty())&&(nd.Size()>0)){
      Resource res;
      for(int j=0;;j++){
        XMLNode tnd = nd.Child(j);
        if(!tnd) break;
        res.push_back(new RequestAttribute(tnd, attrfactory));
      }
      resources.push_back(res);
    }
    else if(!(type.empty())&&(nd.Size()>0)){
      Resource res;
      for(int j=0;;j++){
        XMLNode tnd = nd.Child(j);
        if(!tnd) break;
        std::string type_fullname = (nd.Attribute("Type")).Prefix();
        type_fullname = type_fullname + ":Type";
        XMLNode type_prop = tnd.NewAttribute(type_fullname.c_str());
        type_prop = type;
        res.push_back(new RequestAttribute(tnd, attrfactory));
      }
      resources.push_back(res);
    }
    else if((type.empty()) && (nd.Size()==0) && (((std::string)nd).empty())) {}
    else {std::cerr <<"Error definition in RequestItem:Resource"<<std::endl;}
  }

  //Parse the <Action> part
  for ( int i=0;; i++ ){
    std::string type;
    nd = node["Action"][i];
    if(!nd) break;
    if(!((std::string)(nd.Attribute("Type"))).empty())
      type = (std::string)(nd.Attribute("Type"));
    if(!(type.empty())&&(nd.Size()==0)){
      Action act;
      act.push_back(new RequestAttribute(nd, attrfactory));
      actions.push_back(act);
    }
    else if((type.empty())&&(nd.Size()>0)){
      Action act;
      for(int j=0;;j++){
        XMLNode tnd = nd.Child(j);
        if(!tnd) break;
        act.push_back(new RequestAttribute(tnd, attrfactory));
      }
      actions.push_back(act);
    }
    else if(!(type.empty())&&(nd.Size()>0)){
      Action act;
      for(int j=0;;j++){
        XMLNode tnd = nd.Child(j);
        if(!tnd) break;
        std::string type_fullname = (nd.Attribute("Type")).Prefix();
        type_fullname = type_fullname + ":Type";
        XMLNode type_prop = tnd.NewAttribute(type_fullname.c_str());
        type_prop = type;
        act.push_back(new RequestAttribute(tnd, attrfactory));
      }
      actions.push_back(act);
    }
    else if((type.empty()) && (nd.Size()==0) && (((std::string)nd).empty())) {}
    else {std::cerr <<"Error definition in RequestItem:Action"<<std::endl;}
  }

  //Parse the Context part
  for ( int i=0;; i++ ){
    std::string type;
    nd = node["Context"][i];
    if(!nd) break;
    if(!((std::string)(nd.Attribute("Type"))).empty())
      type = (std::string)(nd.Attribute("Type"));
    if(!(type.empty())&&(nd.Size()==0)){
      Context ctx;
      ctx.push_back(new RequestAttribute(nd, attrfactory));
      contexts.push_back(ctx);
    }
    else if((type.empty())&&(nd.Size()>0)){
      Context ctx;
      for(int j=0;;j++){
        XMLNode tnd = nd.Child(j);
        if(!tnd) break;
        ctx.push_back(new RequestAttribute(tnd, attrfactory));
      }
      contexts.push_back(ctx);
    }
    else if(!(type.empty())&&(nd.Size()>0)){
      Context ctx;
      for(int j=0;;j++){
        XMLNode tnd = nd.Child(j);
        if(!tnd) break;
        std::string type_fullname = (nd.Attribute("Type")).Prefix();
        type_fullname = type_fullname + ":Type";
        XMLNode type_prop = tnd.NewAttribute(type_fullname.c_str());
        type_prop = type;
        ctx.push_back(new RequestAttribute(tnd, attrfactory));
      }
      contexts.push_back(ctx);
    }
    else if((type.empty()) && (nd.Size()==0) && (((std::string)nd).empty())) {}
    else {std::cerr <<"Error definition in RequestItem:Context"<<std::endl;}
  }
}

void ArcRequestItem::removeSubjects() {
  while(!(subjects.empty())){
    Subject sub = subjects.back();
    while(!(sub.empty())){
      delete sub.back();
      sub.pop_back();
    }
    subjects.pop_back();
  }
}
  
void ArcRequestItem::removeResources() {
  while(!(resources.empty())){
    Resource res = resources.back();
    while(!(res.empty())){
      delete res.back();
      res.pop_back();
    }
    resources.pop_back();
  }
}

void ArcRequestItem::removeActions() {
  while(!(actions.empty())){
    Action act = actions.back();
    while(!(act.empty())){
      delete act.back();
      act.pop_back();
    }
    actions.pop_back();
  }
}

void ArcRequestItem::removeContexts() {
  while(!(contexts.empty())){
    Context ctx = contexts.back();
    while(!(ctx.empty())){
      delete ctx.back();
      ctx.pop_back();
    }
    contexts.pop_back();
  }
}

ArcRequestItem::~ArcRequestItem(void){
  removeSubjects();
  removeResources();
  removeActions();
  removeContexts();
}

SubList ArcRequestItem::getSubjects () const{
  return subjects;
}

void ArcRequestItem::setSubjects (const SubList& sl){
  removeSubjects();
  subjects = sl;
}

ResList ArcRequestItem::getResources () const{
  return resources;
}

void ArcRequestItem::setResources (const ResList& rl){
  removeResources();
  resources = rl;
}

ActList ArcRequestItem::getActions () const {
  return actions;
}

void ArcRequestItem::setActions (const ActList& al){
  removeActions();
  actions = al;
}

CtxList ArcRequestItem::getContexts () const{
  return contexts;
}

void ArcRequestItem::setContexts (const CtxList& ctx){
  removeContexts();
  contexts = ctx;
}


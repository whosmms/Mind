#include "ai/SecretStore.h"
#include <QCoreApplication>
#include <QScopeGuard>
#include <QUuid>
#include <cstdio>
int main(int argc,char**argv){
 QCoreApplication app(argc,argv);
 const QString root="LingyaMind/Test/H047/"+QUuid::createUuid().toString(QUuid::WithoutBraces);
 auto first=lingya::createSecretStore(root+"/one");
 auto second=lingya::createSecretStore(root+"/two");
 auto cleanup=qScopeGuard([&]{first->clearApiKey();second->clearApiKey();});
 if(!first->setApiKey("temporary-fake-one",true)||!second->setApiKey("temporary-fake-two",true))return 2;
 auto readFirst=lingya::createSecretStore(root+"/one");
 auto readSecond=lingya::createSecretStore(root+"/two");
 if(readFirst->apiKey()!="temporary-fake-one"||readSecond->apiKey()!="temporary-fake-two")return 3;
 first->clearApiKey();
 if(!lingya::createSecretStore(root+"/one")->apiKey().isEmpty()||lingya::createSecretStore(root+"/two")->apiKey()!="temporary-fake-two")return 4;
 second->clearApiKey();
 if(!lingya::createSecretStore(root+"/two")->apiKey().isEmpty())return 5;
 std::puts("isolated_fake_credentials_write_read_delete_passed");
}

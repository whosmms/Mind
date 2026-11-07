#include <QSize>
#include <cstdio>
int main(){for(const auto input:{QSize(1,1024),QSize(1024,1)}){auto result=input.scaled(320,220,Qt::KeepAspectRatio);std::printf("%dx%d -> %dx%d\n",input.width(),input.height(),result.width(),result.height());}return 0;}

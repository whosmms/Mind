#pragma once
#include "domain/Domain.h"

namespace lingya {


// 新工程与模板（规范§11）：仅在内存创建合法Project，不触碰磁盘。
// 首版不内嵌图片附件，只支持本地图标资源ID（规范§4.1）。
namespace ProjectFactory {

// 空白导图模板：单张树图工作表 + 根节点。M0纵向闭环使用。
Project createMindmap(const QString& title);

// M2起按规范§9.1补充：鱼骨图、流程图、甘特图模板与新手引导工程。

}  // namespace ProjectFactory

}  // namespace lingya
